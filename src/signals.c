/*
 * signals.c - Implementation of the shell's signal handling.
 *
 * Design notes
 * ------------
 * The asynchronous signal handlers installed here do nothing except the
 * minimum that the situation requires.  Everything else (reaping children,
 * printing job notifications) happens from ordinary code at well defined
 * points, which is the only safe way to deal with signals in POSIX:
 * a handler may only call async-signal-safe functions such as write(2),
 * and even those are avoided here.
 *
 * SIGCHLD handling
 * ----------------
 * The SIGCHLD handler does nothing at all.  Its only purpose is to make
 * the signal *caught* instead of ignored: a caught signal interrupts a
 * blocking read(2) in the command loop, so the shell notices that a
 * background job has terminated and can report it without waiting for the
 * user to press a key.  Because SA_RESTART is deliberately not set for
 * SIGCHLD, the read returns EINTR and the interpreter loop restarts.
 *
 * The handler does not call waitpid(2) and does not print anything, for
 * two reasons.  A signal handler may only use async-signal-safe
 * functions, and printf(3) is not one of them.  And reaping in the
 * handler would open a race between the signal delivery and the removal
 * of the job from the job table.
 *
 * SIGINT handling
 * ---------------
 * The shell installs a handler for SIGINT so that the shell itself never
 * dies because of a stray Ctrl+C.  While a foreground child is running the
 * parent blocks SIGINT; the child restores the default disposition.  The
 * result is that Ctrl+C terminates the foreground command (128 + SIGINT is
 * reported as the exit status) but the shell survives and prints a new
 * prompt.  There is no window in which the child could miss the signal
 * because the signal mask is inherited across fork(2).
 */
#include "signals.h"
#include "shell.h"

#include <errno.h>
#include <string.h>

static volatile sig_atomic_t g_interrupt = 0;

/*
 * The SIGCHLD handler deliberately has an empty body.  Installing it is
 * enough: catching the signal is what interrupts the blocking read(2).
 */
static void handle_sigchld(int signo)
{
    (void)signo;
}

static void handle_sigint(int signo)
{
    (void)signo;
    g_interrupt = 1;
}

int signals_install(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);

    /*
     * SIGCHLD: SA_NOCLDSTOP suppresses notifications for stopped children
     * (SIGTSTP/SIGCONT) which would otherwise wake the shell up for no
     * useful reason.  SA_RESTART is deliberately *not* used so that a
     * blocking read in the command loop returns EINTR and the shell can
     * notice a terminating background job promptly.
     */
    sa.sa_handler = handle_sigchld;
    sa.sa_flags = SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0)
        return -1;

    /*
     * SIGINT: the handler only records the event.  SA_RESTART lets an
     * interrupted read restart automatically.
     */
    sa.sa_handler = handle_sigint;
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) < 0)
        return -1;

    return 0;
}

void signals_prepare_fork(sigset_t *saved_mask)
{
    sigset_t block_set;

    sigemptyset(&block_set);
    sigaddset(&block_set, SIGINT);

    if (saved_mask != NULL)
        sigprocmask(SIG_BLOCK, &block_set, saved_mask);
    else
        sigprocmask(SIG_BLOCK, &block_set, NULL);
}

void signals_child_after_fork(const sigset_t *saved_mask, int background)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);

    if (background) {
        /* Background commands ignore SIGINT, as POSIX requires for
         * asynchronous lists in a non-interactive shell. */
        sa.sa_handler = SIG_IGN;
    } else {
        /* Foreground commands must die when the user presses Ctrl+C. */
        sa.sa_handler = SIG_DFL;
    }
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    /* A child must never inherit an ignored SIGPIPE: the classic
     * "producer killed by SIGPIPE" behaviour of pipelines depends on it. */
    sa.sa_handler = SIG_DFL;
    sigaction(SIGPIPE, &sa, NULL);

    if (saved_mask != NULL)
        sigprocmask(SIG_SETMASK, saved_mask, NULL);
}

void signals_parent_after_fork(const sigset_t *saved_mask)
{
    if (saved_mask != NULL)
        sigprocmask(SIG_SETMASK, saved_mask, NULL);
}

int signals_interrupt_pending(void)
{
    return g_interrupt ? 1 : 0;
}

void signals_clear_interrupt(void)
{
    g_interrupt = 0;
}
