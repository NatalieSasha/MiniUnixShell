/*
 * signals.h - Signal disposition management.
 *
 * The shell needs precise control over three signals:
 *
 *   SIGINT  - must reach the foreground child but must not kill the shell.
 *   SIGCHLD - used as a cheap notification that a child terminated; the
 *             actual reaping is done with waitpid(2) from normal code, never
 *             from inside the signal handler.
 *   SIGPIPE - restored to its default disposition in children so that the
 *             classic "producer writes into a closed pipe" behaviour of
 *             pipelines is preserved.
 */
#ifndef MINISHELL_SIGNALS_H
#define MINISHELL_SIGNALS_H

#include <signal.h>

/* Install the shell's signal handlers.  Returns 0 on success, -1 on error. */
int  signals_install(void);

/*
 * Block SIGINT in the parent and remember the previous signal mask.
 * Must be called immediately before fork(2).
 */
void signals_prepare_fork(sigset_t *saved_mask);

/*
 * Child side of signals_prepare_fork(): restore the default disposition of
 * SIGINT (so that Ctrl+C terminates the command) and restore the previous
 * signal mask.  A background child ignores SIGINT instead, as required by
 * POSIX for asynchronous commands.
 */
void signals_child_after_fork(const sigset_t *saved_mask, int background);

/*
 * Parent side of signals_prepare_fork(): restore the previous signal mask
 * once the child has been created.
 */
void signals_parent_after_fork(const sigset_t *saved_mask);

/* True if SIGINT has been delivered since the flag was last cleared. */
int  signals_interrupt_pending(void);
void signals_clear_interrupt(void);

#endif /* MINISHELL_SIGNALS_H */
