/*
 * process.h - Process creation, program execution and child synchronisation.
 */
#ifndef MINISHELL_PROCESS_H
#define MINISHELL_PROCESS_H

#include "parser.h"
#include "shell.h"

/*
 * Execute a complete job.
 *
 * A job that consists of a single built-in command and is not running in
 * the background is executed inside the shell process itself, so that
 * state changing built-ins such as `cd` and `exit` take effect.  Every
 * other job is executed by forking one or more child processes.
 *
 * Returns the exit status the shell should report for the job.
 */
int process_execute_job(const job_t *job, shell_state_t *state);

/*
 * Child side helper: apply the redirections of `cmd` and replace the
 * current process image with the requested program.  Never returns; on
 * failure it prints a diagnostic and terminates the child with _exit(2).
 */
void process_child_exec(const command_t *cmd, shell_state_t *state);

/* Translate the value returned by waitpid(2) into a shell exit status. */
int process_status_to_exit_code(int status);

/*
 * Reap every child that has terminated.  Background jobs are reported and
 * removed from the job table.  Safe to call at any point where the shell
 * is not waiting for a specific child.
 */
void process_reap_children(shell_state_t *state);

/* Wait for one specific child, retrying on EINTR. */
int process_wait_for(pid_t pid);

#endif /* MINISHELL_PROCESS_H */
