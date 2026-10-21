/*
 * pipeline.h - Construction of command pipelines.
 */
#ifndef MINISHELL_PIPELINE_H
#define MINISHELL_PIPELINE_H

#include "parser.h"
#include "shell.h"

/*
 * Execute a job that contains one or more commands connected by pipes.
 *
 * The function is also used for single commands that are not built-ins; in
 * that case the "pipeline" consists of exactly one stage and no pipe is
 * created, which degenerates into the classic
 * fork(2) -> execvp(3) -> waitpid(2) sequence.
 *
 * For a background job the function returns immediately after the fork;
 * the caller is responsible for registering the job.
 *
 * Returns the exit status of the job (the status of the last stage).
 */
int pipeline_run(const job_t *job, shell_state_t *state);

#endif /* MINISHELL_PIPELINE_H */
