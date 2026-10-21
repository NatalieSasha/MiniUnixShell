/*
 * redirection.h - File descriptor manipulation for `<`, `>` and `>>`.
 *
 * Redirection has to work in two very different situations:
 *
 *   - In a child process the descriptors are replaced and then the program
 *     image is replaced by execvp(); nothing ever has to be restored.
 *   - In the parent process (built-in commands) the original descriptors
 *     must be saved and restored afterwards, otherwise the shell's own
 *     standard output would be permanently redirected.
 */
#ifndef MINISHELL_REDIRECTION_H
#define MINISHELL_REDIRECTION_H

#include "parser.h"

/* File creation mode for `>` and `>>`; the effective mode is 0666 & ~umask. */
#define REDIRECTION_CREATE_MODE 0666

typedef struct {
    int saved_stdin;   /* duplicate of the original fd 0, or -1 */
    int saved_stdout;  /* duplicate of the original fd 1, or -1 */
} redirect_save_t;

void redirect_save_init(redirect_save_t *save);

/*
 * Apply the redirections of `cmd` to the current process.
 *
 * Returns 0 on success and -1 on failure (a diagnostic is printed on
 * stderr).  On failure the already applied redirections are undone.
 */
int  redirection_apply(const command_t *cmd, redirect_save_t *save);

/* Restore the descriptors saved by redirection_apply(). */
void redirection_restore(const redirect_save_t *save);

#endif /* MINISHELL_REDIRECTION_H */
