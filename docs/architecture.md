# Architecture

This document describes how `minishell` is structured, which module is
responsible for what, and how the data flows through the program.

---

## 1. Module map

```
                        +------------------+
   keyboard  ---------> |     shell.c      |  interpreter loop, job table
                        +--------+---------+
                                 |
                                 v
                        +------------------+
                        |    parser.c      |  line -> job_t
                        +--------+---------+
                                 |
                                 v
                        +------------------+
                        |   process.c      |  dispatch, waitpid, reaping
                        +--------+---------+
                    +------------+------------+
                    v                         v
          +------------------+      +------------------+
          |   builtins.c     |      |   pipeline.c     |
          | help pwd cd exit |      | pipes, forking   |
          +------------------+      +--------+---------+
                                           |
                                           v
                                  +------------------+
                                  |  redirection.c   |  open/dup2/close
                                  +------------------+
        (all modules)
            +------------------+
            |    signals.c     |  sigaction, SIGCHLD/SIGINT
            +------------------+
```

| File               | Responsibility                                                       |
|--------------------|----------------------------------------------------------------------|
| `src/main.c`       | Entry point, option handling, installation of the signal handlers.   |
| `src/shell.c`      | The interpreter loop, the line reader, the background job table.     |
| `src/parser.c`     | Tokeniser and parser; produces the `job_t` data structure.           |
| `src/builtins.c`   | The four built-in commands.                                          |
| `src/process.c`    | Job dispatch, the child side of the fork, `waitpid(2)`, the reaper.  |
| `src/pipeline.c`   | Construction of pipelines, descriptor plumbing, background jobs.     |
| `src/redirection.c`| `open(2)` / `dup2(2)` / `close(2)` for `<`, `>` and `>>`.            |
| `src/signals.c`    | `sigaction(2)` setup and the signal-safe flag mechanism.             |

Every header carries an include guard, every module has a single clear
purpose, and the only mutable state that is shared between modules is the
`shell_state_t` structure, which is passed explicitly as a pointer.

---

## 2. Data flow

```mermaid
flowchart TD
    A[read(2) from stdin] --> B[linebuf_next: line reader]
    B --> C{line complete?}
    C -- no, EINTR --> D[reap finished children]
    D --> B
    C -- yes --> E[parser_parse]
    E -- error --> F[diagnostic on stderr]
    F --> B
    E -- ok --> G[process_execute_job]
    G -- single built-in,<br/>foreground --> H[run in the shell process]
    G -- everything else --> I[pipeline_run]
    I --> J[pipe 2 if needed]
    J --> K[fork]
    K -- child --> L[redirection_apply]
    L --> M{built-in?}
    M -- yes --> N[builtins_execute then _exit]
    M -- no --> O[execvp then _exit 127]
    K -- parent --> P[close unused pipe ends]
    P --> Q{background?}
    Q -- yes --> R[register the job, print [n] pid]
    Q -- no --> S[waitpid for every stage]
    S --> T[exit status of the last stage]
    H --> B
    R --> B
    T --> B
```

---

## 3. The interpreter loop

`shell_run()` in `src/shell.c` performs the following steps in every
iteration:

1. `process_reap_children()` collects the background jobs that have
   terminated and prints the `Done` notification for them.
2. The prompt is printed **only if standard input is a terminal**
   (`isatty(3)`) and only if the user has not started to type a line.
   This keeps the output of a scripted run free of prompt characters.
3. `linebuf_next()` reads one line with `read(2)`.
   * `EINTR` is not an error: the partially typed line is kept and the
     read continues, or the loop is restarted if nothing was typed yet.
   * `EOF` ends the loop.
4. A blank line is ignored.
5. `parser_parse()` converts the line into a `job_t`.  A parse error
   produces a diagnostic; the shell continues.
6. `process_execute_job()` runs the job and returns its exit status.
7. The status is stored in `state->last_status`; the loop continues.

The shell terminates when

* the `exit` built-in is executed (the requested status becomes the
  status of the shell), or
* standard input reaches end of file.  In that case the status of the
  last command becomes the status of the shell, which is the behaviour of
  a POSIX shell.

### 3.1 Why `read(2)` and not `getline(3)`

`getline(3)` cannot be resumed safely when it is interrupted by a
signal: the bytes that were already read may be lost.  The small reader
in `src/shell.c` keeps the bytes that were read before the interruption
and simply continues, and it also knows whether a partial line is
pending.  That allows the shell to redraw the prompt after a background
job has finished without destroying what the user has already typed.

---

## 4. Dispatch policy

`process_execute_job()` decides *where* a command runs:

| Situation                                        | Where it runs                     |
|--------------------------------------------------|-----------------------------------|
| one command, foreground, is a built-in            | in the shell process              |
| one command, foreground, is an external program   | in a child process                |
| any pipeline                                      | one child process per stage       |
| any job with `&`                                  | in child process(es)              |

A built-in that runs in the shell process can change the state of the
shell: `cd` changes the working directory and `exit` terminates the
shell.  The same built-in inside a pipeline or in a background job runs in
a child process, so its state changes are discarded.  This matches the
behaviour of a POSIX shell, where `echo x | cd /tmp` has no effect on
the parent.

---

## 5. Descriptor ownership

| Descriptor | Owner                                   | Lifetime                          |
|------------|-----------------------------------------|-----------------------------------|
| 0, 1, 2   | the shell                              | the whole run                     |
| `in_fd`    | the parent shell, between two forks    | closed after the next stage forks |
| `pfd[0]`   | the read end of a pipe                 | closed in the parent and in the child that does not need it |
| `pfd[1]`   | the write end of a pipe                | closed in the parent immediately after the fork |
| `redirect` | the child that applies the redirection | closed after `dup2(2)`            |

The rules that guarantee that a pipeline cannot deadlock are:

* the parent closes **both** ends of a pipe immediately after the fork,
  except for the read end that is handed to the next stage;
* the child closes the read end of the pipe it writes to and the write
  end of the pipe it reads from;
* all stages are forked before the shell waits for any of them, so the
  consumer starts while the producer is still running.

A detailed description is in [algorithms.md](algorithms.md).

---

## 6. State handling

`shell_state_t` (see `src/shell.h`) holds

* the table of background jobs (`bg_job_t`: process id, job number, the
  command text that is printed in the notification),
* the job number counter,
* the `exit_requested` flag and the exit status,
* the status of the last command.

There is no other mutable global state.  `signals.c` keeps two
`volatile sig_atomic_t` flags, which is the only state that is shared
between the signal handler and the rest of the program; both are
`volatile sig_atomic_t`, which is the only kind of object a signal
handler is allowed to modify.

---

## 7. Memory ownership

* The parser owns the strings of a `job_t`.  `job_free()` releases the
  whole tree, including the argument vectors, the redirection targets and
  the command array.
* `redirection_apply()` stores the descriptors it has to restore in the
  `redirect_save_t` structure that the caller owns.  `redirection_restore()`
  puts them back and closes the saved copies.
* The job table owns the `command_text` of every background job and
  releases it when the job is reaped or when the shell state is
  destroyed.
* Every allocation is checked; an allocation failure produces a
  diagnostic and aborts the affected command, never the shell.

The whole program was executed under AddressSanitizer and
UndefinedBehaviorSanitizer; see [limitations.md](limitations.md) for the
details of that verification.
