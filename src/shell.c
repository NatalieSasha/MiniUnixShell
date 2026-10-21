/*
 * shell.c - The interpreter loop and the management of background jobs.
 *
 * The command line is read with read(2) instead of getline(3) for two
 * reasons:
 *
 *   1. A SIGCHLD that arrives while the shell waits for input interrupts
 *      the read.  getline(3) cannot be resumed reliably in that case,
 *      while the small reader below keeps the bytes that were already
 *      read and simply continues.
 *   2. The reader knows whether a partial line is pending, so the shell
 *      can decide whether it may redraw the prompt without destroying
 *      what the user has already typed.
 */
#include "shell.h"
#include "parser.h"
#include "process.h"
#include "signals.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Hard limit for the amount of data buffered for one command line. */
#define LINE_BUFFER_LIMIT (1u << 20)

/* ------------------------------------------------------------------ */
/* Shell state                                                        */
/* ------------------------------------------------------------------ */

void shell_state_init(shell_state_t *state)
{
    state->jobs = NULL;
    state->job_count = 0;
    state->job_capacity = 0;
    state->next_job_id = 1;
    state->exit_requested = 0;
    state->exit_status = 0;
    state->last_status = 0;
}

void shell_state_destroy(shell_state_t *state)
{
    size_t i;

    if (state == NULL)
        return;
    for (i = 0; i < state->job_count; i++)
        free(state->jobs[i].command_text);
    free(state->jobs);
    shell_state_init(state);
}

int shell_state_add_job(shell_state_t *state, pid_t pid, const char *text)
{
    bg_job_t *slot;

    if (state->job_count == state->job_capacity) {
        size_t newcap = state->job_capacity ? state->job_capacity * 2 : 8;
        bg_job_t *tmp = realloc(state->jobs, newcap * sizeof(*tmp));

        if (tmp == NULL)
            return -1;
        state->jobs = tmp;
        state->job_capacity = newcap;
    }

    slot = &state->jobs[state->job_count];
    slot->pid = pid;
    slot->job_id = state->next_job_id++;
    slot->command_text = strdup(text != NULL ? text : "");
    if (slot->command_text == NULL)
        return -1;

    state->job_count++;
    return slot->job_id;
}

void shell_report_job_done(const bg_job_t *job)
{
    fprintf(stderr, "[%d] Done    %s\n", job->job_id,
            job->command_text != NULL ? job->command_text : "");
}

