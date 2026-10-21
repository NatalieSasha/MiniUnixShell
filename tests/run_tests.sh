#!/usr/bin/env bash
#
# run_tests.sh - Entry point of the minishell test suite.
#
# The suite has two parts:
#
#   * unit tests      - tests/test_parser.c, compiled and executed directly
#   * integration tests - the test_*.sh scripts, which drive the real shell
#                        executable and inspect its output and its exit
#                        status
#
# The script returns 0 when every test passed and 1 otherwise, so that
# `make test` fails on a regression.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

MINISHELL="${MINISHELL:-$ROOT_DIR/minishell}"
export MINISHELL

TEST_TIMEOUT="${TEST_TIMEOUT:-20}"
export TEST_TIMEOUT

# Every integration test runs in its own temporary directory.
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

FAILED_SCRIPTS=()
PASSED_SCRIPTS=()
SKIPPED_SCRIPTS=()

banner() {
    echo
    echo "==============================================================="
    echo "$1"
    echo "==============================================================="
}

run_test_script() {
    local script="$1"
    local name
    name="$(basename "$script")"
    local dir="$WORK_DIR/${name%.sh}"
    mkdir -p "$dir"

    banner "$name"
    ( cd "$dir" && MINISHELL="$MINISHELL" TEST_TIMEOUT="$TEST_TIMEOUT" \
        timeout $((TEST_TIMEOUT * 4)) bash "$script" )
    local rc=$?

    if [ $rc -eq 0 ]; then
        PASSED_SCRIPTS+=("$name")
    elif [ $rc -eq 124 ]; then
        echo "  [FAIL] $name timed out"
        FAILED_SCRIPTS+=("$name (timeout)")
    else
        FAILED_SCRIPTS+=("$name")
    fi
}

# ---------------------------------------------------------------------
# 0. Preconditions
# ---------------------------------------------------------------------
if [ ! -x "$MINISHELL" ]; then
    echo "error: $MINISHELL is not executable; run 'make' first" >&2
    exit 1
fi

echo "minishell test suite"
echo "executable under test : $MINISHELL"
echo "per test timeout      : ${TEST_TIMEOUT}s"
echo "host                  : $(uname -srm)"

# ---------------------------------------------------------------------
# 1. Unit tests
# ---------------------------------------------------------------------
UNIT_BIN="$ROOT_DIR/build/test_parser"
if [ -x "$UNIT_BIN" ]; then
    banner "test_parser (unit tests)"
    if timeout 60 "$UNIT_BIN"; then
        PASSED_SCRIPTS+=("test_parser")
    else
        FAILED_SCRIPTS+=("test_parser")
    fi
else
    banner "test_parser (unit tests)"
    echo "  [SKIP] build/test_parser is missing; run 'make test'"
    SKIPPED_SCRIPTS+=("test_parser")
fi

# ---------------------------------------------------------------------
# 2. Integration tests
# ---------------------------------------------------------------------
for script in "$SCRIPT_DIR"/test_*.sh; do
    case "$(basename "$script")" in
        testlib.sh) continue ;;
    esac
    run_test_script "$script"
done

# ---------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------
banner "summary"
printf 'passed  : %d\n' "${#PASSED_SCRIPTS[@]}"
printf 'failed  : %d\n' "${#FAILED_SCRIPTS[@]}"
printf 'skipped : %d\n' "${#SKIPPED_SCRIPTS[@]}"

if [ "${#PASSED_SCRIPTS[@]}" -gt 0 ]; then
    printf '  ok    : %s\n' "${PASSED_SCRIPTS[@]}"
fi
if [ "${#FAILED_SCRIPTS[@]}" -gt 0 ]; then
    printf '  fail  : %s\n' "${FAILED_SCRIPTS[@]}"
fi
if [ "${#SKIPPED_SCRIPTS[@]}" -gt 0 ]; then
    printf '  skip  : %s\n' "${SKIPPED_SCRIPTS[@]}"
fi

echo
if [ "${#FAILED_SCRIPTS[@]}" -eq 0 ]; then
    echo "RESULT: all test scripts passed"
    exit 0
fi
echo "RESULT: FAILED"
exit 1
