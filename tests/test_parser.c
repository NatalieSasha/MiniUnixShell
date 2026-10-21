/*
 * test_parser.c - Unit tests for the lexer and the command parser.
 *
 * The tests only use the public interface of parser.h, so they exercise
 * exactly the same code path as the shell itself.
 */
#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

static void pass(const char *name)
{
    tests_run++;
    printf("  [PASS] %s\n", name);
}

static void fail(const char *name, const char *fmt, ...)
{
    va_list ap;

    tests_run++;
    tests_failed++;
    printf("  [FAIL] %s\n         ", name);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

#define ASSERT_TRUE(cond, name)          \
    do {                                 \
        if (cond)                        \
            pass(name);                  \
        else                             \
            fail(name, "condition is false"); \
    } while (0)

#define ASSERT_STATUS(expected, status, name)                            \
    do {                                                                \
        parse_status_t st_ = (status);                                  \
        if (st_ == (expected))                                          \
            pass(name);                                                 \
        else                                                            \
            fail(name, "expected status %d, got %d (%s)",               \
                 (int)(expected), (int)st_,                              \
                 parser_status_message(st_));                            \
    } while (0)

/* Compare the argument vector of a command with an expected list. */
static int argv_equals(const command_t *cmd, const char *const *expected,
                       size_t count)
{
    size_t i;

    if (cmd->argc != count)
        return 0;
    for (i = 0; i < count; i++) {
        if (cmd->argv[i] == NULL || expected[i] == NULL)
            return 0;
        if (strcmp(cmd->argv[i], expected[i]) != 0)
            return 0;
    }
    return cmd->argv[count] == NULL; /* NULL terminated */
}

/* ------------------------------------------------------------------ */

static void test_simple_command(void)
{
    job_t job;
    const char *const expected[] = { "echo", "Hello", "World" };
    parse_status_t st = parser_parse("echo Hello World", &job);

    ASSERT_STATUS(PARSE_OK, st, "parse: simple command succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(job.count == 1, "parse: simple command has one stage");
        ASSERT_TRUE(argv_equals(&job.commands[0], expected, 3),
                    "parse: simple command splits into three words");
        ASSERT_TRUE(job.background == 0, "parse: simple command is foreground");
        job_free(&job);
    }
}

static void test_quoting(void)
{
    job_t job;
    const char *const q1[] = { "echo", "Hello World" };
    const char *const q2[] = { "echo", "Operating Systems" };
    const char *const q3[] = { "echo", "" };
    const char *const q4[] = { "echo", "ab cd" };
    const char *const q5[] = { "echo", "say \"hi\"" };
    const char *const q6[] = { "echo", "a b" };
    parse_status_t st;

    st = parser_parse("echo \"Hello World\"", &job);
    ASSERT_STATUS(PARSE_OK, st, "parse: double quoted word succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(argv_equals(&job.commands[0], q1, 2),
                    "parse: double quotes keep the space in one word");
        job_free(&job);
    }

    st = parser_parse("echo 'Operating Systems'", &job);
    ASSERT_STATUS(PARSE_OK, st, "parse: single quoted word succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(argv_equals(&job.commands[0], q2, 2),
                    "parse: single quotes keep the space in one word");
        job_free(&job);
    }

    st = parser_parse("echo \"\"", &job);
    ASSERT_STATUS(PARSE_OK, st, "parse: empty quoted word succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(argv_equals(&job.commands[0], q3, 2),
                    "parse: an empty quoted argument is preserved");
        job_free(&job);
    }

    st = parser_parse("echo a\"b c\"d", &job);
    ASSERT_STATUS(PARSE_OK, st, "parse: concatenated quoted sections succeed");
    if (st == PARSE_OK) {
        ASSERT_TRUE(argv_equals(&job.commands[0], q4, 2),
                    "parse: adjacent quoted and unquoted text form one word");
        job_free(&job);
    }

    st = parser_parse("echo \"say \\\"hi\\\"\"", &job);
    ASSERT_STATUS(PARSE_OK, st, "parse: escaped double quote succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(argv_equals(&job.commands[0], q5, 2),
                    "parse: backslash escapes a double quote inside \"...\"");
        job_free(&job);
    }

    st = parser_parse("echo a\\ b", &job);
    ASSERT_STATUS(PARSE_OK, st, "parse: escaped space succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(argv_equals(&job.commands[0], q6, 2),
                    "parse: backslash escapes a space outside quotes");
        job_free(&job);
    }}

