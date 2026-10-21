/*
 * redirection.c - Implementation of the `<`, `>` and `>>` operators.
 *
 * The sequence for every redirection is:
 *
 *   open(2)  ->  dup(2) (save the old descriptor)  ->  dup2(2) (install)
 *             ->  close(2) (release the temporary descriptor)
 *
 * The dup()/dup2()/close() combination is used instead of simply closing
 * descriptor 0 or 1 first because closing the descriptor would make the
 * shell lose its standard input/output if the dup2() failed.
 */
#include "redirection.h"
#include "shell.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void redirect_save_init(redirect_save_t *save)
{
    save->saved_stdin = -1;
    save->saved_stdout = -1;
}

void redirection_restore(const redirect_save_t *save)
{
    if (save == NULL)
        return;
    if (save->saved_stdin >= 0) {
        if (dup2(save->saved_stdin, STDIN_FILENO) < 0)
            fprintf(stderr, "%s: dup2(stdin): %s\n", MINISHELL_NAME,
                    strerror(errno));
        close(save->saved_stdin);
    }
    if (save->saved_stdout >= 0) {
        if (dup2(save->saved_stdout, STDOUT_FILENO) < 0)
            fprintf(stderr, "%s: dup2(stdout): %s\n", MINISHELL_NAME,
                    strerror(errno));
        close(save->saved_stdout);
    }
}

int redirection_apply(const command_t *cmd, redirect_save_t *save)
{
    int fd;

    if (save == NULL)
        return -1;
    redirect_save_init(save);

    if (cmd->input_file != NULL) {
        fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "%s: %s: %s\n", MINISHELL_NAME, cmd->input_file,
                    strerror(errno));
            return -1;
        }
        if (fd != STDIN_FILENO) {
            save->saved_stdin = dup(STDIN_FILENO);
            if (save->saved_stdin < 0) {
                fprintf(stderr, "%s: dup: %s\n", MINISHELL_NAME,
                        strerror(errno));
                close(fd);
                return -1;
            }
            if (dup2(fd, STDIN_FILENO) < 0) {
                fprintf(stderr, "%s: dup2: %s\n", MINISHELL_NAME,
                        strerror(errno));
                close(fd);
                return -1;
            }
            close(fd);
        }
    }

    if (cmd->output_file != NULL) {
        int flags = O_WRONLY | O_CREAT;
        flags |= cmd->output_append ? O_APPEND : O_TRUNC;

        fd = open(cmd->output_file, flags, REDIRECTION_CREATE_MODE);
        if (fd < 0) {
            fprintf(stderr, "%s: %s: %s\n", MINISHELL_NAME, cmd->output_file,
                    strerror(errno));
            return -1;
        }
        if (fd != STDOUT_FILENO) {
            save->saved_stdout = dup(STDOUT_FILENO);
            if (save->saved_stdout < 0) {
                fprintf(stderr, "%s: dup: %s\n", MINISHELL_NAME,
                        strerror(errno));
                close(fd);
                return -1;
            }
            if (dup2(fd, STDOUT_FILENO) < 0) {
                fprintf(stderr, "%s: dup2: %s\n", MINISHELL_NAME,
                        strerror(errno));
                close(fd);
                return -1;
            }
            close(fd);
        }
    }

    return 0;
}
