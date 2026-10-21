/*
 * process.c - Process creation, program execution and child synchronisation.
 *
 * The classical Unix sequence for running an external command is
 *
 *      fork(2)      the child gets a copy of the parent's address space
 *      execvp(3)    the child's image is replaced by the new program
 *      waitpid(2)   the parent blocks until the child terminates
 *
 * fork() and exec() are kept strictly separate in this implementation so
 * that the two independent failure modes can be reported accurately:
 * a failing fork() is a resource problem of the shell, whereas a failing
 * execvp() is a problem of the command and is reported by the child.
 */
#include "process.h"
#include "builtins.h"
#include "pipeline.h"
#include "redirection.h"
#include "shell.h"
#include "signals.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define EXIT_NOT_FOUND  127
#define EXIT_CANNOT_RUN 126

int process_status_to_exit_code(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 0;
}

int process_wait_for(pid_t pid)
{
    int status = 0;
    pid_t r;

    for (;;) {
        r = waitpid(pid, &status, 0);
        if (r == pid)
            break;
        if (r < 0 && errno == EINTR)
            continue; /* interrupted by a signal, simply try again */
        if (r < 0) {
            fprintf(stderr, "%s: waitpid: %s\n", MINISHELL_NAME,
                    strerror(errno));
            return 0;
        }
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Child side                                                         */
/* ------------------------------------------------------------------ */

void process_child_exec(const command_t *cmd, shell_state_t *state)
{
    redirect_save_t save;
    shell_state_t child_state;

    if (cmd == NULL || cmd->argv == NULL || cmd->argv[0] == NULL)
        _exit(EXIT_CANNOT_RUN);

    if (redirection_apply(cmd, &save) != 0) {
        redirection_restore(&save);
        _exit(1);
    }

    /*
     * A built-in that runs in a child (a pipeline stage or a background
     * job) must not be able to modify the parent shell's state.  The
     * child therefore works on a private copy of the state; the job table
     * is explicitly detached so that the child cannot touch it.
     */
    if (builtins_is_builtin(cmd->argv[0])) {
        child_state = *state;
        child_state.jobs = NULL;
        child_state.job_count = 0;
        child_state.job_capacity = 0;
        child_state.exit_requested = 0;

        {
            int status = builtins_execute(cmd->argv, (int)cmd->argc,
                                          &child_state);
            /*
             * The output of the built-in may still sit in the stdio
             * buffer.  _exit() does not flush the buffers, so they have to
             * be flushed explicitly *before* the descriptors are restored;
             * afterwards the data would go to the wrong destination.
             */
            fflush(stdout);
            fflush(stderr);
            redirection_restore(&save);
            _exit(status);
        }
    }

    /*
     * execvp(3) searches PATH when the command name contains no slash and
     * calls execve(2) internally.  It only returns on failure.
     */
    execvp(cmd->argv[0], cmd->argv);

    {
        int code = (errno == ENOENT) ? EXIT_NOT_FOUND : EXIT_CANNOT_RUN;
        fprintf(stderr, "%s: %s: %s\n", MINISHELL_NAME, cmd->argv[0],
                strerror(errno));
        fflush(stderr);
        /*
         * _exit(2) instead of exit(2): the child inherited a copy of the
         * parent's stdio buffers.  Calling exit() here would flush those
         * buffers a second time and could duplicate output that the parent
         * has not written yet.  _exit() skips the flushing entirely.
         */
        _exit(code);
    }
}

/* ------------------------------------------------------------------ */
/* Built-ins executed in the shell process                            */
/* ------------------------------------------------------------------ */

static int run_builtin_in_shell(const command_t *cmd, shell_state_t *state)
{
    redirect_save_t save;
    int status;

    /*
     * Redirections are applied to the shell process itself, therefore the
     * original descriptors are saved first and restored afterwards.
     */
    if (redirection_apply(cmd, &save) != 0) {
        redirection_restore(&save);
        return 1;
    }

    status = builtins_execute(cmd->argv, (int)cmd->argc, state);

    /*
     * The output of the built-in has to leave the buffer while the
     * redirection is still in effect; flushing afterwards would send the
     * data to the restored descriptor instead of the file.
     */
    fflush(stdout);
    fflush(stderr);
    redirection_restore(&save);
    return status;
}

/* ------------------------------------------------------------------ */
/* Job dispatch                                                       */
/* ------------------------------------------------------------------ */

int process_execute_job(const job_t *job, shell_state_t *state)
{
    if (job == NULL || job->count == 0)
        return 0;

    /*
     * Policy: a built-in that is the only command of a foreground job runs
     * inside the shell process so that it can change shell state (`cd`) or
     * terminate the shell (`exit`).  In a pipeline or in the background it
     * runs in a child process (a subshell) and its state changes are
     * discarded, which matches the behaviour of a POSIX shell.
     */
    if (job->count == 1 && !job->background &&
        builtins_is_builtin(job->commands[0].argv[0])) {
        return run_builtin_in_shell(&job->commands[0], state);
    }

    return pipeline_run(job, state);
}

/* ------------------------------------------------------------------ */
/* Reaping                                                            */
/* ------------------------------------------------------------------ */

void process_reap_children(shell_state_t *state)
{
    for (;;) {
        int status = 0;
        pid_t pid;

        /*
         * waitpid(-1, WNOHANG) reaps every child that has already
         * terminated and returns 0 immediately when other children are
         * still running.  Because the reaping is done here and not in the
         * signal handler, no race between signal delivery and job
         * bookkeeping can occur.
         */
        pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;

        shell_state_remove_job(state, pid);
    }
}
