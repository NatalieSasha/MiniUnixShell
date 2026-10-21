# Parser grammar

This document specifies the shell language that `minishell` accepts.  The
grammar is implemented in `src/parser.c`; the two phases of the
implementation (lexing and parsing) correspond to the two sections below.

The grammar is a deliberately small subset of the POSIX shell language.  It
is **not** Bash compatible and it does not attempt to be.  Every feature
that is not part of the grammar is rejected with a diagnostic instead of
being silently ignored, so that the shell never executes a command that
differs from what the user wrote.

---

## 1. Notation

The grammar uses the following notation (adapted from the POSIX shell
specification, IEEE Std 1003.1, section 2.10):

| Notation | Meaning                                                       |
|----------|---------------------------------------------------------------|
| `A`      | a terminal, matched literally                                |
| `A B`    | `A` followed by `B`                                           |
| `A \| B` | `A` or `B`                                                    |
| `[ A ]`  | optional, zero or one occurrence of `A`                       |
| `A*`     | zero or more occurrences of `A`                               |
| `A+`     | one or more occurrences of `A`                                 |
| `( A )`  | grouping                                                     |

---

## 2. Lexical structure (the tokeniser)

```
token          := WORD
                | PIPE            /* |  */
                | REDIR_IN        /* <  */
                | REDIR_OUT       /* >  */
                | REDIR_APPEND    /* >> */
                | AMP             /* &  */
                | END_OF_INPUT

WORD           := word_part+

word_part      := LITERAL_CHAR
                | SINGLE_QUOTED
                | DOUBLE_QUOTED
                | ESCAPED

SINGLE_QUOTED  := "'" { any character except "'" } "'"
DOUBLE_QUOTED  := '"' { double_quoted_char } '"'
ESCAPED        := "\" any_character
```

### 2.1 Word boundaries

A word is a maximal sequence of characters that

* contains no unquoted blank (`space`, `tab`, `newline`, `carriage
  return`, `vertical tab`, `form feed`),
* contains no unquoted operator.

The operators recognised by the tokeniser are `|`, `<`, `>`, `>>` and `&`.
A word may contain quoted sections, and a quoted section may be adjacent to
unquoted text; the result is still a single word:

```
echo a"b c"d      ->  argv[1] == "ab cd"
```

### 2.2 Quoting rules

| Context        | Behaviour                                                        |
|----------------|------------------------------------------------------------------|
| `'...'`        | Every character is literal, including blanks and `"`. A single quote cannot be escaped inside single quotes. |
| `"..."`        | Blanks are literal. A backslash escapes only `"`, `\` and `$`. For every other character the backslash is kept literally (POSIX rule 2 of section 2.2). |
| `\c` outside quotes | `c` becomes a literal character, whatever it is. A backslash at the end of the line is kept as a literal backslash (POSIX leaves this undefined). |
| `""` or `''`   | An empty word. The argument exists and is the empty string.      |

### 2.3 Characters that are rejected

The following characters are **not** part of the accepted language.  The
parser reports `unsupported shell syntax` and executes nothing:

| Construct                | Example      | Reason                                    |
|--------------------------|--------------|-------------------------------------------|
| variable expansion       | `$HOME`      | not implemented                            |
| command substitution     | `` `date` `` | not implemented                            |
| command substitution     | `$(date)`    | not implemented                            |
| pattern matching (glob)  | `*.c`        | not implemented                            |
| here-document            | `<< EOF`     | not implemented                            |
| command separator        | `;`          | not implemented                            |
| subshell / grouping      | `(a; b)`     | not implemented                            |

Pattern characters are rejected only **outside** quotes.  `echo "*.c"` is
accepted and passes the literal string `*.c` to the command, exactly like
a POSIX shell does for a quoted pattern.

---

## 3. Grammar (the parser)

```
job          := pipeline [ AMP ] END_OF_INPUT
pipeline     := command { PIPE command }
command      := { WORD | redirection }
redirection  := REDIR_IN  WORD
              | REDIR_OUT WORD
              | REDIR_APPEND WORD
```

### 3.1 Rules

1. **A pipeline contains at least one command.**  `|` may not be the first
   or the last token of a pipeline.
2. **Every command contains at least one word.**  A command without a
   word (`cat | > file`) is rejected with `empty command in pipeline`.
   This is slightly stricter than POSIX, which allows a command without
   arguments, and it keeps the grammar unambiguous.
3. **A redirection target must be a word.**  `echo >` is rejected with
   `redirection operator is missing a file name`.
4. **Redirections may appear before, between or after the words** of a
   command.  `> f ls` and `ls > f` are equivalent.
5. **The last redirection of a kind wins.**  `ls > a > b` redirects to `b`,
   because the second assignment overwrites the first.
6. **`&` may only appear at the end of a job.**  `echo a & echo b` is
   rejected with `unexpected token`; the shell does not silently split the
   line into two commands.
7. **At most one job per line.**  There is no command separator, so
   `echo a; echo b` is rejected.

### 3.2 Precedence of redirection and pipelines

Redirection binds **more tightly** than the pipe operator:

```
cat < in.txt | grep error > out.txt
```

is interpreted as

```
( cat < in.txt ) | ( grep error > out.txt )
```

That is, the input redirection belongs to the first stage, the output
redirection to the second.  A redirection that is attached to a stage
overrides the pipe for that stage, because the redirection is applied
*after* the pipe descriptors have been installed.

### 3.3 Exit status of a pipeline

The exit status of a pipeline is the exit status of its **last** command.
The status of the earlier stages is discarded.  This is the behaviour of
the POSIX shell and it is what `minishell` implements.

### 3.4 Background jobs

A job that ends with `&` is executed in the background:

```
sleep 10 &
```

The shell forks the job, records the process id in its job table, prints
`[1] 4711` and returns to the prompt.  The exit status of a background
job is not reported to the shell's exit status.

---

## 4. Static syntax tree

```
job_t
 ├── background : int
 └── commands[] : command_t          (one entry per pipeline stage)
      ├── argv[] : char *            (NULL terminated)
      ├── argc   : size_t
      ├── input_file  : char *       (NULL if there is no '<')
      ├── output_file : char *       (NULL if there is no '>' or '>>')
      └── output_append : int        (1 if the operator was '>>')
```

The data structures are defined in `src/parser.h`; the allocation and
ownership rules are documented there and in `src/parser.c`.

---

## 5. Limits

| Limit                        | Value              | Constant                    |
|------------------------------|--------------------|-----------------------------|
| maximum length of a line     | 65536 bytes        | `PARSER_MAX_LINE_LENGTH`    |
| maximum words per command    | 1024               | `PARSER_MAX_WORDS`          |
| maximum buffered line length | 1 MiB              | `LINE_BUFFER_LIMIT`         |

Exceeding the line length limit produces the diagnostic `command line is
too long`; exceeding the argument limit produces `too many arguments`.
Both limits exist to bound the memory that a single command line can
consume.