static void test_pipeline(void)
{
    job_t job;
    const char *const s0[] = { "cat", "input.txt" };
    const char *const s1[] = { "grep", "error" };
    const char *const s2[] = { "wc", "-l" };
    parse_status_t st = parser_parse("cat input.txt | grep error | wc -l", &job);

    ASSERT_STATUS(PARSE_OK, st, "parse: three stage pipeline succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(job.count == 3, "parse: pipeline has three stages");
        ASSERT_TRUE(argv_equals(&job.commands[0], s0, 2),
                    "parse: first stage is 'cat input.txt'");
        ASSERT_TRUE(argv_equals(&job.commands[1], s1, 2),
                    "parse: second stage is 'grep error'");
        ASSERT_TRUE(argv_equals(&job.commands[2], s2, 2),
                    "parse: third stage is 'wc -l'");
        job_free(&job);
    }
}

static void test_redirection(void)
{
    job_t job;
    parse_status_t st;

    st = parser_parse("cat < input.txt", &job);
    ASSERT_STATUS(PARSE_OK, st, "parse: input redirection succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(job.commands[0].input_file != NULL &&
                    strcmp(job.commands[0].input_file, "input.txt") == 0,
                    "parse: input redirection records the file name");
        ASSERT_TRUE(job.commands[0].output_file == NULL,
                    "parse: input redirection leaves stdout alone");
        job_free(&job);
    }

    st = parser_parse("echo Hello > output.txt", &job);
    ASSERT_STATUS(PARSE_OK, st, "parse: output redirection succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(job.commands[0].output_file != NULL &&
                    strcmp(job.commands[0].output_file, "output.txt") == 0,
                    "parse: output redirection records the file name");
        ASSERT_TRUE(job.commands[0].output_append == 0,
                    "parse: '>' does not set the append flag");
        job_free(&job);
    }

    st = parser_parse("echo More >> output.txt", &job);
    ASSERT_STATUS(PARSE_OK, st, "parse: append redirection succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(job.commands[0].output_append == 1,
                    "parse: '>>' sets the append flag");
        job_free(&job);
    }

    st = parser_parse("> out.txt echo hi", &job);
    ASSERT_STATUS(PARSE_OK, st, "parse: redirection before the command succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(job.commands[0].output_file != NULL,
                    "parse: a leading redirection is attached to the command");
        job_free(&job);
    }
}

static void test_background(void)
{
    job_t job;
    parse_status_t st = parser_parse("sleep 5 &", &job);

    ASSERT_STATUS(PARSE_OK, st, "parse: background job succeeds");
    if (st == PARSE_OK) {
        ASSERT_TRUE(job.background == 1, "parse: '&' sets the background flag");
        job_free(&job);
    }
}

