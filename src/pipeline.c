/*
 * pipeline.c - Construction and execution of command pipelines.
 *
 * Descriptor bookkeeping is the delicate part of this module.  The rules
 * that are followed are:
 *
 *   1. Every stage of the pipeline is forked *before* the shell waits for
 *      any of them.  The downstream consumer therefore starts running
 *      immediately and cannot block the upstream producer.
 *   2. In the child the read end of the outgoing pipe is closed and the
 *      write end is duplicated onto descriptor 1; in the parent both ends
 *      are closed as well.  Only the descriptor that connects two
 *      consecutive stages survives, and it survives in exactly one place:
 *      the child of the downstream stage.
 *   3. A pipe is a unidirectional byte stream.  All write ends of a pipe
 *      must be closed before the reader observes end-of-file.  If the shell
 *      kept a write end open, the reader would block forever and the
 *      pipeline would deadlock.
 */
#include "pipeline.h"
#include "process.h"
#include "redirection.h"
#include "shell.h"
#include "signals.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Duplicate `from` onto `to` and release the original descriptor.
 * `to` is only closed indirectly (dup2 replaces it atomically).
 */
static int move_descriptor(int from, int to)
{
    if (from == to)
        return 0;
    if (dup2(from, to) < 0)
        return -1;
    close(from);
    return 0;
}

int pipeline_run(const job_t *job, shell_state_t *state)
{
    size_t n = job->count;
    size_t i;
    pid_t *pids;
    int in_fd = -1;
    int result = 0;
    int failed = 0;

    if (n == 0)
        return 0;

    pids = calloc(n, sizeof(*pids));
    if (pids == NULL) {
        fprintf(stderr, "%s: out of memory\n", MINISHELL_NAME);
        return 1;
    }

    for (i = 0; i < n; i++) {
        int pfd[2];
        sigset_t saved_mask;
        pid_t pid;

        pfd[0] = -1;
        pfd[1] = -1;

        if (i + 1 < n) {
            if (pipe(pfd) < 0) {
                fprintf(stderr, "%s: pipe: %s\n", MINISHELL_NAME,
                        strerror(errno));
                failed = 1;
                break;
            }
        }

        /*
         * Flush the shell's own output before the fork.  The child
         * inherits a copy of the stdio buffers; if the parent still had
         * pending data in them, the child would write that data a second
         * time when it terminates.  Flushing before the fork keeps the
         * child's copy of the buffer empty.
         */
        fflush(stdout);
        fflush(stderr);

        signals_prepare_fork(&saved_mask);
        pid = fork();

        if (pid < 0) {
            signals_parent_after_fork(&saved_mask);
            fprintf(stderr, "%s: fork: %s\n", MINISHELL_NAME, strerror(errno));
            if (pfd[0] >= 0)
                close(pfd[0]);
            if (pfd[1] >= 0)
                close(pfd[1]);
            failed = 1;
            break;
        }

        if (pid == 0) {
            /* ---------------- child ---------------- */
            signals_child_after_fork(&saved_mask, job->background);

            /*
             * A background command must not read from the terminal: the
             * shell would compete with it for the input.  POSIX therefore
             * redirects the standard input of an asynchronous command to
             * /dev/null.  This is only done for the first stage of a
             * pipeline; later stages receive their input from the pipe.
             * An explicit `<` redirection still wins because it is applied
             * afterwards by redirection_apply().
             */
            if (job->background && in_fd < 0) {
                int devnull = open("/dev/null", O_RDONLY);
                if (devnull >= 0)
                    move_descriptor(devnull, STDIN_FILENO);
            }

            if (in_fd >= 0) {
                if (move_descriptor(in_fd, STDIN_FILENO) < 0)
                    _exit(1);
                in_fd = -1;
            }
            if (pfd[1] >= 0) {
                if (move_descriptor(pfd[1], STDOUT_FILENO) < 0)
                    _exit(1);
                pfd[1] = -1;
                if (pfd[0] >= 0) {
                    close(pfd[0]);
                    pfd[0] = -1;
                }
            }

            process_child_exec(&job->commands[i], state);
            _exit(127); /* not reached */
        }

        /* ---------------- parent ---------------- */
        signals_parent_after_fork(&saved_mask);
        pids[i] = pid;

        if (in_fd >= 0) {
            close(in_fd);
            in_fd = -1;
        }
        if (pfd[0] >= 0) {
            /*
             * The read end is handed over to the next stage.  The parent
             * keeps it open only until the next fork has happened; it
             * never reads from it.
             */
            in_fd = pfd[0];
            pfd[0] = -1;
        }
        if (pfd[1] >= 0) {
            /* The parent must not keep the write end open, otherwise the
             * reader would never see end-of-file. */
            close(pfd[1]);
            pfd[1] = -1;
        }
    }

    if (in_fd >= 0) {
        close(in_fd);
        in_fd = -1;
    }

    if (failed) {
        size_t j;

        /* Collect the children that were already started. */
        for (j = 0; j < i; j++) {
            int status = process_wait_for(pids[j]);
            (void)status;
        }
        free(pids);
        return 1;
    }

    if (job->background) {
        char *text = job_describe(job);
        int job_id = shell_state_add_job(state, pids[n - 1], text);

        if (job_id > 0)
            fprintf(stderr, "[%d] %d\n", job_id, (int)pids[n - 1]);
        free(text);
        free(pids);
        return 0;
    }

    {
        size_t j;

        for (j = 0; j < n; j++) {
            int status = process_wait_for(pids[j]);

            /*
             * The exit status of a pipeline is the status of its last
             * command, which is the convention used by POSIX shells.
             */
            if (j + 1 == n)
                result = process_status_to_exit_code(status);
        }
    }

    free(pids);
    return result;
}
