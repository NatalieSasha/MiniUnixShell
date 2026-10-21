# System calls and library functions

This document lists every POSIX interface that `minishell` uses, where it
is used, and which Linux system call the interface is implemented with.

The distinction between a *library function* and a *system call* matters
for the experiments: the source code uses the portable POSIX names, while
`strace(1)` shows the names of the Linux system calls that the C library
actually issues.

---

## 1. Overview

| POSIX interface      | Linux system call     | Purpose                                   |
|----------------------|-----------------------|-------------------------------------------|
| `fork(2)`            | `clone(2)`            | create a child process                    |
| `execvp(3)`          | `execve(2)`           | replace the program image                  |
| `waitpid(2)`         | `wait4(2)`            | wait for a child to terminate              |
| `pipe(2)`            | `pipe2(2)`            | create a unidirectional channel           |
| `dup2(2)`            | `dup3(2)`             | duplicate a descriptor onto another one    |
| `open(2)`            | `openat(2)`           | open a file                               |
| `close(2)`           | `close(2)`            | release a descriptor                      |
| `sigaction(2)`       | `rt_sigaction(2)`     | install a signal handler                  |
| `sigprocmask(2)`     | `rt_sigprocmask(2)`   | block or unblock signals                  |
| `read(2)`            | `read(2)`             | read the command line                     |
| `write(2)`           | `write(2)`            | write to a descriptor (used by stdio)     |
| `_exit(2)`           | `exit_group(2)`       | terminate a child without flushing        |
| `getcwd(3)`          | `getcwd(2)`           | determine the working directory           |
| `chdir(2)`           | `chdir(2)`            | change the working directory              |
| `kill(2)`            | `kill(2)`             | not used by the shell (only by the tests)|

---

## 2. Process creation and execution

### `fork(2)` — `src/pipeline.c`

```c
signals_prepare_fork(&saved_mask);
pid = fork();
```

`fork(2)` returns twice: once in the parent with the child's process id,
once in the child with 0.  The child gets a copy of the parent's address
space; the copy is created lazily by the kernel through copy-on-write, so
the cost of the fork does not depend on the size of the shell's memory.

The child then

1. restores the signal mask and the default disposition of `SIGINT`
   (`signals_child_after_fork()` in `src/signals.c`),
2. installs the pipe descriptors on descriptor 0 and 1 with `dup2(2)`,
3. applies the redirections of its command (`src/redirection.c`),
4. calls `execvp(3)`, which replaces the program image.

If `execvp(3)` fails, the child prints a diagnostic and terminates with
`_exit(127)` (command not found) or `_exit(126)` (found but not
executable).

### `execvp(3)` — `src/process.c`

`execvp(3)` is a *library* function, not a system call.  If the command
name contains no `/`, glibc searches the directories of the `PATH`
environment variable and issues one `execve(2)` system call per candidate
directory.  The trace in `results/process_creation.txt` shows exactly
this: several failed `execve(2)` calls followed by a successful one.

`execvp(3)` returns only when it failed.  On success the process image is
replaced and the process continues at the entry point of the new program.

### `waitpid(2)` — `src/process.c`

```c
r = waitpid(pid, &status, 0);
```

`waitpid(2)` blocks until the child with the given process id has
terminated and writes the exit status into `status`.  The status must be
interpreted with the `WIFEXITED()`, `WEXITSTATUS()`, `WIFSIGNALED()` and
`WTERMSIG()` macros, because a terminated-by-signal child does not have an
exit code.

`process_status_to_exit_code()` converts the raw status into the status
that the shell reports:

* `WIFEXITED`  -> `WEXITSTATUS(status)` (0 ... 255)
* `WIFSIGNALED` -> `128 + WTERMSIG(status)` (the convention of the POSIX
  shells; `SIGINT` therefore becomes 130)

`waitpid(-1, &status, WNOHANG)` is used by the reaper
(`process_reap_children()`); it returns immediately and yields 0 if no
child has terminated.

### `_exit(2)` — `src/process.c`

`_exit(2)` terminates the process without running the `atexit(3)` handlers
and without flushing the stdio buffers.  The child uses it after a failed
`execvp(3)`, because the child inherited a *copy* of the parent's stdio
buffers.  Calling `exit(3)` instead would flush those buffers a second
time and could duplicate output that the parent has not written yet.

The one place where the child has to flush explicitly is the built-in
path of `process_child_exec()`: a built-in writes with `printf(3)`, whose
output may still be in the buffer.  The child therefore calls
`fflush(stdout)` **before** it restores the descriptors and calls
`_exit(2)`.  The order matters: flushing after the restore would send
the data to the wrong destination.

---

## 3. Pipes and descriptors

### `pipe(2)` — `src/pipeline.c`

`pipe(2)` creates two descriptors.  The read end is used for input, the
write end for output; the data flows in one direction only.

The shell creates one pipe per `|` in the command line and connects them
as follows:

```
stage 0  --pipe0-->  stage 1  --pipe1-->  stage 2
```

Descriptor table of the three stages (the numbers are the values that
`pipe(2)` returned):

| Descriptor | stage 0 | stage 1 | stage 2 |
|------------|---------|---------|---------|
| 0 (stdin) | inherited| `pipe0[0]` | `pipe1[0]` |
| 1 (stdout)| `pipe0[1]` | `pipe1[1]` | inherited |
| `pipe0[0]` | closed   | **stdin**  | closed |
| `pipe0[1]` | **stdout** | closed | closed |
| `pipe1[0]` | closed   | closed | **stdin**  |
| `pipe1[1]` | closed   | **stdout** | closed |

