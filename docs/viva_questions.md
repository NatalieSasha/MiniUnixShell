# Viva questions and answers

Forty questions about the implementation, with answers that match the code
in `src/`.  The source file and the function that implements a concept
are named in each answer so that the claim can be verified immediately.

---

### 1. What is a shell?

A shell is a program that reads command lines from a user or from a
script, interprets them, and either performs the requested action itself
or starts a program that performs it.  A shell is therefore both a
command interpreter and a *program launcher*: it is the layer between the
user and the operating system's process interface.  `minishell`
(`src/shell.c`, `shell_run()`) implements the interpreter loop, and
`src/process.c` / `src/pipeline.c` implement the launcher.

### 2. What is a process?

A process is an instance of a program in execution.  The operating system
keeps for every process a control block (in Linux: `task_struct`) that
holds its state: the program counter, the registers, the memory map, the
open file descriptors and the parent/child relationship.  A process is
identified by its process id (PID), a positive integer that the kernel
assigns and that is unique among the running processes.

### 3. What happens during `fork(2)`?

`fork(2)` creates a second process from the calling one.  The kernel
allocates a new control block and a new address space that is initially
a *copy* of the parent's; the pages are shared copy-on-write, so the
fork is cheap until one of the processes writes to a page.  The child
receives a return value of 0, the parent receives the PID of the child.
Both processes continue at the instruction after the `fork(2)` call.
The child inherits the open file descriptors of the parent - that is
precisely why a pipe can be used to connect a parent and a child.

### 4. How does `execvp(3)` differ from `fork(2)`?

`fork(2)` creates a *new* process; the calling process continues
unchanged.  `execvp(3)` does not create anything: it replaces the
program image of the *calling* process with a new program and never
returns on success.  The usual combination is therefore
`fork(2)` followed by `execvp(3)` in the child: the child starts out as
a copy of the shell and then becomes the requested program.  Because
`execvp(3)` never returns on success, the child must always terminate
with `_exit(2)` on the error path - see `process_child_exec()` in
`src/process.c`.

### 5. Why does the child use `_exit(2)` after a failed execution?

`_exit(2)` terminates the process without flushing the stdio buffers.
The child inherited a *copy* of the parent's stdio buffers through
`fork(2)`.  If the child called `exit(3)` instead, it would flush those
inherited buffers, and the data would be written a second time - the
user would see the output of the shell duplicated.  `_exit(2)` avoids
this.  The single exception is the built-in path in
`process_child_exec()`, where the child itself produced output with
`printf(3)`: there the buffers are flushed explicitly with
`fflush(stdout)` *before* the descriptors are restored, because
`_exit(2)` would discard that output.

### 6. What does `waitpid(2)` do?

`waitpid(2)` suspends the calling process until one of its children
changes state, and then reports which child it was and how it
terminated.  With `pid > 0` it waits for that specific child, with
`pid == -1` it waits for any child, and the `WNOHANG` flag makes it
return immediately with 0 if no child has terminated yet.  The exit
status has to be decoded with `WIFEXITED()`, `WEXITSTATUS()`,
`WIFSIGNALED()` and `WTERMSIG()`, because a child that was killed by a
signal has no exit code.  `process_wait_for()` in `src/process.c`
implements the waiting and retries on `EINTR`.

### 7. What is a zombie process?

A zombie is a process that has terminated but whose exit status has not
yet been collected by its parent.  It occupies an entry in the process
table but consumes no memory and no CPU.  The parent removes it with
`waitpid(2)`.  A shell that starts background jobs and never calls
`waitpid(2)` would accumulate zombies; `minishell` avoids this with
`process_reap_children()`, which calls `waitpid(-1, ..., WNOHANG)` in a
loop.  The experiment in `results/resource_management.txt` shows that no
child is left in the process table after ten background jobs.

### 8. What is an orphan process?

An orphan is a process whose parent has terminated.  The kernel
re-parents an orphan to the init process (or to the nearest subreaper),
which is then responsible for reaping it.  A background job of a shell
that is killed becomes an orphan; it keeps running, but it can no longer
be reaped by the original shell.

### 9. What is a file descriptor?

