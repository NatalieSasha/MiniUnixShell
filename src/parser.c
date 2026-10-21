/*
 * parser.c - Lexer and parser for the supported shell subset.
 *
 * The implementation is deliberately split in two phases:
 *
 *   1. tokenize()  - turns the raw line into a flat token vector.  Word
 *                    boundaries, quoting and escaping are resolved here.
 *   2. build_job() - groups the tokens into commands, pipelines and jobs.
 *
 * Keeping the phases separate means the lexer is easy to reason about and
 * the grammar in docs/parser_grammar.md maps one to one onto the code.
 */
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Growable byte buffer                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} strbuf_t;

static void sb_init(strbuf_t *sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_free(strbuf_t *sb)
{
    free(sb->data);
    sb_init(sb);
}

static int sb_reserve(strbuf_t *sb, size_t extra)
{
    size_t need;
    char *tmp;

    if (extra > (size_t)-1 - sb->len - 1)
        return -1;
    need = sb->len + extra + 1;
    if (need <= sb->cap)
        return 0;

    if (sb->cap == 0)
        sb->cap = 64;
    while (sb->cap < need) {
        if (sb->cap > ((size_t)-1) / 2)
            return -1;
        sb->cap *= 2;
    }
    tmp = realloc(sb->data, sb->cap);
    if (tmp == NULL)
        return -1;
    sb->data = tmp;
    return 0;
}

static int sb_append(strbuf_t *sb, char c)
{
    if (sb_reserve(sb, 1) != 0)
        return -1;
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 0;
}

static int sb_append_n(strbuf_t *sb, const char *s, size_t n)
{
    if (sb_reserve(sb, n) != 0)
        return -1;
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tokens                                                             */
/* ------------------------------------------------------------------ */

typedef enum {
    TOK_WORD,   /* a fully de-quoted word                             */
    TOK_PIPE,   /* `|`                                                */
    TOK_LT,     /* `<`                                                */
    TOK_GT,     /* `>`                                                */
    TOK_APPEND, /* `>>`                                               */
    TOK_AMP,    /* `&`                                                */
    TOK_END     /* end of input                                       */
} token_kind_t;

typedef struct {
    token_kind_t kind;
    char        *text; /* owned, only meaningful for TOK_WORD */
} token_t;

typedef struct {
    token_t *items;
    size_t   count;
    size_t   capacity;
} token_list_t;

static void token_list_init(token_list_t *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void token_list_free(token_list_t *list)
{
    size_t i;

    for (i = 0; i < list->count; i++)
        free(list->items[i].text);
    free(list->items);
    token_list_init(list);
}

static int token_list_push(token_list_t *list, token_kind_t kind, char *text)
{
    if (list->count == list->capacity) {
        size_t newcap = list->capacity ? list->capacity * 2 : 8;
        token_t *tmp = realloc(list->items, newcap * sizeof(*tmp));
        if (tmp == NULL)
            return -1;
        list->items = tmp;
        list->capacity = newcap;
    }
    list->items[list->count].kind = kind;
    list->items[list->count].text = text;
    list->count++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Character classes                                                  */
/* ------------------------------------------------------------------ */

static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

/*
 * Characters that terminate an unquoted word.  Everything else that has a
 * special meaning in a POSIX shell is detected explicitly so that we can
 * report a precise diagnostic instead of silently doing something else.
 */
static int is_word_terminator(char c)
{
    switch (c) {
    case '|': case '&': case '<': case '>':
    case ';': case '`': case '$': case '(': case ')':
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Quoted section readers                                             */
/* ------------------------------------------------------------------ */

/*
 * Read a single quoted section starting at *p (which points at the opening
 * quote).  On success returns a pointer just past the closing quote and
 * appends the literal text to `buf`.  On failure returns NULL.
 */
static const char *read_single_quoted(const char *p, strbuf_t *buf, int *err)
{
    p++; /* skip the opening quote */
    for (;;) {
        if (*p == '\0')
            return NULL; /* unterminated */
        if (*p == '\'') {
            p++;
            break;
        }
        if (sb_append(buf, *p++) != 0) {
            *err = 1;
            return NULL;
        }
    }
    return p;
}

/*
 * Read a double quoted section starting at *p (which points at the opening
 * quote).  Inside double quotes a backslash only escapes `"`, `\` and `$`
 * (and a backtick); for any other character the backslash is kept verbatim,
 * which matches POSIX rule 2 of section 2.2.
 */
static const char *read_double_quoted(const char *p, strbuf_t *buf, int *err)
{
    p++; /* skip the opening quote */
    for (;;) {
        if (*p == '\0')
            return NULL; /* unterminated */
        if (*p == '"') {
            p++;
            break;
        }
        if (*p == '\\') {
            char next = p[1];
            if (next == '"' || next == '\\') {
                if (sb_append(buf, next) != 0)
                    return NULL;
                p += 2;
                continue;
            }
            if (next == '$' || next == '`') {
                *err = 1; /* expansion is not supported */
                return NULL;
            }
            /* Not a special backslash sequence: keep both characters. */
            if (sb_append(buf, '\\') != 0)
                return NULL;
            p++;
            continue;
        }
        if (*p == '$' || *p == '`') {
            *err = 1; /* expansion is not supported */
            return NULL;
        }
        if (sb_append(buf, *p++) != 0)
            return NULL;
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Lexer                                                              */
/* ------------------------------------------------------------------ */

/*
 * Read one word starting at *p.  Quoted and escaped sections are folded
 * into the word; the returned pointer is the first character after the
 * word.
 */
static const char *read_word(const char *p, strbuf_t *buf, parse_status_t *status)
{
    for (;;) {
        if (*p == '\0')
            break;
        if (is_blank(*p))
            break;
        if (is_word_terminator(*p))
            break;

        if (*p == '\'') {
            int err = 0;
            const char *next = read_single_quoted(p, buf, &err);
            if (next == NULL) {
                *status = err ? PARSE_ERR_OUT_OF_MEMORY : PARSE_ERR_UNCLOSED_QUOTE;
                return NULL;
            }
            p = next;
            continue;
        }
        if (*p == '"') {
            int err = 0;
            const char *next = read_double_quoted(p, buf, &err);
            if (next == NULL) {
                *status = err ? PARSE_ERR_UNSUPPORTED : PARSE_ERR_UNCLOSED_QUOTE;
                return NULL;
            }
            p = next;
            continue;
        }
        if (*p == '\\') {
            p++;
            if (*p == '\0') {
                /* Trailing backslash: POSIX leaves this undefined.  We keep
                 * the backslash as a literal character. */
                if (sb_append(buf, '\\') != 0) {
                    *status = PARSE_ERR_OUT_OF_MEMORY;
                    return NULL;
                }
                break;
            }
            if (sb_append(buf, *p++) != 0) {
                *status = PARSE_ERR_OUT_OF_MEMORY;
                return NULL;
            }
            continue;
        }
        if (*p == '*' || *p == '?' || *p == '[') {
            /* Pattern matching is deliberately not implemented. */
            *status = PARSE_ERR_UNSUPPORTED;
            return NULL;
        }
        if (sb_append(buf, *p++) != 0) {
            *status = PARSE_ERR_OUT_OF_MEMORY;
            return NULL;
        }
    }
    return p;
}

static parse_status_t tokenize(const char *line, token_list_t *out)
{
    const char *p = line;
    size_t length;

    token_list_init(out);

    if (line == NULL)
        return PARSE_ERR_EMPTY_INPUT;

    length = strlen(line);
    if (length > PARSER_MAX_LINE_LENGTH)
        return PARSE_ERR_TOO_LONG;

    for (;;) {
        strbuf_t buf;
        const char *next;
        parse_status_t status = PARSE_OK;

        while (*p != '\0' && is_blank(*p))
            p++;
        if (*p == '\0')
            break;

        switch (*p) {
        case '|':
            if (token_list_push(out, TOK_PIPE, NULL) != 0)
                goto oom;
            p++;
            continue;
        case '&':
            if (token_list_push(out, TOK_AMP, NULL) != 0)
                goto oom;
            p++;
            continue;
        case '<':
            if (p[1] == '<') { /* here-document */
                token_list_free(out);
                return PARSE_ERR_UNSUPPORTED;
            }
            if (token_list_push(out, TOK_LT, NULL) != 0)
                goto oom;
            p++;
            continue;
        case '>':
            if (p[1] == '>') {
                if (token_list_push(out, TOK_APPEND, NULL) != 0)
                    goto oom;
                p += 2;
            } else {
                if (token_list_push(out, TOK_GT, NULL) != 0)
                    goto oom;
                p++;
            }
            continue;
        case ';':
        case '`':
        case '$':
        case '(':
        case ')':
            token_list_free(out);
            return PARSE_ERR_UNSUPPORTED;
        default:
            break;
        }

        sb_init(&buf);
        next = read_word(p, &buf, &status);
        if (next == NULL) {
            sb_free(&buf);
            token_list_free(out);
            return status;
        }
        p = next;

        /* An explicitly empty word ("" or '') must still produce a token
         * with an empty string, so make sure the buffer owns a valid C
         * string even when nothing was appended. */
        if (buf.data == NULL) {
            buf.data = strdup("");
            if (buf.data == NULL) {
                sb_free(&buf);
                token_list_free(out);
                return PARSE_ERR_OUT_OF_MEMORY;
            }
        }
        if (token_list_push(out, TOK_WORD, buf.data) != 0) {
            sb_free(&buf);
            goto oom;
        }
        /* Ownership of the buffer has been transferred to the token list;
         * detach it so that sb_free() does not release it a second time. */
        buf.data = NULL;
        buf.len = 0;
        buf.cap = 0;
        sb_free(&buf);
    }

    if (token_list_push(out, TOK_END, NULL) != 0)
        goto oom;

    return PARSE_OK;

oom:
    token_list_free(out);
    return PARSE_ERR_OUT_OF_MEMORY;
}

/* ------------------------------------------------------------------ */
/* Parser                                                             */
/* ------------------------------------------------------------------ */

void job_init(job_t *job)
{
    job->commands = NULL;
    job->count = 0;
    job->background = 0;
}

static void command_free(command_t *cmd)
{
    size_t i;

    if (cmd->argv != NULL) {
        for (i = 0; i < cmd->argc; i++)
            free(cmd->argv[i]);
        free(cmd->argv);
    }
    free(cmd->input_file);
    free(cmd->output_file);
    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->output_append = 0;
}

void job_free(job_t *job)
{
    size_t i;

    if (job == NULL)
        return;
    for (i = 0; i < job->count; i++)
        command_free(&job->commands[i]);
    free(job->commands);
    job_init(job);
}

static int command_push_word(command_t *cmd, const char *word)
{
    char **tmp;

    tmp = realloc(cmd->argv, (cmd->argc + 2) * sizeof(*tmp));
    if (tmp == NULL)
        return -1;
    cmd->argv = tmp;
    cmd->argv[cmd->argc] = strdup(word);
    if (cmd->argv[cmd->argc] == NULL)
        return -1;
    cmd->argc++;
    cmd->argv[cmd->argc] = NULL;
    return 0;
}

static int job_push_command(job_t *job, const command_t *cmd)
{
    command_t *tmp;

    tmp = realloc(job->commands, (job->count + 1) * sizeof(*tmp));
    if (tmp == NULL)
        return -1;
    job->commands = tmp;
    job->commands[job->count] = *cmd;
    job->count++;
    return 0;
}

/*
 * Parse a single command: a sequence of words and redirections in any
 * order.  Every redirection target must be a word; the last redirection of
 * a given kind wins, exactly like a POSIX shell.
 */
static parse_status_t parse_command(const token_list_t *tokens, size_t *index,
                                    command_t *out_cmd)
{
    command_t cmd;
    size_t i = *index;

    cmd.argv = NULL;
    cmd.argc = 0;
    cmd.input_file = NULL;
    cmd.output_file = NULL;
    cmd.output_append = 0;

    for (;;) {
        const token_t *tok;

        if (i >= tokens->count)
            break;
        tok = &tokens->items[i];

        if (tok->kind == TOK_WORD) {
            if (cmd.argc >= PARSER_MAX_WORDS) {
                command_free(&cmd);
                return PARSE_ERR_TOO_MANY_ARGS;
            }
            if (command_push_word(&cmd, tok->text) != 0) {
                command_free(&cmd);
                return PARSE_ERR_OUT_OF_MEMORY;
            }
            i++;
            continue;
        }

        if (tok->kind == TOK_LT || tok->kind == TOK_GT || tok->kind == TOK_APPEND) {
            token_kind_t op = tok->kind;
            char *target;

            i++;
            if (i >= tokens->count || tokens->items[i].kind != TOK_WORD) {
                command_free(&cmd);
                return PARSE_ERR_MISSING_REDIRECT;
            }
            target = strdup(tokens->items[i].text);
            if (target == NULL) {
                command_free(&cmd);
                return PARSE_ERR_OUT_OF_MEMORY;
            }
            i++;

            if (op == TOK_LT) {
                free(cmd.input_file);
                cmd.input_file = target;
            } else {
                free(cmd.output_file);
                cmd.output_file = target;
                cmd.output_append = (op == TOK_APPEND);
            }
            continue;
        }

        break;
    }

    if (cmd.argc == 0) {
        command_free(&cmd);
        return PARSE_ERR_EMPTY_COMMAND;
    }

    *index = i;
    *out_cmd = cmd;
    return PARSE_OK;
}

static parse_status_t build_job(const token_list_t *tokens, job_t *job)
{
    size_t i = 0;
    parse_status_t status;

    job_init(job);

    for (;;) {
        command_t cmd;

        status = parse_command(tokens, &i, &cmd);
        if (status != PARSE_OK) {
            job_free(job);
            return status;
        }

        if (job_push_command(job, &cmd) != 0) {
            command_free(&cmd);
            return PARSE_ERR_OUT_OF_MEMORY;
        }

        if (i < tokens->count && tokens->items[i].kind == TOK_PIPE) {
            i++;
            continue;
        }
        break;
    }

    if (i < tokens->count && tokens->items[i].kind == TOK_AMP) {
        job->background = 1;
        i++;
    }

    if (i < tokens->count && tokens->items[i].kind != TOK_END) {
        job_free(job);
        return PARSE_ERR_UNEXPECTED_TOKEN;
    }

    return PARSE_OK;
}

parse_status_t parser_parse(const char *line, job_t *out_job)
{
    token_list_t tokens;
    parse_status_t status;

    job_init(out_job);

    if (line == NULL)
        return PARSE_ERR_EMPTY_INPUT;

    status = tokenize(line, &tokens);
    if (status != PARSE_OK) {
        token_list_free(&tokens);
        return status;
    }

    status = build_job(&tokens, out_job);
    token_list_free(&tokens);
    return status;
}

const char *parser_status_message(parse_status_t status)
{
    switch (status) {
    case PARSE_OK:
        return "ok";
    case PARSE_ERR_EMPTY_INPUT:
        return "empty command";
    case PARSE_ERR_UNCLOSED_QUOTE:
        return "unterminated quoted string";
    case PARSE_ERR_UNEXPECTED_TOKEN:
        return "unexpected token";
    case PARSE_ERR_EMPTY_COMMAND:
        return "empty command in pipeline";
    case PARSE_ERR_MISSING_REDIRECT:
        return "redirection operator is missing a file name";
    case PARSE_ERR_TOO_LONG:
        return "command line is too long";
    case PARSE_ERR_TOO_MANY_ARGS:
        return "too many arguments";
    case PARSE_ERR_UNSUPPORTED:
        return "unsupported shell syntax "
               "(no variable expansion, command substitution, globbing, "
               "here-documents or command separators)";
    case PARSE_ERR_OUT_OF_MEMORY:
        return "out of memory";
    default:
        return "unknown parse error";
    }
}

char *job_describe(const job_t *job)
{
    strbuf_t buf;
    size_t i, j;

    sb_init(&buf);
    for (i = 0; i < job->count; i++) {
        const command_t *cmd = &job->commands[i];
        if (i > 0)
            sb_append_n(&buf, " | ", 3);
        for (j = 0; j < cmd->argc; j++) {
            if (j > 0)
                sb_append(&buf, ' ');
            sb_append_n(&buf, cmd->argv[j], strlen(cmd->argv[j]));
        }
        if (cmd->input_file != NULL) {
            sb_append_n(&buf, " < ", 3);
            sb_append_n(&buf, cmd->input_file, strlen(cmd->input_file));
        }
        if (cmd->output_file != NULL) {
            sb_append_n(&buf, cmd->output_append ? " >> " : " > ", 4);
            sb_append_n(&buf, cmd->output_file, strlen(cmd->output_file));
        }
    }
    if (job->background)
        sb_append_n(&buf, " &", 2);

    if (buf.data == NULL)
        return strdup("");
    return buf.data;
}
