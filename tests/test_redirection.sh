#!/usr/bin/env bash
#
# test_redirection.sh - Tests for the `<`, `>` and `>>` redirection
# operators and for the file descriptor hygiene of the shell.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$SCRIPT_DIR/testlib.sh"

echo "integration tests: redirection"

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

cd "$tmpdir" || exit 1

# ---------------------------------------------------------------------
# Output redirection
# ---------------------------------------------------------------------
out=$(run_shell "echo hello > out.txt
")
assert_file_exists "'>' creates the target file" "$tmpdir/out.txt"
assert_file_content "'>' writes the command output to the file" \
          "$tmpdir/out.txt" "hello"
assert_eq "'>' leaves nothing on the terminal" "$out" ""

printf 'previous content\n' > "$tmpdir/existing.txt"
out=$(run_shell "echo new > existing.txt
")
assert_file_content "'>' truncates an existing file" "$tmpdir/existing.txt" \
          "new"

# ---------------------------------------------------------------------
# Append redirection
# ---------------------------------------------------------------------
out=$(run_shell "echo first > app.txt
echo second >> app.txt
")
assert_file_content "'>>' appends to the file" "$tmpdir/app.txt" \
          "$(printf 'first\nsecond')"

out=$(run_shell "echo third >> app.txt
")
assert_file_content "'>>' appends to an existing file" "$tmpdir/app.txt" \
          "$(printf 'first\nsecond\nthird')"

out=$(run_shell "echo fourth > app.txt
")
assert_file_content "'>' truncates a file that was appended to before" \
          "$tmpdir/app.txt" "fourth"

# ---------------------------------------------------------------------
# Input redirection
# ---------------------------------------------------------------------
printf 'alpha\nbeta\ngamma\n' > "$tmpdir/in.txt"
out=$(run_shell "cat < in.txt
")
assert_eq "'<' feeds the file to the command" "$out" \
          "$(printf 'alpha\nbeta\ngamma')"

out=$(run_shell "cat < in.txt | wc -l
")
assert_eq "'<' works together with a pipeline" "$out" "3"

# ---------------------------------------------------------------------
# Combined input and output redirection
# ---------------------------------------------------------------------
out=$(run_shell "sort < in.txt > sorted.txt
")
assert_file_content "input and output redirection can be combined" \
          "$tmpdir/sorted.txt" "$(printf 'alpha\nbeta\ngamma')"

# ---------------------------------------------------------------------
# Redirection of a built-in must be undone afterwards
# ---------------------------------------------------------------------
out=$(run_shell "pwd > built_in.txt
echo marker
")
assert_file_exists "a built-in can be redirected to a file" \
          "$tmpdir/built_in.txt"
assert_file_content "the output of a redirected built-in reaches the file" \
          "$tmpdir/built_in.txt" "$(pwd)"
assert_contains "the shell's own stdout is restored after a built-in" "$out" \
                "marker"

# ---------------------------------------------------------------------
# Errors
# ---------------------------------------------------------------------
out=$(run_shell "cat < missing.txt
")
assert_contains "a missing input file is reported" "$out" \
                "No such file or directory"
assert_eq "a missing input file exits with status 1" \
          "$(run_shell_status "cat < missing.txt
")" "1"

out=$(run_shell "echo x > /no/such/dir/file
")
assert_contains "an unwritable target is reported" "$out" \
                "No such file or directory"

# Permission errors: the root user bypasses the permission bits, so the
# test is only meaningful for an unprivileged user.
if [ "$(id -u)" -ne 0 ]; then
    printf 'secret\n' > "$tmpdir/secret.txt"
    chmod 000 "$tmpdir/secret.txt"
    out=$(run_shell "cat < secret.txt
")
    assert_contains "a permission error is reported" "$out" "Permission denied"
    chmod 644 "$tmpdir/secret.txt"
else
    skip_test "a permission error is reported" "running as root"
fi

# ---------------------------------------------------------------------
# The umask influences the mode of a created file
# ---------------------------------------------------------------------
# touch(1) creates the reference file with mode 0666 & ~umask, which is the
# same rule that the shell uses for a `>` redirection.
touch "$tmpdir/reference.txt"
ref_mode=$(stat -c '%a' "$tmpdir/reference.txt")
run_shell "echo x > umask_file.txt" >/dev/null
if [ -e "$tmpdir/umask_file.txt" ]; then
    mode=$(stat -c '%a' "$tmpdir/umask_file.txt")
    assert_eq "the created file respects the umask" "$mode" "$ref_mode"
else
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    TESTS_FAILED=$((TESTS_FAILED + 1))
    printf '  [FAIL] the created file respects the umask (file was not created)\n'
fi

# ---------------------------------------------------------------------
# File descriptor hygiene
# ---------------------------------------------------------------------
# The shell must not leak descriptors: a long running shell that executes
# many redirections must keep the same number of open descriptors.
fifo="$tmpdir/ctl"
mkfifo "$fifo"
"$MINISHELL" < "$fifo" > /dev/null 2>&1 &
shell_pid=$!
exec 9> "$fifo"
fd_before=$(ls "/proc/$shell_pid/fd" | wc -l)
for _ in $(seq 1 20); do
    printf 'echo x > fd2.txt\ncat < fd2.txt\n' >&9
done
sleep 1
fd_after=$(ls "/proc/$shell_pid/fd" | wc -l)
printf 'exit\n' >&9
exec 9>&-
wait "$shell_pid" 2>/dev/null
assert_eq "a long running shell does not leak descriptors" "$fd_after" \
          "$fd_before"

finish
