/*
 * main.c - Entry point of the mini Unix shell.
 */
#include "shell.h"
#include "signals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *out, const char *argv0)
{
    fprintf(out, "Usage: %s [options]\n", argv0);
    fprintf(out, "\n");
    fprintf(out, "  -h, --help     show this help text and exit\n");
    fprintf(out, "  -v, --version  show the version and exit\n");
    fprintf(out, "\n");
    fprintf(out, "Run without options to start an interactive shell.\n");
}

int main(int argc, char **argv)
{
    shell_state_t state;
    int status;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("%s %s\n", MINISHELL_NAME, MINISHELL_VERSION);
            return 0;
        }
        fprintf(stderr, "%s: unknown option: %s\n", MINISHELL_NAME, argv[i]);
        print_usage(stderr, argv[0]);
        return 2;
    }

    shell_state_init(&state);

    if (signals_install() != 0) {
        fprintf(stderr, "%s: unable to install signal handlers\n",
                MINISHELL_NAME);
        shell_state_destroy(&state);
        return 1;
    }

    status = shell_run(&state);

    shell_state_destroy(&state);
    return status;
}