A file descriptor is a small non-negative integer that the kernel uses as
an index into the per-process table of open files.  Descriptor 0, 1 and 2
are by convention the standard input, output and error.  A descriptor
refers to an *open file description*, which holds the position in the
file, the status flags and the reference to the underlying object.  Two
descriptors that were produced by `dup(2)` or `dup2(2)` share the same
open file description and therefore the same file offset - that is why a
shell can redirect descriptor 1 without disturbing the offset of
descriptor 0.

### 10. What does `dup2(2)` do?

`dup2(oldfd, newfd)` duplicates the descriptor `oldfd` onto the
position `newfd`.  If `newfd` is already open, it is closed first, and
the operation is atomic: no other thread can observe a moment in which
`newfd` is invalid.  `dup2(2)` is therefore the safe way to install a
new standard input or output; simply closing descriptor 1 first would
leave the process without a standard output if the following call
failed.  `minishell` uses `dup2(2)` in `move_descriptor()`
(`src/pipeline.c`) and in `redirection_apply()` (`src/redirection.c`).

### 11. How do Unix pipes work?

`pipe(2)` creates two descriptors that refer to the same kernel object, a
buffer of a few tens of kilobytes in the kernel memory.  A write to the
write end appends data to the buffer, a read from the read end removes it.
The data never passes through user space, and it never reaches the file
system.  If the buffer is full, the writing process blocks; if the
buffer is empty, the reading process blocks.  When every write end of a
pipe has been closed, the read end reports end-of-file.  That last
property is the reason why the unused ends must be closed.

### 12. Why must unused pipe endpoints be closed?

Because a pipe reports end-of-file only when **all** write ends have been
closed.  If the parent process keeps the write end of a pipe open after
the producer has finished, the consumer waits for data that will never
come and the pipeline blocks forever - this is the classic deadlock of a
shell implementation.  `minishell` therefore closes the write end in the
parent immediately after the fork and closes the read end in the child
that does not read from it.  The rule is documented in
`docs/algorithms.md`, section 3.1.

### 13. What is the difference between foreground and background execution?

A foreground command is *synchronised* with the shell: the shell calls
`waitpid(2)` for it and cannot read the next command line until the
command has terminated.  A background command is *asynchronous*: the
shell forks it, records its process id in the job table, prints a
notification and returns to the prompt immediately.  The status of a
background command is not reported through the exit status of the shell.
The experiments in `results/foreground_background.txt` show three seconds
for the foreground case and zero seconds for the background case.

### 14. What is `SIGCHLD`?

`SIGCHLD` is delivered to a parent when one of its children changes
state.  With `SA_NOCLDSTOP` it is only delivered when a child
terminates.  The handler must not call `waitpid(2)` directly, because a
signal handler may only use async-signal-safe functions.  `minishell`
therefore only sets a flag in the handler (`src/signals.c`) and collects
the children in the normal control flow (`process_reap_children()`).

### 15. What is `SIGINT`?

`SIGINT` is the signal that the terminal driver sends to the foreground
process group when the user presses Ctrl+C.  It asks a program to
interrupt its current work.  `minishell` installs a handler for it that
only sets a flag, so the shell itself is never killed by a stray Ctrl+C.
While a foreground child runs, the shell blocks `SIGINT` and the child
restores the default disposition, so the child dies and the shell
survives.  The status of an interrupted command is 128 + 2 = 130.

### 16. Why is `sigaction(2)` generally preferred over the older signal interfaces?

`sigaction(2)` gives full control over the behaviour: the flags
(`SA_RESTART`, `SA_NOCLDSTOP`, ...) are explicit, the signal mask that
is installed for the handler is specified, and the interface is not
affected by the historical inconsistencies of `signal(2)` - in
particular the semantics of `signal(2)` differ between SysV and BSD
systems.  `minishell` uses `sigaction(2)` in `signals_install()`.

### 17. What happens when a command is not found?

`execvp(3)` fails with `ENOENT` after it has tried every directory of
`PATH`.  The child prints `minishell: <name>: No such file or directory`
to standard error and terminates with `_exit(127)`, the conventional
status for "command not found".  If the file exists but cannot be
executed, the status is 126.  The parent collects the status with
`waitpid(2)` and reports it.

### 18. How does the shell parse quoted arguments?

