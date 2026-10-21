/*
 * parser.h - Lexical analysis and command line parsing.
 *
 * The parser turns a raw input line into a `job_t`: an ordered list of
 * `command_t` structures connected by pipes, optionally running in the
 * background.
 *
 * The grammar implemented here is documented in docs/parser_grammar.md.
 */
#ifndef MINISHELL_PARSER_H
#define MINISHELL_PARSER_H

#include <stddef.h>

/* Maximum number of words (arguments) in a single command. */
#define PARSER_MAX_WORDS 1024

/* Maximum length of a command line accepted by the parser. */
#define PARSER_MAX_LINE_LENGTH 65536

typedef enum {
    PARSE_OK = 0,
    PARSE_ERR_EMPTY_INPUT,        /* line contained only whitespace       */
    PARSE_ERR_UNCLOSED_QUOTE,     /* ' or " was never closed              */
    PARSE_ERR_UNEXPECTED_TOKEN,   /* stray token, e.g. `a & b`            */
    PARSE_ERR_EMPTY_COMMAND,      /* a pipeline stage has no words        */
    PARSE_ERR_MISSING_REDIRECT,   /* `>` or `<` without a target          */
    PARSE_ERR_TOO_LONG,           /* line exceeds PARSER_MAX_LINE_LENGTH  */
    PARSE_ERR_TOO_MANY_ARGS,      /* command exceeds PARSER_MAX_WORDS     */
    PARSE_ERR_UNSUPPORTED,        /* a shell feature we deliberately omit */
    PARSE_ERR_OUT_OF_MEMORY
} parse_status_t;

/*
 * A single command: a word vector plus its redirections.
 *
 * Ownership: `argv` is a NULL terminated, heap allocated vector whose
 * elements are heap allocated strings; `input_file` / `output_file` are
 * individually allocated strings or NULL.  All of them are released by
 * `command_free()`.
 */
typedef struct {
    char **argv;         /* NULL terminated argument vector              */
    size_t argc;         /* number of words (excluding the NULL)         */
    char  *input_file;   /* source of `<` redirection, or NULL            */
    char  *output_file;  /* target of `>` / `>>` redirection, or NULL     */
    int    output_append;/* 1 when the target was introduced by `>>`      */
} command_t;

/*
 * A job: one or more commands joined by pipes, optionally in background.
 */
typedef struct {
    command_t *commands;
    size_t     count;
    int        background;   /* 1 when the job was introduced by `&` */
} job_t;

void          job_init(job_t *job);
void          job_free(job_t *job);
parse_status_t parser_parse(const char *line, job_t *out_job);

/* Human readable, stable description of a parse failure. */
const char   *parser_status_message(parse_status_t status);

/*
 * Build a human readable description of a job, e.g. "cat f | grep x".
 * The returned string is heap allocated and must be freed by the caller.
 */
char         *job_describe(const job_t *job);

#endif /* MINISHELL_PARSER_H */
