/*
 * builtins.h - Shell commands that are implemented inside the shell process.
 *
 * A built-in runs in the shell's own address space, which is required for
 * commands that must change shell state (`cd`) or terminate it (`exit`).
 */
#ifndef MINISHELL_BUILTINS_H
#define MINISHELL_BUILTINS_H

#include "shell.h"

/* Return 1 if `name` is a built-in command, 0 otherwise. */
int builtins_is_builtin(const char *name);

/*
 * Execute a built-in command.
 *
 * `argv` is the NULL terminated argument vector of the command and `argc`
 * its length.  The return value is the exit status of the built-in.
 */
int builtins_execute(char *const argv[], int argc, shell_state_t *state);

/* Print the built-in command reference. */
void builtins_print_help(void);

#endif /* MINISHELL_BUILTINS_H */