The tokeniser (`read_word()` in `src/parser.c`) reads a word character
by character.  A single quoted section is copied literally until the
next single quote.  A double quoted section keeps blanks literal and
treats a backslash as an escape only before `"`, `\` and `$`.  A
backslash outside quotes escapes the next character.  Quoted and
unquoted sections may be adjacent; they all belong to the same word, so
`echo a"b c"d` produces the single argument `ab cd`.  The complete
grammar is in `docs/parser_grammar.md`.

### 19. Why is `strtok(3)` insufficient for a real shell grammar?

`strtok(3)` splits a string at a fixed set of delimiters and it cannot
express quoting, escaping or nested structures.  It cannot tell
`echo "a b"` from `echo a b`, it cannot handle an empty argument
(`echo ""`), and it has no notion of operators such as `|`, `>` or `&`.
A shell needs a tokeniser that understands the quoting rules and a parser
that groups the tokens into commands and pipelines, which is exactly what
`src/parser.c` does in two separate phases.

### 20. What is the role of `execve(2)`?

`execve(2)` is the system call that replaces the image of the calling
process with the program given by the path.  It is the primitive that
implements every other "execute a program" interface, including
`execvp(3)`, which only adds the search of the `PATH` variable.  After a
successful `execve(2)` the process keeps the same PID and the same open
descriptors (unless they were marked close-on-exec), but its program,
its arguments and its environment have been replaced.

### 21. What is the difference between a library function and a system call?

A system call is an interface to the kernel; it is entered through a
trap instruction and the arguments are passed in registers.  A library
function is ordinary code of the C library that may perform one or
several system calls.  `open(2)` is a library function that glibc
implements with the `openat(2)` system call, which is why a `strace(1)`
trace of `open(2)` shows `openat(2)`.  `fork(2)` is implemented with
`clone(2)`, and `waitpid(2)` with `wait4(2)`.  The distinction matters
when the traces of the experiments are interpreted.

### 22. What does `strace` show?

`strace(1)` shows the system calls that a program issues, together with
their arguments and their return values.  With `-f` it follows the
child processes and prefixes every line with the process id; with
`-e trace=...` it restricts the output to a class of calls.  It shows
the *system call* interface, not the library interface, and it is
therefore the tool that makes the difference between `fork(2)` and
`clone(2)` visible.

### 23. How are exit statuses represented?

On Unix, a process terminates either normally, in which case it provides
an exit code in the range 0 to 255, or because of a signal, in which case
it provides no code at all.  `waitpid(2)` returns a single integer that
encodes both cases, and the macros `WIFEXITED()`, `WEXITSTATUS()`,
`WIFSIGNALED()` and `WTERMSIG()` decode it.  A shell therefore has to
translate the raw value into a number: `minishell` uses the exit code
itself, or `128 + signal number` for a signal, which is the convention of
the POSIX shells.

### 24. Why must built-ins such as `cd` execute in the parent shell?

Because `cd` has to change the working directory of the shell itself, and
the working directory is a property of the *process*.  A child process
created with `fork(2)` has its own copy of that property, so a `cd` in a
child is lost when the child terminates.  `minishell` therefore executes
a single foreground built-in in the shell's own process
(`run_builtin_in_shell()` in `src/process.c`) and executes it in a child
when it is part of a pipeline or a background job - which reproduces the
behaviour of a POSIX shell, where `echo x | cd /tmp` has no effect.

### 25. What causes pipeline deadlocks?

Three situations.  (1) The producer writes more than the kernel buffer
holds while nobody reads - the fix is that the consumer has to be started
before the producer can fill the buffer, which is why `minishell` forks
all stages before it waits for any of them.  (2) A process keeps a write
end of a pipe open that it does not write to, so the reader never sees
end-of-file.  (3) Two processes wait for each other, for example when the
shell waits for the producer before it starts the consumer.  `minishell`
avoids all three by forking every stage first and by closing every
descriptor that a stage does not need.

### 26. What is the role of `umask`?

The `umask` is a per-process value that masks the permission bits of
every file that is created.  It is inherited by the child processes and
is therefore also in effect for the files that a command creates.  A
redirection with `>` creates the file with the mode `0666 & ~umask`, so
with the usual `umask 022` the new file has the mode 0644.  The shell
does not change the `umask`; the test `test_redirection.sh` compares the
mode of a file created by the shell with the mode of a file created by
`touch(1)` and checks that they are identical.

### 27. What are race conditions in child-process management?

A race is a situation in which the result depends on the order in which
two events happen.  The classic example is a `SIGCHLD` that arrives
between the `fork(2)` and the registration of the child in the job table:
if the shell reaped the child before it had been registered, the
notification would be lost.  `minishell` avoids this by never reaping
inside the signal handler.  The handler only sets a flag, and the
reaping happens in `process_reap_children()`, which is called from the
normal control flow of the interpreter loop at points where the job table
is consistent.  A second race - the delivery of `SIGINT` between the
`fork(2)` and the installation of the default disposition in the child -
is closed by blocking `SIGINT` around the `fork(2)`: the mask is
inherited by the child, so the signal is pending there and is delivered
as soon as the child restores the mask and the default disposition.

### 28. What happens when `fork(2)` fails?

`fork(2)` fails when the process limit of the system is reached, when
the limit of the user is reached, or when the memory cannot be allocated.
The shell reports `minishell: fork: <reason>` and returns a status of 1.
In a pipeline, the stages that were already started are collected with
`waitpid(2)` before the shell returns, so that no zombie is left behind.
`minishell` never continues into the interpreter loop with a half
constructed pipeline.

### 29. How does redirection interact with pipelines?

Redirection binds more tightly than the pipe operator:
`cat < in.txt | grep error > out.txt` means `( cat < in.txt ) | ( grep
error > out.txt )`.  The redirection of a stage is applied *after* the
pipe descriptors have been installed, so an explicit `>` on a stage
replaces the pipe for that stage.  The redirection of the first stage
applies to its standard input, the redirection of the last stage applies
to its standard output.  The test `test_pipelines.sh` contains a case
that verifies exactly this.

### 30. What is the difference between a shell and a terminal?

A terminal is a character device that is connected to a process
(through a pseudo terminal in a modern system); it translates the
keystrokes into input for a process and renders the output of a process
on the screen.  A shell is a *program* that reads that input and
executes commands.  The terminal does not know anything about commands;
the shell does not know anything about the screen.  The consequence for
this project is that the prompt is only printed when the standard input
is a terminal (`isatty(3)`), so that a scripted run produces clean
output, and the prompt is written to standard error so that it does not
end up in a redirected output file.

### 31. Why is this not a full Bash implementation?

Because a shell is a language, and a language has to be specified
completely before it can be implemented completely.  Bash has variables,
arithmetic, arrays, functions, aliases, job control, programmable
completion, history, globbing, brace expansion, parameter substitution,
here-documents, process substitution, conditional statements, loops and
much more.  `minishell` implements the part of the language that is
needed to demonstrate the operating system concepts of the project -
process creation, program execution, synchronisation, pipes, redirection
and background jobs - and rejects everything else with a diagnostic
instead of pretending to support it.  The limits are listed in
`docs/limitations.md`.

### 32. What are the limitations of the project's job-control implementation?

There is no job control in the strict sense: the shell does not create a
process group for a foreground job, does not hand the terminal over with
`tcsetpgrp(3)`, and does not implement `fg`, `bg` or `jobs`.  Because
the shell and its children share one process group, the `SIGINT` of the
terminal reaches the shell as well; the shell blocks it while a
foreground child runs, and it sets `SIG_IGN` in background children so
that they are not affected.  A background job therefore cannot read from
the terminal, which is why its standard input is redirected to
`/dev/null`.  A suspended job cannot be resumed.  These limitations are
documented in `docs/limitations.md`, section 2.

### 33. How would the shell be extended to support environment-variable expansion?

The parser would have to stop treating `$` as an error.  A parameter
expansion is not a lexical operation: the value of the variable is
inserted into the word, and the result is subject to word splitting and
to globbing, so the expansion cannot happen in the tokeniser but has to
happen between the tokeniser and the parser - a third phase.  The
implementation would need an environment store in the shell state, an
`export` built-in, and a clear decision about how the result of an
expansion is split into arguments.  The parser of `minishell` is
structured in two phases, so a third phase could be inserted between
`tokenize()` and `build_job()`.

### 34. How would process groups and terminal control support proper job control?

Every job would get its own process group: the child would call
`setpgid(0, 0)` right after the `fork(2)`, so that the job becomes the
leader of a new group.  The parent would then hand the terminal over to
that group with `tcsetpgrp(3)`, and take it back when the job is
finished.  The shell would have to ignore `SIGTTOU` and `SIGTTIN`, and
the terminal would then deliver Ctrl+C only to the process group of the
foreground job, so that a background job would no longer receive the
signal by accident.  In addition the shell would need a handler for
`SIGTSTP` to note that a job has been stopped, a way to continue a
stopped job, and the commands `fg`, `bg` and `jobs` to drive the job
table.  The job table of `minishell` already stores the process id and
could be extended with the process group id.

### 35. What testing evidence demonstrates correctness?

Three independent kinds of evidence.  (1) A unit test of the parser
(`tests/test_parser.c`) that checks the tokenising and the error
detection in isolation.  (2) Six integration test scripts
(`tests/test_*.sh`) that drive the real executable and assert on its
output, on its exit status and on the files it produces; they cover the
built-ins, the external commands, the quoting, the redirections, the
pipelines, the background jobs and the signals, and every invocation is
protected by a `timeout(1)`.  (3) The complete suite was executed under
AddressSanitizer and UndefinedBehaviorSanitizer with
`detect_leaks=1`; the sanitizers found two real defects, both of which
were fixed.  The experiments in `results/` add the observation of the
system call level behaviour with `strace(1)`.

### 36. Why does the shell use `read(2)` instead of `getline(3)`?

Because a `SIGCHLD` that arrives while the shell waits for input
interrupts the read, and `getline(3)` cannot be resumed safely in that
case - the bytes that were already read may be lost.  The small reader in
`src/shell.c` keeps the bytes that were read before the interruption and
continues, and it also knows whether a partial line is pending, so that
the shell can decide whether it may redraw the prompt without destroying
what the user has typed.

### 37. What is the difference between a signal handler and a process synchronisation mechanism?

A signal handler is a reaction to an asynchronous event; it runs in the
context of the interrupted process, at an arbitrary point of the
program, and it may only use async-signal-safe functions.  A
synchronisation mechanism such as `waitpid(2)` is a deliberate
cooperation between two processes: the parent blocks until the child
reports that it has finished.  The `SIGCHLD` handler of `minishell`
only *notifies* the shell that a child has terminated; the actual
synchronisation is the `waitpid(2)` call in `process_reap_children()`.
The separation keeps the two mechanisms apart and avoids the classic
mistake of doing the work in the handler.

### 38. Why is the exit status of a pipeline the status of the last stage?

Because that is the convention of the POSIX shell, and because the last
stage is the one whose result the user is normally interested in: in
`cat f | grep error | wc -l` the interesting number is the one that
`wc` produced.  The alternative convention - the status of the *first*
stage - is used by some other tools.  `minishell` follows the POSIX
convention and the test `test_pipelines.sh` verifies that
`/bin/false | /bin/true` reports 0.

### 39. How does the shell decide that a command is a built-in?

`builtins_is_builtin()` in `src/builtins.c` compares the first word of
the command with the fixed table of the four built-in names.  The
decision is made *before* the fork, because a built-in that changes the
state of the shell has to run in the shell's own process.  A built-in
that is part of a pipeline or of a background job runs in a child
process, where the state changes are discarded.

### 40. What would happen if `execvp(3)` succeeded but the program was not what the user expected?

Nothing special: the shell has no way of knowing.  The shell's
responsibility ends at the point where `execvp(3)` has replaced the
program image.  Everything that happens afterwards - the output, the exit
status, the signals - is the behaviour of the new program, and the shell
observes it only through `waitpid(2)`.  This separation is the reason
why the shell is small: it does not interpret the output of a program, it
only starts programs and waits for them.

### 41. How does the shell avoid leaking file descriptors?

Every descriptor that the shell opens is closed again: the temporary
descriptor of a redirection is closed after `dup2(2)`, and the saved copy
that `dup(2)` created is closed by `redirection_restore()`.  In a
pipeline, the parent closes both ends of every pipe right after the
fork, and the child closes the ends it does not use.  The test
`test_redirection.sh` counts the entries of `/proc/<pid>/fd` before and
after thirty commands with redirections and checks that the number does
not change, and the experiment in `results/resource_management.txt`
records the same measurement.

### 42. Why does the shell not use `system(3)`?

`system(3)` runs a command through `/bin/sh -c`, which means that the
behaviour of the command depends on a different shell, that the shell
cannot control the process group, the redirections or the exit status
precisely, and that the intermediate shell appears in every trace.  The
whole point of this project is to use the process interface of the
operating system directly, so `fork(2)`, `execvp(3)` and `waitpid(2)`
are used instead.  `system(3)` does not appear anywhere in `src/`.
