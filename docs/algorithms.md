# Algorithms and process flow

This document describes the algorithms that the shell implements, with
pseudocode for the parts that are worth studying.

---

## 1. The command loop

```text
shell_run(state):
    interactive := isatty(stdin)
    loop:
        reap_finished_children(state)      # prints "[n] Done ..." notices
        if interactive and no partial line pending:
            print prompt to stderr
        line, status := read_line()        # read(2), survives EINTR
        if status == EOF:      break
        if status == ERROR:    break
        if status == TOO_LONG: report and continue
        if line is blank:      continue

        job, status := parse(line)
        if status != OK:
            print diagnostic to stderr
            continue                        # the shell survives

        code := execute(job, state)
        state.last_status := code
        reap_finished_children(state)
        if state.exit_requested: break
    return state.exit_requested ? state.exit_status : state.last_status
```

---

## 2. Executing a job

```text
execute(job, state):
    if job has exactly one command and is not a background job
       and the command is a built-in:
        return run_builtin_in_shell(command, state)
    return pipeline_run(job, state)
```

`run_builtin_in_shell()` executes the built-in in the shell's own
process, so that `cd` can change the working directory and `exit` can
terminate the shell.  The redirections of the command are applied to the
shell process and undone afterwards:

```text
run_builtin_in_shell(command, state):
    if redirection_apply(command, save) fails:
        redirection_restore(save)
        return 1
    status := builtins_execute(command.argv, command.argc, state)
    fflush(stdout); fflush(stderr)   # while the redirection is in effect
    redirection_restore(save)
    return status
```

---

## 3. Building a pipeline

The central algorithm.  `in_fd` is the read end of the pipe that feeds
the stage that is about to be created; it is `-1` for the first stage.

```text
pipeline_run(job, state):
    in_fd := -1
    for i := 0 to job.count - 1:
        pfd := {-1, -1}
        if i is not the last stage:
            if pipe(pfd) fails:
                report; wait for the stages that were already started; return 1

        flush stdout and stderr          # the child must not inherit
                                         # pending output of the parent
        block SIGINT, remember the mask
        pid := fork()

        if pid fails:
            restore the mask; close pfd; report; clean up; return 1

        if pid == 0:                     # ---------------- child ----------
            set SIGINT to SIG_DFL (or SIG_IGN for a background job)
            restore the signal mask
            if this is a background job and this is the first stage:
                open /dev/null and install it on descriptor 0
            if in_fd >= 0:
                dup2(in_fd, 0); close(in_fd)
            if pfd[1] >= 0:
                dup2(pfd[1], 1); close(pfd[0]); close(pfd[1])
            process_child_exec(job.commands[i], state)   # never returns

        # ---------------- parent ----------
        restore the signal mask
        if in_fd >= 0: close(in_fd)
        if pfd[0] >= 0: in_fd := pfd[0]   # handed to the next stage
        if pfd[1] >= 0: close(pfd[1])      # the parent never writes

    if in_fd >= 0: close(in_fd)

    if job.background:
        register the job (process id of the last stage) in the job table
        print "[n] <pid>"
        return 0

    status := 0
    for i := 0 to job.count - 1:
        raw := waitpid(job.pids[i], 0)
        if i is the last stage:
            status := to_exit_code(raw)
    return status
```

### 3.1 Why this cannot deadlock

1. **All stages are forked before the shell waits for any of them.**
   The consumer of a pipe therefore starts running while the producer is
   still alive.  A shell that forked stage 0, waited for it and only then
   forked stage 1 would deadlock as soon as the pipe buffer filled up.
2. **The parent never keeps a write end open.**  After the fork it closes
   `pfd[1]` immediately.  If the parent kept it, the reader would never
   see end-of-file and would block forever.
3. **The child closes the ends it does not use.**  The child of stage *i*
   closes the read end of the pipe it writes to (`pfd[0]`) and closes the
   write end of the pipe it reads from (the previous `in_fd`).
4. **The redirection is applied after the pipe descriptors.**  An explicit
   `>` on a stage replaces the pipe for that stage, which is the
   documented precedence rule.

The rule that the parent keeps `in_fd` open until the next fork is
important but harmless: the parent never reads from that descriptor, and
the end-of-file condition of a pipe depends only on the **write** ends.
The parent closes it immediately after the next stage has been forked.

### 3.2 Descriptor table of a two stage pipeline

