# ShellLite: A Simple Shell Implementation

ShellLite is a simple shell implementation that provides a command-line interface with basic shell functionalities.

The program supports:

- Interactive and non-interactive modes
- Parsing command-line input
- Implementing parameter expansion
- Executing built-in and non-built-in commands
- Handling signals such as `SIGINT` and `SIGTSTP`.

## Introduction

ShellLite performs the following tasks:

1. Prints an interactive input prompt.
2. Parses command-line input into semantic tokens.
3. Implements parameter expansion, including shell special parameters `$$`, `$?`, and `$!`, and generic parameters as `${parameter}`.
4. Implements two shell built-in commands: `exit` and `cd`.
5. Executes non-built-in commands using the appropriate `EXEC(3)` function.
   - Implements redirection operators `<`, `>`, and `>>`.
   - Implements the `&` operator to run commands in the background.
6. Implements custom behavior for `SIGINT` and `SIGTSTP` signals.

## Program Functionality

The following steps are performed in an infinite loop:

1. **Input**
2. **Word Splitting**
3. **Expansion**
4. **Parsing**
5. **Execution**
6. **Waiting**

The loop is exited when:

- The built-in `exit` command is executed.

  or

- The end of input is reached.
  - End of input will be interpreted as an implied `exit $?` command (i.e., ShellLite exits with the status of the last foreground command as its own exit status).

ShellLite can be invoked with:

- No arguments, in which case it reads commands from `stdin`.
- With one argument, in which case the argument specifies the name of a file (script) to read commands from.
  - These will be referred to as interactive and non-interactive mode, respectively.
  - In non-interactive mode, ShellLite opens its file/script with the `CLOEXEC` flag, so that child processes do not inherit the open file descriptor.
- Errors result in informative messages printed to stderr, and processing stops.

## Input

### Managing Background Processes

- ShellLite checks for any un-waited-for background processes in the same process group ID as ShellLite and prints informative messages for each:
  - If exited: `"Child process %d done. Exit status %d.\n", <pid>, <exit status>`
  - If signaled: `"Child process %d done. Signaled %d.\n", <pid>, <signal number>`
- If a child process is stopped, ShellLite sends it the `SIGCONT` signal and prints: `"Child process %d stopped. Continuing.\n", <pid>`

### The Prompt

- In interactive mode, ShellLite prints a prompt to stderr by expanding the `PS1` parameter.
  - Otherwise, nothing is printed.
- PS1 stands for "Prompt String 1" and is used before each new command line.

### Reading a Line of Input

- In interactive mode, ShellLite reads a line of input from stdin.
- In non-interactive mode, ShellLite reads from the specified script file.
- Interruptions during interactive mode result in a newline, a new command prompt, and resumed input reading.

## Word Splitting

- The line of input is split into words delimited by whitespace characters (ISSPACE(3)), including <newline>.
- The `\` character removes whitespace and includes the next character in the current word.
- A `#` comment character at the beginning of a new word removes it and any characters following it.

## Expansion

- Recognizes and expands occurrences of `$$`, `$?`, `$!`, and `${parameter}` within each word.
- Unset expanded environment variables result in an empty string.
- `$?` defaults to 0, and `$!` defaults to an empty string.
- Expansion is not recursive and is performed in a single forward pass through each word.

## Parsing

- The words are parsed syntactically into tokens.
- If the last word is `&`, it is interpreted as the _background operator_.
- Any occurrence of the words `>`, `<`, or `>>` will be interpreted as redirection operators (write, read, append).

## Execution

- If no command word is present, ShellLite silently returns to step 1 and prints a new prompt message.
- Built-in commands like `exit` or `cd` execute their respective procedures.
- Non-built-in commands are executed in a new child process.
- Redirection operators (`<`, `>`, `>>`) are handled, and the child process may exit with an informative error message on failure.

## Waiting

- Built-in commands will skip this step.
- If a non-built-in command was executed without the background operator `&`, the parent ShellLite process performs a blocking wait on the foreground child process.
- Otherwise, the child process runs in the background, and the parent ShellLite process does not wait on it.
- `$?` shell variable is set to the exit status of the waited-for command.
  - If the waited-for command is terminated by a signal, `$?` is set to `128 + [n]` where `[n]` is the signal number.
  - If waiting on a foreground child process is stopped, ShellLite sends `SIGCONT` and prints: `"Child process %d stopped. Continuing.\n", <pid>`
- `$!` is updated to the pid of the child process.

## Signal Handling

- ShellLite performs signal handling of the `SIGINT` and `SIGTSTP` signals in interactive mode.
  - The `SIGTSTP` signal is ignored.
  - The `SIGINT` signal is ignored except when reading a line of input, during which time it is registered to a signal handler that does nothing.
- In non-interactive mode, ShellLite does not handle these signals specially.

---

_Note: This README provides an overview of the functionalities and behaviors of ShellLite. For detailed implementation and usage, please refer to the provided source code._