void shell_state_remove_job(shell_state_t *state, pid_t pid)
{
    size_t i;

    for (i = 0; i < state->job_count; i++) {
        if (state->jobs[i].pid != pid)
            continue;

        shell_report_job_done(&state->jobs[i]);
        free(state->jobs[i].command_text);

        /* Keep the array dense: shift the remaining entries down. */
        if (i + 1 < state->job_count)
            memmove(&state->jobs[i], &state->jobs[i + 1],
                    (state->job_count - i - 1) * sizeof(state->jobs[0]));
        state->job_count--;
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Line reader                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    LINE_OK,          /* a complete line was read              */
    LINE_EOF,         /* end of file, no more data              */
    LINE_INTERRUPTED, /* the read was interrupted by a signal   */
    LINE_TOO_LONG,    /* the line exceeded LINE_BUFFER_LIMIT    */
    LINE_ERROR        /* an unrecoverable read error            */
} line_result_t;

typedef struct {
    char  *buf;   /* raw bytes read from the terminal          */
    size_t cap;   /* allocated size of buf                      */
    size_t len;   /* number of valid bytes in buf              */
    size_t pos;   /* offset of the first byte of the next line */
} linebuf_t;

static void linebuf_init(linebuf_t *lb)
{
    lb->buf = NULL;
    lb->cap = 0;
    lb->len = 0;
    lb->pos = 0;
}

static void linebuf_free(linebuf_t *lb)
{
    free(lb->buf);
    linebuf_init(lb);
}

static void linebuf_reset(linebuf_t *lb)
{
    lb->len = 0;
    lb->pos = 0;
}

/* True when bytes that do not belong to a complete line are pending. */
static int linebuf_has_partial(const linebuf_t *lb)
{
    return lb->pos < lb->len;
}

static int linebuf_grow(linebuf_t *lb)
{
    size_t newcap;
    char *tmp;

    if (lb->cap > LINE_BUFFER_LIMIT / 2)
        newcap = LINE_BUFFER_LIMIT;
    else
        newcap = lb->cap ? lb->cap * 2 : 256;

    if (newcap > LINE_BUFFER_LIMIT)
        newcap = LINE_BUFFER_LIMIT;
    if (newcap <= lb->cap)
        return -1;

    tmp = realloc(lb->buf, newcap);
    if (tmp == NULL)
        return -1;
    lb->buf = tmp;
    lb->cap = newcap;
    return 0;
}

/*
 * Read the next line.  On LINE_OK the returned pointer refers to memory
 * owned by the buffer and stays valid until the next call.
 */
static line_result_t linebuf_next(linebuf_t *lb, char **out_line)
{
    for (;;) {
        char *nl = NULL;
        ssize_t n;

        /*
         * memchr() must not be called with a null pointer, not even with
         * a length of zero, so the search is only started once the buffer
         * exists and actually contains data.
         */
        if (lb->buf != NULL && lb->len > lb->pos)
            nl = memchr(lb->buf + lb->pos, '\n', lb->len - lb->pos);

        if (nl != NULL) {
            size_t idx = (size_t)(nl - lb->buf);

            *nl = '\0';
            *out_line = lb->buf + lb->pos;
            lb->pos = idx + 1;
            return LINE_OK;
        }

        if (lb->len + 2 > lb->cap) {
            /* Drop the bytes of the lines that were already processed. */
            if (lb->pos > 0) {
                memmove(lb->buf, lb->buf + lb->pos, lb->len - lb->pos);
                lb->len -= lb->pos;
                lb->pos = 0;
            }
            if (lb->cap >= LINE_BUFFER_LIMIT)
                return LINE_TOO_LONG;
            if (linebuf_grow(lb) != 0)
                return LINE_ERROR;
        }

        n = read(STDIN_FILENO, lb->buf + lb->len, lb->cap - lb->len - 1);
        if (n > 0) {
            lb->len += (size_t)n;
            continue;
        }
        if (n == 0) {
            /* End of file. */
            if (linebuf_has_partial(lb)) {
                lb->buf[lb->len] = '\0';
                *out_line = lb->buf + lb->pos;
                lb->pos = lb->len;
                return LINE_OK;
            }
            return LINE_EOF;
        }
        if (errno == EINTR) {
            /*
             * A signal arrived.  If the user has already typed part of a
             * line the data is kept and the read continues; otherwise the
             * caller is told that the read was interrupted.
             */
            if (linebuf_has_partial(lb))
                continue;
            return LINE_INTERRUPTED;
        }
        return LINE_ERROR;
    }
}

/* ------------------------------------------------------------------ */
/* Interpreter loop                                                   */
/* ------------------------------------------------------------------ */

static int line_is_blank(const char *s)
{
    while (*s != '\0') {
        if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' &&
            *s != '\v' && *s != '\f')
            return 0;
        s++;
    }
    return 1;
}

static void print_prompt(void)
{
    /*
     * The prompt is written to stderr so that redirecting the shell's
     * standard output to a file does not mix the prompt into the data.
     */
    fputs(SHELL_PROMPT, stderr);
    fflush(stderr);
}

int shell_run(shell_state_t *state)
{
    linebuf_t lb;
    int interactive = isatty(STDIN_FILENO);

    linebuf_init(&lb);

    for (;;) {
        char *line = NULL;
        line_result_t lr;
        job_t job;
        parse_status_t pstatus;
        int code;

        /* Collect background jobs that terminated while we were idle. */
        process_reap_children(state);
        signals_clear_interrupt();

        /*
         * The prompt is only redrawn when the user has not started to
         * type a line, otherwise the prompt would be inserted into the
         * middle of the input.
         */
        if (interactive && !linebuf_has_partial(&lb))
            print_prompt();

        lr = linebuf_next(&lb, &line);
        if (lr == LINE_EOF)
            break;
        if (lr == LINE_ERROR) {
            fprintf(stderr, "%s: read: %s\n", MINISHELL_NAME, strerror(errno));
            break;
        }
        if (lr == LINE_TOO_LONG) {
            fprintf(stderr, "%s: command line is too long\n", MINISHELL_NAME);
            linebuf_reset(&lb);
            continue;
        }
        if (lr == LINE_INTERRUPTED) {
            if (signals_interrupt_pending()) {
                fputc('\n', stderr);
                signals_clear_interrupt();
            }
            continue;
        }

        if (line_is_blank(line))
            continue;

        pstatus = parser_parse(line, &job);
        if (pstatus != PARSE_OK) {
            fprintf(stderr, "%s: %s\n", MINISHELL_NAME,
                    parser_status_message(pstatus));
            job_free(&job);
            continue;
        }

        code = process_execute_job(&job, state);
        job_free(&job);

        state->last_status = code;

        /* Report background jobs that finished while the command ran. */
        process_reap_children(state);

        if (state->exit_requested)
            break;
    }

    linebuf_free(&lb);

    if (!state->exit_requested)
        state->exit_status = state->last_status;

    return state->exit_status;
}