```
                parent            child 0            child 1
  0            inherited         inherited          pipe0[0]
  1            inherited         pipe0[1]            inherited
  pipe0[0]     closed after      closed              stdin
               the fork of
               child 1
  pipe0[1]     closed after      stdout              closed
               the fork of
               child 0
```

---

## 4. Reaping children

```text
reap_finished_children(state):
    loop:
        pid := waitpid(-1, &status, WNOHANG)
        if pid <= 0: break          # 0: children exist but none exited
                                   # -1: ECHILD, no children left
        remove_from_job_table(state, pid)   # prints "[n] Done ..."
```

The reaping is **not** done in the `SIGCHLD` handler.  The handler only
sets a flag:

```c
static void handle_sigchld(int signo)
{
    (void)signo;
    g_child_event = 1;
}
```

The reason is the POSIX rule that a signal handler may only call
async-signal-safe functions.  `printf(3)`, `malloc(3)` and `waitpid(2)`
are not among them.  Deferring the work to the interpreter loop removes
the race between the signal delivery and the job bookkeeping: the job
table is only ever modified from the main control flow, and the reaper
runs at well defined points (before the prompt and after every command).

`waitpid(-1, WNOHANG)` in a loop is also robust against a lost signal:
even if the flag were never set, the loop would still collect the
children that have terminated.  The flag exists so that the shell can
notice the termination of a background job while it is waiting for the
next line of input.

---

## 5. Interrupting a foreground command

The sequence is designed so that Ctrl+C terminates the command but never
the shell.

```text
parent                              child
------                              -----
block SIGINT  (sigprocmask)
pid := fork()
                                    set SIGINT to SIG_DFL
                                    restore the mask (SIGINT unblocked)
                                    execvp(...)
restore the mask
waitpid(pid, &status, 0)
status := to_exit_code(status)   ->  128 + SIGINT = 130
```

Because the signal mask is inherited across `fork(2)`, the `SIGINT` that
the terminal sends to the whole foreground process group is *pending* in
the child from the moment it is created.  The child cannot miss it: as
soon as it restores the mask and the default disposition, the pending
signal is delivered and the child dies.

In the parent the signal stays blocked until the child has been
collected; it is then delivered to the shell's handler, which only sets a
flag.  The shell survives and prints a new prompt.

A background child receives `SIG_IGN` for `SIGINT` instead, which is what
POSIX requires for asynchronous commands: a background job must not be
killed by a Ctrl+C that was meant for the foreground job.

---

## 6. The line reader

```text
linebuf_next(buffer, out_line):
    loop:
        if the buffer contains a newline after the current position:
            terminate the line there
            remember the position after the newline
            return LINE_OK

        if the buffer is full:
            discard the bytes of the lines that were already processed
            if the buffer is at its maximum size: return LINE_TOO_LONG
            grow the buffer

        n := read(stdin, buffer + length, capacity - length - 1)
        if n > 0:  length += n; continue
        if n == 0:  # end of file
            if a partial line is pending: return it as LINE_OK
            return LINE_EOF
        if errno == EINTR:
            if a partial line is pending: continue     # keep the data
            return LINE_INTERRUPTED
        return LINE_ERROR
```

The reader never discards input that has already been read, and it can
report whether a partial line is pending.  The interpreter loop uses
that information to decide whether it may redraw the prompt.

---

## 7. The reaper and the job table

The job table is a growable array of

```c
typedef struct {
    pid_t pid;         /* the process id of the job          */
    int   job_id;      /* the number shown to the user        */
    char *command_text; /* "sleep 10", for the notification    */
} bg_job_t;
```

* A background job is inserted by `shell_state_add_job()` right after the
  fork.  The insertion happens in the parent, in the normal control flow,
  so there is no race with the `SIGCHLD` handler.
* The reaper looks the process id up in the table.  If it is found, the
  job is finished: the notification is printed, the entry is removed and
  the memory is released.
* A process id that is not in the table (for example the intermediate
  stage of a background pipeline) is simply reaped and discarded.

---

## 8. Exit status conventions

| Situation                              | Status |
|----------------------------------------|--------|
| success                                | 0      |
| general failure of a built-in          | 1      |
| wrong number of arguments to a built-in| 2      |
| the command was found but not executable| 126    |
| the command was not found               | 127    |
| the command was killed by signal *n*    | 128 + n|
| the command was interrupted by `SIGINT` | 130    |

The status of a pipeline is the status of its last stage.  The status of
the shell is the status of the `exit` built-in, or - if the shell reached
the end of its input - the status of the last command that was executed.