static void test_errors(void)
{
    job_t job;
    parse_status_t st;

    st = parser_parse("echo \"unterminated", &job);
    ASSERT_STATUS(PARSE_ERR_UNCLOSED_QUOTE, st,
                  "parse: unterminated double quote is rejected");
    job_free(&job);

    st = parser_parse("echo 'unterminated", &job);
    ASSERT_STATUS(PARSE_ERR_UNCLOSED_QUOTE, st,
                  "parse: unterminated single quote is rejected");
    job_free(&job);

    st = parser_parse("echo >", &job);
    ASSERT_STATUS(PARSE_ERR_MISSING_REDIRECT, st,
                  "parse: '>' without a file name is rejected");
    job_free(&job);

    st = parser_parse("echo <", &job);
    ASSERT_STATUS(PARSE_ERR_MISSING_REDIRECT, st,
                  "parse: '<' without a file name is rejected");
    job_free(&job);

    st = parser_parse("echo hi & echo bye", &job);
    ASSERT_STATUS(PARSE_ERR_UNEXPECTED_TOKEN, st,
                  "parse: a token after '&' is rejected");
    job_free(&job);

    st = parser_parse("echo | | wc", &job);
    ASSERT_STATUS(PARSE_ERR_EMPTY_COMMAND, st,
                  "parse: an empty pipeline stage is rejected");
    job_free(&job);

    st = parser_parse("echo $HOME", &job);
    ASSERT_STATUS(PARSE_ERR_UNSUPPORTED, st,
                  "parse: variable expansion is rejected");
    job_free(&job);

    st = parser_parse("echo `date`", &job);
    ASSERT_STATUS(PARSE_ERR_UNSUPPORTED, st,
                  "parse: command substitution is rejected");
    job_free(&job);

    st = parser_parse("ls *.c", &job);
    ASSERT_STATUS(PARSE_ERR_UNSUPPORTED, st,
                  "parse: globbing is rejected");
    job_free(&job);

    st = parser_parse("echo a; echo b", &job);
    ASSERT_STATUS(PARSE_ERR_UNSUPPORTED, st,
                  "parse: the command separator ';' is rejected");
    job_free(&job);

    st = parser_parse("cat << EOF", &job);
    ASSERT_STATUS(PARSE_ERR_UNSUPPORTED, st,
                  "parse: here-documents are rejected");
    job_free(&job);

    st = parser_parse("echo (x)", &job);
    ASSERT_STATUS(PARSE_ERR_UNSUPPORTED, st,
                  "parse: subshell syntax is rejected");
    job_free(&job);
}

static void test_limits(void)
{
    job_t job;
    char *line;
    size_t i;
    parse_status_t st;

    /* Too many arguments. */
    line = malloc(64 * 1024);
    if (line == NULL) {
        fail("parse: allocation for the argument limit test", "out of memory");
        return;
    }
    strcpy(line, "echo");
    for (i = 0; i < PARSER_MAX_WORDS + 10; i++)
        strcat(line, " a");
    st = parser_parse(line, &job);
    ASSERT_STATUS(PARSE_ERR_TOO_MANY_ARGS, st,
                  "parse: more than PARSER_MAX_WORDS arguments is rejected");
    job_free(&job);

    /* A line that is too long. */
    {
        size_t len = PARSER_MAX_LINE_LENGTH + 16;
        char *big = malloc(len + 1);
        if (big == NULL) {
            fail("parse: allocation for the length limit test", "out of memory");
        } else {
            memset(big, 'x', len);
            big[len] = '\0';
            st = parser_parse(big, &job);
            ASSERT_STATUS(PARSE_ERR_TOO_LONG, st,
                          "parse: an over-long line is rejected");
            job_free(&job);
            free(big);
        }
    }

    free(line);
}

static void test_description(void)
{
    job_t job;
    char *text;

    if (parser_parse("cat f | grep x", &job) == PARSE_OK) {
        text = job_describe(&job);
        ASSERT_TRUE(text != NULL && strcmp(text, "cat f | grep x") == 0,
                    "parse: job_describe renders the job");
        free(text);
        job_free(&job);
    }
    if (parser_parse("sleep 5 &", &job) == PARSE_OK) {
        text = job_describe(&job);
        ASSERT_TRUE(text != NULL && strcmp(text, "sleep 5 &") == 0,
                    "parse: job_describe marks a background job");
        free(text);
        job_free(&job);
    }
}

int main(void)
{
    printf("unit tests: parser\n");

    test_simple_command();
    test_quoting();
    test_pipeline();
    test_redirection();
    test_background();
    test_errors();
    test_limits();
    test_description();

    printf("\n");
    if (tests_failed == 0) {
        printf("test_parser: all %d assertions passed\n", tests_run);
        return 0;
    }
    printf("test_parser: %d of %d assertions FAILED\n", tests_failed, tests_run);
    return 1;
}
