/*
 * shell.h - Shared shell state and the top level interpreter loop.
 *
 * The shell keeps a small amount of global-ish state in a `shell_state_t`
 * structure that is passed explicitly to every function that needs it.  This
 * keeps the modules independently testable and avoids scattering mutable
 * globals across translation units.
 */
#ifndef MINISHELL_SHELL_H
#define MINISHELL_SHELL_H

#include <sys/types.h>
#include <stddef.h>

#define MINISHELL_VERSION "1.0.0"
#define MINISHELL_NAME     "minishell"

/* Maximum accepted length of a single command line (bytes, excluding NUL). */
#define SHELL_MAX_LINE_LENGTH 65536

/* Prompt printed in interactive mode. */
#define SHELL_PROMPT MINISHELL_NAME "$ "

/*
 * A background job that has been started but not yet reaped.
 *
 * Ownership: `command_text` is heap allocated and owned by the job table; it
 * is released when the job is reaped or when the shell state is destroyed.
 */
typedef struct {
    pid_t pid;        /* PID of the last process of the job (job leader)   */
    int   job_id;     /* monotonically increasing job number shown to user */
    char *command_text; /* human readable description, e.g. "sleep 10"     */
} bg_job_t;

/*
 * Mutable interpreter state.
 */
typedef struct {
    bg_job_t *jobs;        /* table of running background jobs        */
    size_t    job_count;
    size_t    job_capacity;
    int       next_job_id; /* 1 for the first job, like a POSIX shell  */

    int exit_requested;    /* set by the `exit` built-in               */
    int exit_status;       /* status the shell will terminate with    */
    int last_status;       /* status of the most recent command       */
} shell_state_t;

void shell_state_init(shell_state_t *state);
void shell_state_destroy(shell_state_t *state);

/*
 * Register a freshly forked background job.  Returns the job number on
 * success and -1 if the job could not be registered.
 */
int  shell_state_add_job(shell_state_t *state, pid_t pid, const char *text);

/* Remove a job from the table (used by the reaper). */
void shell_state_remove_job(shell_state_t *state, pid_t pid);

/* Print "[n] Done   <text>" for a completed background job. */
void shell_report_job_done(const bg_job_t *job);

/* The interactive interpreter loop.  Returns the shell's exit status. */
int  shell_run(shell_state_t *state);

#endif /* MINISHELL_SHELL_H */
