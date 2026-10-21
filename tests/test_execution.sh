#!/usr/bin/env bash
#
# test_execution.sh - Tests for the execution of external commands, the
# propagation of exit statuses and the robustness of the command loop.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$SCRIPT_DIR/testlib.sh"

echo "integration tests: external command execution"

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

# ---------------------------------------------------------------------
# Empty input
# ---------------------------------------------------------------------
out=$(run_shell "")
assert_eq "an empty input produces no output" "$out" ""
assert_eq "an empty input exits with status 0" "$(run_shell_status "")" "0"

out=$(run_shell "     
	  
")
assert_eq "whitespace only input produces no output" "$out" ""
assert_eq "whitespace only input exits with status 0" \
          "$(run_shell_status "     
	  
")" "0"

# ---------------------------------------------------------------------
# Valid external commands
# ---------------------------------------------------------------------
out=$(run_shell "echo Hello World
")
assert_eq "an external command is executed" "$out" "Hello World"

out=$(run_shell "/bin/echo absolute
")
assert_eq "an external command with an absolute path is executed" "$out" \
          "absolute"

out=$(run_shell "echo one
echo two
")
assert_eq "several commands are executed in order" "$out" "$(printf 'one\ntwo')"

# ---------------------------------------------------------------------
# Invalid external commands
# ---------------------------------------------------------------------
out=$(run_shell "this_command_does_not_exist_12345
")
assert_contains "an unknown command produces a diagnostic" "$out" \
                "this_command_does_not_exist_12345"
assert_eq "an unknown command exits with status 127" \
          "$(run_shell_status "this_command_does_not_exist_12345
")" "127"

# A file that exists but cannot be executed -> 126
printf '#!/bin/sh\nexit 0\n' > "$tmpdir/noexec.sh"
chmod 644 "$tmpdir/noexec.sh"
out=$(run_shell "$tmpdir/noexec.sh
")
assert_eq "a non-executable file exits with status 126" \
          "$(run_shell_status "$tmpdir/noexec.sh
")" "126"

# ---------------------------------------------------------------------
# Exit status propagation
# ---------------------------------------------------------------------
assert_eq "a successful command reports status 0" \
          "$(run_shell_status "/bin/true
")" "0"
assert_eq "a failing command reports status 1" \
          "$(run_shell_status "/bin/false
")" "1"
assert_eq "a command with exit status 42 reports 42" \
          "$(run_shell_status "/bin/sh -c 'exit 42'
")" "42"

# The status of the last command becomes the status of the shell when the
# input ends.
assert_eq "the shell reports the status of the last command at EOF" \
          "$(run_shell_status "/bin/false
")" "1"

# ---------------------------------------------------------------------
# Quoting
# ---------------------------------------------------------------------
out=$(run_shell "echo \"Hello World\"
")
assert_eq "a double quoted argument keeps its spaces" "$out" "Hello World"

out=$(run_shell "echo 'Operating Systems'
")
assert_eq "a single quoted argument keeps its spaces" "$out" \
          "Operating Systems"

out=$(run_shell "echo \"\"
")
assert_eq "an empty quoted argument produces an empty line" "$out" ""

out=$(run_shell "echo a\"b\"c
")
assert_eq "adjacent quoted and unquoted text form one argument" "$out" "abc"

out=$(run_shell "echo one two
")
assert_eq "argument boundaries are preserved" "$out" "one two"

# ---------------------------------------------------------------------
# Malformed quoting must not kill the shell
# ---------------------------------------------------------------------
out=$(run_shell "echo \"unterminated
echo after
")
assert_contains "an unterminated quote produces a diagnostic" "$out" \
                "unterminated"
assert_contains "the shell continues after a malformed quote" "$out" "after"

out=$(run_shell "echo 'unterminated
echo after
")
assert_contains "an unterminated single quote produces a diagnostic" "$out" \
                "unterminated"
assert_contains "the shell continues after a malformed single quote" "$out" \
                "after"

# ---------------------------------------------------------------------
# A parse error executes no command, so it does not change the exit
# status of the shell: the status of the last command that actually ran
# is kept.  This is documented in docs/report.md section 17.1.
# ---------------------------------------------------------------------
assert_eq "a parse error leaves the exit status unchanged" "0" \
          "$(run_shell_status 'echo "unterminated
')"
assert_eq "a parse error does not mask an earlier failure" "127" \
          "$(run_shell_status 'no_such_command_42
echo "unterminated
')"
assert_eq "a parse error does not turn a success into a failure" "0" \
          "$(run_shell_status '/bin/true
echo "unterminated
')"

# ---------------------------------------------------------------------
# Repeated execution
# ---------------------------------------------------------------------
out=$(run_shell "echo x
echo x
echo x
echo x
echo x
")
assert_eq "the same command can be executed repeatedly" "$out" \
          "$(printf 'x\nx\nx\nx\nx')"

# ---------------------------------------------------------------------
# Large but valid input
# ---------------------------------------------------------------------
long_word=$(printf 'x%.0s' $(seq 1 4000))
out=$(run_shell "echo $long_word
")
assert_eq "a 4000 character argument is passed through unchanged" \
          "${#out}" "4000"

# A long line (many arguments) must also work.
many_args=$(printf 'a%.0s' $(seq 1 500))
out=$(run_shell "echo $many_args
")
assert_eq "a line with 500 arguments is accepted" "${#out}" "500"

# ---------------------------------------------------------------------
# Graceful end of file
# ---------------------------------------------------------------------
out=$(printf 'echo no-newline' | timeout "$TEST_TIMEOUT" "$MINISHELL" 2>&1)
assert_eq "a last line without a newline is still executed" "$out" "no-newline"

out=$(printf '' | timeout "$TEST_TIMEOUT" "$MINISHELL" 2>&1)
assert_eq "an immediate EOF produces no output" "$out" ""
assert_eq "an immediate EOF exits with status 0" \
          "$(printf '' | timeout "$TEST_TIMEOUT" "$MINISHELL" >/dev/null 2>&1; echo $?)" \
          "0"

# ---------------------------------------------------------------------
# Missing files and other runtime errors
# ---------------------------------------------------------------------
out=$(run_shell "cat /this/file/does/not/exist
")
assert_contains "a missing input file produces a diagnostic" "$out" \
                "No such file or directory"

out=$(run_shell "echo hi > /no/such/directory/out.txt
")
assert_contains "an impossible output file produces a diagnostic" "$out" \
                "No such file or directory"

# ---------------------------------------------------------------------
# The shell survives a failing command
# ---------------------------------------------------------------------
out=$(run_shell "this_command_does_not_exist_12345
echo survived
")
assert_contains "the shell continues after a failed command" "$out" "survived"

# ---------------------------------------------------------------------
# Malformed syntax
# ---------------------------------------------------------------------
out=$(run_shell "echo hi > 
")
assert_contains "a redirection without a target produces a diagnostic" "$out" \
                "missing a file name"

out=$(run_shell "echo a & echo b
")
assert_contains "a token after '&' produces a diagnostic" "$out" \
                "unexpected token"

out=$(run_shell "echo \$HOME
")
assert_contains "variable expansion is reported as unsupported" "$out" \
                "unsupported"

finish
