#!/usr/bin/env bash
#
# testlib.sh - Shared helpers for the minishell integration test scripts.
#
# Every test script sources this file, sets MINISHELL (the path to the
# executable under test) and calls `finish` at the end.  The exit status of
# the script is 0 when every assertion passed and 1 otherwise.
#
# The helper deliberately avoids depending on optional programs.  Tests
# that need a program which may be missing are skipped with `skip_test`
# instead of failing.

: "${MINISHELL:?MINISHELL must point at the executable under test}"

# Timeout (seconds) used for every invocation of the shell.  A hanging
# shell must fail the test instead of blocking the whole suite.
TEST_TIMEOUT="${TEST_TIMEOUT:-15}"

TESTS_TOTAL=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# ---------------------------------------------------------------------
# Low level helpers
# ---------------------------------------------------------------------

have() {
    command -v "$1" >/dev/null 2>&1
}

# Run the shell with `input` on its standard input and echo the combined
# standard output and standard error.  The exit status of the shell is
# lost; use run_shell_status for that.
run_shell() {
    printf '%s' "$1" | timeout "$TEST_TIMEOUT" "$MINISHELL" 2>&1
    return 0
}

# Run the shell with `input` and echo only its exit status.
run_shell_status() {
    local st
    printf '%s' "$1" | timeout "$TEST_TIMEOUT" "$MINISHELL" >/dev/null 2>&1
    st=$?
    printf '%d' "$st"
}

# Run the shell with `input` and echo only what the shell wrote to stdout.
run_shell_stdout() {
    printf '%s' "$1" | timeout "$TEST_TIMEOUT" "$MINISHELL" 2>/dev/null
    return 0
}

# Run the shell with `input` and echo only what the shell wrote to stderr.
run_shell_stderr() {
    printf '%s' "$1" | timeout "$TEST_TIMEOUT" "$MINISHELL" 2>&1 >/dev/null
    return 0
}

# ---------------------------------------------------------------------
# Assertions
# ---------------------------------------------------------------------

assert_eq() {
    # assert_eq <description> <expected> <actual>
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    if [ "$2" = "$3" ]; then
        printf '  [PASS] %s\n' "$1"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        printf '  [FAIL] %s\n         expected: [%s]\n         actual:   [%s]\n' \
               "$1" "$2" "$3"
    fi
}

assert_ne() {
    # assert_ne <description> <not-expected> <actual>
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    if [ "$2" != "$3" ]; then
        printf '  [PASS] %s\n' "$1"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        printf '  [FAIL] %s\n         value must differ from: [%s]\n' "$1" "$2"
    fi
}

assert_contains() {
    # assert_contains <description> <haystack> <needle>
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    case "$2" in
        *"$3"*)
            printf '  [PASS] %s\n' "$1"
            ;;
        *)
            TESTS_FAILED=$((TESTS_FAILED + 1))
            printf '  [FAIL] %s\n         expected to contain: [%s]\n         actual: [%s]\n' \
                   "$1" "$3" "$2"
            ;;
    esac
}

assert_not_contains() {
    # assert_not_contains <description> <haystack> <needle>
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    case "$2" in
        *"$3"*)
            TESTS_FAILED=$((TESTS_FAILED + 1))
            printf '  [FAIL] %s\n         expected NOT to contain: [%s]\n         actual: [%s]\n' \
                   "$1" "$3" "$2"
            ;;
        *)
            printf '  [PASS] %s\n' "$1"
            ;;
    esac
}

assert_file_exists() {
    # assert_file_exists <description> <path>
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    if [ -e "$2" ]; then
        printf '  [PASS] %s\n' "$1"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        printf '  [FAIL] %s\n         file does not exist: %s\n' "$1" "$2"
    fi
}

assert_file_absent() {
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    if [ ! -e "$2" ]; then
        printf '  [PASS] %s\n' "$1"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        printf '  [FAIL] %s\n         file must not exist: %s\n' "$1" "$2"
    fi
}

assert_file_content() {
    # assert_file_content <description> <path> <expected-content>
    local actual
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    if [ ! -e "$2" ]; then
        TESTS_FAILED=$((TESTS_FAILED + 1))
        printf '  [FAIL] %s\n         file does not exist: %s\n' "$1" "$2"
        return
    fi
    actual=$(cat "$2")
    if [ "$actual" = "$3" ]; then
        printf '  [PASS] %s\n' "$1"
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        printf '  [FAIL] %s\n         expected: [%s]\n         actual:   [%s]\n' \
               "$1" "$3" "$actual"
    fi
}

skip_test() {
    # skip_test <description> <reason>
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
    printf '  [SKIP] %s (%s)\n' "$1" "$2"
}

finish() {
    echo
    if [ "$TESTS_FAILED" -eq 0 ]; then
        printf '%s: all %d assertions passed' "$(basename "$0")" "$TESTS_TOTAL"
        if [ "$TESTS_SKIPPED" -gt 0 ]; then
            printf ' (%d skipped)' "$TESTS_SKIPPED"
        fi
        printf '\n'
        exit 0
    fi
    printf '%s: %d of %d assertions FAILED' "$(basename "$0")" \
           "$TESTS_FAILED" "$TESTS_TOTAL"
    if [ "$TESTS_SKIPPED" -gt 0 ]; then
        printf ' (%d skipped)' "$TESTS_SKIPPED"
    fi
    printf '\n'
    exit 1
}
