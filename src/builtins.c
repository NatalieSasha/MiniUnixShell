/*
 * builtins.c - Implementation of the shell's built-in commands.
 *
 * Implemented built-ins: help, pwd, cd, exit.
 *
 * Exit status conventions used throughout the shell:
 *
 *   0        success
 *   1        general failure (I/O error, invalid argument, ...)
 *   2        misuse of a built-in (wrong number of arguments)
 *   126     the command was found but could not be executed
 *   127     the command was not found
 *   128+n    the command was terminated by signal n
 */
#include "builtins.h"
#include "shell.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUILTIN_OK      0
#define BUILTIN_ERROR   1
#define BUILTIN_USAGE   2

static const char *const builtin_names[] = {
    "help", "pwd", "cd", "exit", NULL
};

int builtins_is_builtin(const char *name)
{
    size_t i;

    if (name == NULL)
        return 0;
    for (i = 0; builtin_names[i] != NULL; i++) {
        if (strcmp(name, builtin_names[i]) == 0)
            return 1;
    }
    return 0;
}

void builtins_print_help(void)
{
    printf("%s %s - a small Unix-like shell\n", MINISHELL_NAME,
           MINISHELL_VERSION);
    printf("\n");
    printf("Built-in commands:\n");
    printf("  help              show this help text\n");
    printf("  pwd               print the current working directory\n");
    printf("  cd [directory]    change the current working directory\n");
    printf("                    (without an argument the shell changes to $HOME)\n");
    printf("  exit [status]     leave the shell; status is an integer 0-255\n");
    printf("\n");
    printf("Command syntax:\n");
    printf("  cmd args...            run an external program\n");
    printf("  cmd | cmd | cmd        connect commands with a pipe\n");
    printf("  cmd < file             read standard input from file\n");
    printf("  cmd > file             write standard output to file (truncate)\n");
    printf("  cmd >> file            append standard output to file\n");
    printf("  cmd &                  run the command in the background\n");
    printf("  'text' \"text\" \\c       quoting and escaping\n");
    printf("\n");
    printf("Not supported: variable expansion, command substitution, globbing,\n");
    printf("here-documents, command separators (';') and job control.\n");
}

/* ------------------------------------------------------------------ */
/* pwd                                                                */
/* ------------------------------------------------------------------ */

static int builtin_pwd(char *const argv[], int argc)
{
    size_t size = 256;
    char *buf;

    (void)argv;

    if (argc != 1) {
        fprintf(stderr, "pwd: too many arguments\n");
        return BUILTIN_USAGE;
    }

    for (;;) {
        errno = 0;
        buf = malloc(size);
        if (buf == NULL) {
            fprintf(stderr, "pwd: out of memory\n");
            return BUILTIN_ERROR;
        }
        if (getcwd(buf, size) != NULL) {
            printf("%s\n", buf);
            free(buf);
            return BUILTIN_OK;
        }
        free(buf);
        if (errno != ERANGE) {
            fprintf(stderr, "pwd: %s\n", strerror(errno));
            return BUILTIN_ERROR;
        }
        if (size > (size_t)1 << 20) {
            fprintf(stderr, "pwd: path is too long\n");
            return BUILTIN_ERROR;
        }
        size *= 2;
    }
}

/* ------------------------------------------------------------------ */
/* cd                                                                 */
/* ------------------------------------------------------------------ */

static int builtin_cd(char *const argv[], int argc)
{
    const char *target;

    if (argc > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return BUILTIN_USAGE;
    }

    if (argc == 2) {
        target = argv[1];
    } else {
        target = getenv("HOME");
        if (target == NULL || target[0] == '\0') {
            fprintf(stderr, "cd: HOME is not set\n");
            return BUILTIN_ERROR;
        }
    }

    if (chdir(target) != 0) {
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
        return BUILTIN_ERROR;
    }
    return BUILTIN_OK;
}

/* ------------------------------------------------------------------ */
/* exit                                                               */
/* ------------------------------------------------------------------ */

/*
 * `exit` does not terminate the process directly.  It records the request
 * in the shell state so that the interpreter loop can shut down cleanly
 * (reaping background children, freeing memory).  When the command runs
 * inside a subshell (pipeline stage or background job) the state belongs to
 * the child process and the child exits immediately afterwards.
 */
static int builtin_exit(char *const argv[], int argc, shell_state_t *state)
{
    long value;
    char *end = NULL;

    if (argc > 2) {
        fprintf(stderr, "exit: too many arguments\n");
        return BUILTIN_USAGE;
    }

    if (argc == 1) {
        value = 0;
    } else {
        errno = 0;
        value = strtol(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0') {
            fprintf(stderr, "exit: %s: numeric argument required\n", argv[1]);
            return BUILTIN_USAGE;
        }
        if (value < 0 || value > 255) {
            fprintf(stderr, "exit: %s: argument must be in the range 0-255\n",
                    argv[1]);
            return BUILTIN_USAGE;
        }
    }

    state->exit_requested = 1;
    state->exit_status = (int)value;
    return BUILTIN_OK;
}

/* ------------------------------------------------------------------ */

int builtins_execute(char *const argv[], int argc, shell_state_t *state)
{
    if (argv == NULL || argv[0] == NULL)
        return BUILTIN_ERROR;

    if (strcmp(argv[0], "help") == 0) {
        if (argc != 1) {
            fprintf(stderr, "help: too many arguments\n");
            return BUILTIN_USAGE;
        }
        builtins_print_help();
        return BUILTIN_OK;
    }
    if (strcmp(argv[0], "pwd") == 0)
        return builtin_pwd(argv, argc);
    if (strcmp(argv[0], "cd") == 0)
        return builtin_cd(argv, argc);
    if (strcmp(argv[0], "exit") == 0)
        return builtin_exit(argv, argc, state);

    fprintf(stderr, "%s: %s: not a built-in command\n", MINISHELL_NAME,
            argv[0]);
    return BUILTIN_ERROR;
}
