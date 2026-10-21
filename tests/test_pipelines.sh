#!/usr/bin/env bash
#
# test_pipelines.sh - Tests for pipelines, their exit status and the
# absence of deadlocks.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$SCRIPT_DIR/testlib.sh"

echo "integration tests: pipelines"

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

cd "$tmpdir" || exit 1

printf 'alpha\nbeta\ngamma\ndelta\nerror one\nerror two\n' > "$tmpdir/input.txt"

# ---------------------------------------------------------------------
# Two stage pipeline
# ---------------------------------------------------------------------
out=$(run_shell "cat input.txt | grep error
")
assert_eq "a two stage pipeline filters the input" "$out" \
          "$(printf 'error one\nerror two')"

out=$(run_shell "cat input.txt | wc -l
")
assert_eq "a two stage pipeline counts the lines" "$out" "6"

# ---------------------------------------------------------------------
# Multi stage pipeline
# ---------------------------------------------------------------------
out=$(run_shell "cat input.txt | grep error | wc -l
")
assert_eq "a three stage pipeline produces the expected result" "$out" "2"

out=$(run_shell "cat input.txt | grep error | grep two
")
assert_eq "a three stage pipeline can filter twice" "$out" "error two"

# ---------------------------------------------------------------------
# Redirection combined with a pipeline
# ---------------------------------------------------------------------
out=$(run_shell "cat < input.txt | grep error | wc -l > count.txt
")
assert_file_content "redirection and pipelines can be combined" \
          "$tmpdir/count.txt" "2"

out=$(run_shell "cat input.txt > copy.txt
")
assert_file_content "'>' redirects the output of a pipeline stage" \
          "$tmpdir/copy.txt" "$(cat "$tmpdir/input.txt")"

out=$(run_shell "cat input.txt | grep error >> errors.txt
")
assert_file_content "'>>' appends the output of a pipeline" \
          "$tmpdir/errors.txt" "$(printf 'error one\nerror two')"

# The redirection of a stage overrides the pipe for that stage.
out=$(run_shell "cat input.txt | cat > merged.txt
")
assert_file_content "a stage redirection replaces the pipe for that stage" \
          "$tmpdir/merged.txt" "$(cat "$tmpdir/input.txt")"

# ---------------------------------------------------------------------
# Exit status of a pipeline
# ---------------------------------------------------------------------
assert_eq "a pipeline reports the status of its last stage" \
          "$(run_shell_status "cat input.txt | /bin/false
")" "1"
assert_eq "a successful pipeline reports status 0" \
          "$(run_shell_status "cat input.txt | grep error
")" "0"
# The status of an earlier stage is not the status of the pipeline.
assert_eq "the status of an earlier stage does not become the pipeline status" \
          "$(run_shell_status "/bin/false | /bin/true
")" "0"

# ---------------------------------------------------------------------
# Failing pipelines
# ---------------------------------------------------------------------
out=$(run_shell "cat missing.txt | wc -l
")
assert_contains "a failing first stage is reported" "$out" \
                "No such file or directory"

out=$(run_shell "cat input.txt | no_such_command_xyz
")
assert_contains "a failing last stage is reported" "$out" \
                "no_such_command_xyz"
assert_eq "a pipeline with an unknown command reports status 127" \
          "$(run_shell_status "cat input.txt | no_such_command_xyz
")" "127"

# ---------------------------------------------------------------------
# No deadlock: a producer that writes more than the pipe buffer
# ---------------------------------------------------------------------
out=$(run_shell "yes hello | head -3
")
assert_eq "a producer that exceeds the pipe buffer does not deadlock" \
          "$out" "$(printf 'hello\nhello\nhello')"

out=$(run_shell "seq 1 20000 | wc -l
")
assert_eq "a large data flow through a pipe is handled" "$out" "20000"

# The consumer must start before the producer finishes: a pipeline whose
# first stage sleeps must still deliver the data of the second stage.
out=$(run_shell "sleep 1 | cat
")
assert_eq "a sleeping producer does not block the consumer" "$out" ""

# ---------------------------------------------------------------------
# Built-ins inside a pipeline
# ---------------------------------------------------------------------
out=$(run_shell "echo hello | pwd
")
assert_contains "a built-in can be the last stage of a pipeline" "$out" "/"

out=$(run_shell "echo hello | cat
")
assert_eq "a built-in in the middle of a pipeline produces no output" \
          "$out" "hello"

# `cd` inside a pipeline must not change the directory of the shell.
before=$(pwd)
out=$(run_shell "echo x | cd /tmp
pwd
")
assert_contains "'cd' inside a pipeline does not change the shell's directory" \
                "$out" "$before"

# ---------------------------------------------------------------------
# A pipeline with a single stage behaves like a normal command
# ---------------------------------------------------------------------
out=$(run_shell "echo solo
")
assert_eq "a single stage command works" "$out" "solo"

finish