The parent closes both ends of every pipe right after the fork, except
for the read end that it hands over to the next stage.  If a stage kept
the write end of a pipe open, the reader would never observe end-of-file
and the pipeline would block forever; this is the classic cause of a
"deadlocked pipeline".

### `dup2(2)` — `src/pipeline.c`, `src/redirection.c`

`dup2(oldfd, newfd)` duplicates the descriptor `oldfd` on the position
`newfd`.  If `newfd` was open it is closed first, and the operation is
atomic: another thread can never observe a moment in which `newfd` is
invalid.  `dup2(2)` is the reason why a redirection can be implemented
without closing descriptor 0 or 1 first, which would leave the shell
without a standard input if the operation failed.

The shell uses `dup2(2)` in two places:

* to install a pipe end on descriptor 0 or 1 (`src/pipeline.c`),
* to install the file of a redirection on descriptor 0 or 1
  (`src/redirection.c`).

### `open(2)` and `close(2)` — `src/redirection.c`

| Operator | `open(2)` flags                                | Mode |
|----------|------------------------------------------------|------|
| `<`      | `O_RDONLY`                                     | -    |
| `>`      | `O_WRONLY \| O_CREAT \| O_TRUNC`               | 0666 |
| `>>`     | `O_WRONLY \| O_CREAT \| O_APPEND`              | 0666 |

The mode 0666 is the traditional value; the kernel masks it with the
current `umask`, so with the default `umask 022` a newly created file has
the mode 0644.  `umask(2)` is inherited by the child processes and is
therefore also in effect for the files that a command creates.

The complete sequence for `cmd > file` is

```
open("file", O_WRONLY|O_CREAT|O_TRUNC, 0666)  ->  fd
dup(STDOUT_FILENO)                            ->  saved
dup2(fd, STDOUT_FILENO)
close(fd)
```

`dup(2)` is only used when a built-in is executed with a redirection in
the shell process itself: the original descriptor has to be restored
afterwards.  In a child process the descriptor is simply replaced, because
the child is replaced by the program image.

---

## 4. Signals

### `sigaction(2)` — `src/signals.c`

`sigaction(2)` is used instead of the older `signal(2)` because it is
explicit about the flags, it supports `SA_NOCLDSTOP` and `SA_RESTART`,
and it has well defined semantics for the delivery of the signal.

| Signal    | Disposition in the shell                     | Disposition in a foreground child | Disposition in a background child |
|-----------|----------------------------------------------|----------------------------------|----------------------------------|
| `SIGINT`  | handler, sets a flag                         | `SIG_DFL`                        | `SIG_IGN`                        |
| `SIGCHLD` | handler, sets a flag (`SA_NOCLDSTOP`)        | inherited                        | inherited                        |
| `SIGPIPE` | default                                       | `SIG_DFL` (set explicitly)       | `SIG_DFL` (set explicitly)       |

`SA_NOCLDSTOP` suppresses the notification for children that are stopped
or continued; only the termination of a child is of interest.

The `SIGCHLD` handler does nothing except set a `volatile sig_atomic_t`
flag.  The reaping itself is done in `process_reap_children()` with
`waitpid(-1, ..., WNOHANG)`, which is called from the normal control flow
of the interpreter loop.  This is the only safe approach: a signal
handler may only call async-signal-safe functions, and `printf(3)` is not
one of them.

### `sigprocmask(2)` — `src/signals.c`

While a foreground child is running, the shell blocks `SIGINT`.  The
child restores the previous mask and sets the default disposition of
`SIGINT` before it calls `execvp(3)`, so the mask is inherited across the
`fork(2)` and the child dies from `SIGINT` even if the user presses
Ctrl+C in the tiny interval between the `fork(2)` and the `execve(2)`.

The shell itself is not killed by Ctrl+C: the `SIGINT` that the terminal
delivers to the whole foreground process group is blocked in the shell
while the child runs, and is delivered to the shell's handler after the
child has been collected.

---

## 5. Mapping to the source files

| Concept                     | API / system call   | Source file and function                       | Test                             |
|-----------------------------|---------------------|-----------------------------------------------|----------------------------------|
| Process creation            | `fork(2)`           | `src/pipeline.c: pipeline_run()`              | `test_execution.sh`              |
| Program replacement         | `execvp(3)`         | `src/process.c: process_child_exec()`         | `test_execution.sh`              |
| Parent/child synchronisation| `waitpid(2)`        | `src/process.c: process_wait_for()`           | `test_execution.sh`              |
| Exit status handling        | `WIFEXITED` et al.  | `src/process.c: process_status_to_exit_code()` | `test_execution.sh`              |
| Pipe creation               | `pipe(2)`           | `src/pipeline.c: pipeline_run()`              | `test_pipelines.sh`              |
| Descriptor duplication      | `dup2(2)`           | `src/pipeline.c: move_descriptor()`           | `test_pipelines.sh`              |
| File opening and closing    | `open(2)`, `close(2)`| `src/redirection.c: redirection_apply()`      | `test_redirection.sh`            |
| Saving a descriptor         | `dup(2)`            | `src/redirection.c: redirection_apply()`      | `test_redirection.sh`            |
| Signal registration         | `sigaction(2)`      | `src/signals.c: signals_install()`            | `test_signals.sh`                |
| Blocking a signal           | `sigprocmask(2)`    | `src/signals.c: signals_prepare_fork()`       | `test_signals.sh`                |
| Child process cleanup       | `waitpid(-1, WNOHANG)` | `src/process.c: process_reap_children()`    | `test_background.sh`             |
| Built-in execution          | -                   | `src/builtins.c: builtins_execute()`           | `test_builtins.sh`               |
| Parsing                     | -                   | `src/parser.c: parser_parse()`                 | `test_parser.c`                  |
