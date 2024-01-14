/*
 * File: smallsh.c
 * Author: Jack Huang
 * Created: 2023-05-02
 * Updated: 2023-05-19
 * Description: A simple shell program that supports built-in commands cd and exit, and non-built-in commands.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

/*
 * Global Variables:
 * words: An array of words read from input
 * int_buf: A buffer for converting integer to string
 * bg_flag: A flag for background process
 * ppgid: Parent process group id
 */
char *words[MAX_WORDS];
char int_buf[21];
int bg_flag = 0;
pid_t ppgid;

/*
 * Sinal Handling
 * SIGTSTP_default: Default SIGTSTP action
 * SIGTSTP_action: New SIGTSTP action
 * SIGINT_default: Default SIGINT action
 * SIGINT_action: New SIGINT action
 */
struct sigaction SIGTSTP_default = {0};
struct sigaction SIGTSTP_action = {0};
struct sigaction SIGINT_default = {0};
struct sigaction SIGINT_action = {0};

void SIGTSTP_setup();

void SIGINT_setup();

void SIGINT_handler(int signo);

void print_prompt();

/* Words processing */
size_t wordsplit(char const *line);

char *expand(char const *word);

size_t parse_command(size_t nwords, char **argv);

/* Command processing */
void builtin_cd(char **argv, size_t argc);

void builtin_exit(char **argv, size_t argc);

void execute_cmds(char **words_argv, size_t words_argc);

void execute_nonbuiltin_cmds(char **argv);

int bg_handler();

int main(int argc, char *argv[])
{
smallsh:;
    FILE *input = stdin;
    char *input_fn = "(stdin)";
    if (argc == 2)
    {
        input_fn = argv[1];
        input = fopen(input_fn, "re");
        if (!input)
            err(1, "%s", input_fn);
    }
    else if (argc > 2)
    {
        errx(1, "too many arguments");
    }

    // Initialize line and n for getline outside the loop
    char *line = NULL;
    size_t n = 0;

    ppgid = getpgrp();

    // Initialize $$, $?, and $$
    sprintf(int_buf, "%d", getpid());
    setenv("$", int_buf, 1);
    setenv("?", "0", 1);
    setenv("!", "", 1);

    for (;;)
    {
        // Setup signal handler
        SIGTSTP_setup();
        SIGINT_setup();

    /* Input */
    prompt:;

        // Manage background process
        if (bg_handler())
            errx(1, "bg_handler");

        // Input is stdin: Interactive mode -> Print prompt
        if (input == stdin)
            print_prompt();

        // Otherwise, read from a file: Non-interactive mode

        // Read a line from input
        ssize_t line_len = getline(&line, &n, input);
        if (line_len < 0)
        {
            // Handle EOF
            if (feof(input))
                return 0;
            // Handle EINTR
            if (errno == EINTR)
            {
                clearerr(stdin);
                goto smallsh;
            }
            err(1, "getline %s", input_fn);
        }

        // Done reading, set SIGINT to SIG_IGN
        SIGINT_action.sa_handler = SIG_IGN;
        sigaction(SIGINT, &SIGINT_action, NULL);

        /* Word Splitting */
        size_t nwords = wordsplit(line);
        if (nwords == 0)
            goto prompt;

        /* Expansion */
        for (size_t i = 0; i < nwords; ++i)
        {
            char *exp_word = expand(words[i]);
            free(words[i]);
            words[i] = exp_word;
        }

        /* Parsing */
        char **words_argv = malloc(sizeof(*words) * (nwords + 1));
        if (!words_argv)
            err(1, "malloc");
        size_t words_argc = parse_command(nwords, words_argv);

        /* Execution */
        execute_cmds(words_argv, words_argc);

        // Clean up
        for (size_t i = 0; i < nwords; ++i)
            words[i] = 0;
        free(words_argv);
    }
}

/*
 * Setup SIGTSTP signal handler:
 * Store default SIGTSTP action in SIGTSTP_default
 * Set SIGTSTP action to SIG_IGN
 */
void SIGTSTP_setup()
{
    sigaction(SIGTSTP, NULL, &SIGTSTP_default);

    SIGTSTP_action.sa_handler = SIG_IGN;
    sigfillset(&SIGTSTP_action.sa_mask);
    sigaction(SIGTSTP, &SIGTSTP_action, &SIGTSTP_default);
}

/*
 * Setup SIGINT signal handler:
 * Store default SIGINT action in SIGINT_default
 * Set SIGINT action to SIGINT_handler
 */
void SIGINT_setup()
{
    // Save default SIGINT action
    sigaction(SIGINT, NULL, &SIGINT_default);
    // Add new SIGINT handler
    SIGINT_action.sa_handler = SIGINT_handler;
    sigfillset(&SIGINT_action.sa_mask);
    sigaction(SIGINT, &SIGINT_action, &SIGINT_default);
}

/*
 * SIGINT_handler:
 * Print a new line to stderr
 */
void SIGINT_handler(int signo)
{
    fprintf(stderr, "\n");
}

/*
 * Print prompt: ps1
 */
void print_prompt()
{
    char *ps1 = getenv("PS1");
    if (!ps1)
        ps1 = "";
    fprintf(stderr, "%s", ps1);
}

char *words[MAX_WORDS] = {0};

/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line)
{
    size_t wlen = 0;
    size_t wind = 0;

    char const *c = line;
    for (; *c && isspace(*c); ++c)
        ; /* discard leading space */

    for (; *c;)
    {
        if (wind == MAX_WORDS)
            break;
        /* read a word */
        if (*c == '#')
            break;
        for (; *c && !isspace(*c); ++c)
        {
            if (*c == '\\')
                ++c;
            void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
            if (!tmp)
                err(1, "realloc");
            words[wind] = tmp;
            words[wind][wlen++] = *c;
            words[wind][wlen] = '\0';
        }
        ++wind;
        wlen = 0;
        for (; *c && isspace(*c); ++c)
            ;
    }
    return wind;
}

/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char param_scan(char const *word, char const **start, char const **end)
{
    static char const *prev;
    if (!word)
        word = prev;

    char ret = 0;
    *start = 0;
    *end = 0;
    for (char const *s = word; *s && !ret; ++s)
    {
        s = strchr(s, '$');
        if (!s)
            break;
        switch (s[1])
        {
        case '$':
        case '!':
        case '?':
            ret = s[1];
            *start = s;
            *end = s + 2;
            break;
        case '{':;
            char *e = strchr(s + 2, '}');
            if (e)
            {
                ret = s[1];
                *start = s;
                *end = e + 1;
            }
            break;
        }
    }
    prev = *end;
    return ret;
}

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *build_str(char const *start, char const *end)
{
    static size_t base_len = 0;
    static char *base = 0;

    if (!start)
    {
        /* Reset; new base string, return old one */
        char *ret = base;
        base = NULL;
        base_len = 0;
        return ret;
    }
    /* Append [start, end) to base string
     * If end is NULL, append whole start string to base string.
     * Returns a newly allocated string that the caller must free.
     */
    size_t n = end ? end - start : strlen(start);
    size_t newsize = sizeof *base * (base_len + n + 1);
    void *tmp = realloc(base, newsize);
    if (!tmp)
        err(1, "realloc");
    base = tmp;
    memcpy(base + base_len, start, n);
    base_len += n;
    base[base_len] = '\0';

    return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string
 * Returns a newly allocated string that the caller must free
 */
char *expand(char const *word)
{
    char const *pos = word;
    char const *start, *end;
    char c = param_scan(pos, &start, &end);
    build_str(NULL, NULL);
    build_str(pos, start);

    while (c)
    {
        switch (c)
        {
        case '$':
        {
            // Process ID
            char *pid = getenv("$");
            if (!pid)
                build_str("", NULL);
            else
                build_str(pid, NULL);
            break;
        }
        case '!':
        {
            // Background PID most recent bg process, default "" if no background process ID available
            char *pid = getenv("!");
            if (!pid)
                build_str("", NULL);
            else
                build_str(pid, NULL);
            break;
        }

        case '?':
        {
            // Exit status of the last fg command, 0 by default
            char *status = getenv("?");
            if (!status)
                build_str("0", NULL);
            else
                build_str(status, NULL);
            break;
        }

        case '{':
        {
            // Environment variable
            char *env_var = malloc(sizeof(char) * (end - start + 1));
            if (!env_var)
                err(1, "malloc");
            strncpy(env_var, start + 2, end - start - 3);
            env_var[end - start - 3] = '\0';
            char *env_val = getenv(env_var);
            if (env_val)
                build_str(env_val, NULL);
            free(env_var);
            break;
        }
        }
        pos = end;
        c = param_scan(pos, &start, &end);
        build_str(pos, start);
    }
    return build_str(start, NULL);
}

/*
 * Copy the pointers from words to args, skipping over redirection characters and &.
 */
size_t parse_command(size_t nwords, char **words_argv)
{
    size_t words_argc = 0;
    for (size_t i = 0; i < nwords; ++i)
    {
        // Skip over redirection characters
        if (strcmp(words[i], ">") == 0 || strcmp(words[i], "<") == 0 || strcmp(words[i], ">>") == 0)
        {
            ++i;
            continue;
        }
        words_argv[words_argc] = words[i];
        ++words_argc;
    }

    // Check if bg process
    if (strcmp(words_argv[words_argc - 1], "&") == 0)
    {
        bg_flag = 1;
        words_argv[words_argc - 1] = NULL;
        --words_argc;
    }
    else
    {
        bg_flag = 0;
        words_argv[words_argc] = NULL;
    }

    return words_argc;
}

/*
 * Built-in commands: cd
 */
void builtin_cd(char **argv, size_t argc)
{
    if (argc == 1)
    {
        // No argument, cd to home directory
        char *home = getenv("HOME");
        if (home)
            chdir(home);
        else
        {
            fprintf(stderr, "smallsh: cd: HOME not set\n");
            setenv("?", "1", 1);
        }
    }
    else if (argc > 2)
    {
        // More than one argument
        fprintf(stderr, "smallsh: cd: too many arguments\n");
        setenv("?", "1", 1);
    }
    else
    {
        // Just one argument
        if (chdir(argv[1]) != 0)
        {
            fprintf(stderr, "smalssh: chdir failed\n");
            setenv("?", "1", 1);
        }
    }
}

/*
 * Built-in commands: exit
 */
void builtin_exit(char **argv, size_t argc)
{
    if (argc > 2)
    {
        fprintf(stderr, "smallsh: exit: too many arguments\n");
        setenv("?", "1", 1);
    }
    int exit_code;
    if (argc == 2)
    {
        char *endptr;
        exit_code = strtol(argv[1], &endptr, 0);
        if (*endptr != '\0')
        {
            fprintf(stderr, "smallsh: exit: %s: integer argument required\n", argv[1]);
            setenv("?", "1", 1);
        }
    }
    else
    {
        // If no argument is provided, use $?
        char *status = getenv("?");
        if (!status)
            exit_code = 0;
        else
            exit_code = strtol(status, NULL, 0);
    }
    exit(exit_code);
}

/*
 * Execute commands
 * If the command is a built-in command, execute it directly.
 * Otherwise, execute it as a non-built-in command.
 */
void execute_cmds(char **words_argv, size_t words_argc)
{

    if (strcmp(words_argv[0], "cd") == 0)
        builtin_cd(words_argv, words_argc);
    else if (strcmp(words[0], "exit") == 0)
        builtin_exit(words_argv, words_argc);
    else
        execute_nonbuiltin_cmds(words_argv);
}

/* Execute non-built-in commands. */
void execute_nonbuiltin_cmds(char **words_argv)
{
    pid_t pid = fork();
    int status;
    int fd = STDIN_FILENO;

    switch (pid)
    {
    case -1:
        fprintf(stderr, "Fork: %s", words_argv[0]);
        exit(EXIT_FAILURE);
        break;

    case 0:
        /* Child process */

        // Set to default signal handling
        sigaction(SIGINT, &SIGINT_default, NULL);
        sigaction(SIGTSTP, &SIGTSTP_default, NULL);

        // Handle redirection in words
        for (int i = 0; words[i] != NULL; i++)
        {
            if (strcmp(words[i], ">") == 0)
            {
                if (!words[i + 1])
                    err(1, "no file for redirection output");
                fd = open(words[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0777);
                if (fd < 0)
                    err(1, "open file for redirection output");
                if (dup2(fd, fileno(stdout)) < 0)
                    err(1, "dup2");
                close(fd);
            }
            else if (strcmp(words[i], "<") == 0)
            {
                if (!words[i + 1])
                    err(1, "no file for redirection output");
                fd = open(words[i + 1], O_RDONLY);
                if (fd < 0)
                    err(1, "open file for redirection input");
                if (dup2(fd, fileno(stdin)) < 0)
                    err(1, "dup2");
                close(fd);
            }
            else if (strcmp(words[i], ">>") == 0)
            {
                if (!words[i + 1])
                    err(1, "no file for redirection output");
                fd = open(words[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0777);
                if (fd < 0)
                    err(1, "open file for redirection output append");
                if (dup2(fd, fileno(stdout)) < 0)
                    err(1, "dup2");
                close(fd);
            }
        }

        // Check if contain /
        if (strchr(words_argv[0], '/') != NULL)
        {
            // Contain /, use the path
            execv(words_argv[0], words_argv);
            perror("execv");
            exit(EXIT_FAILURE);
        }
        else
        {
            // Not contain /, search the PATH
            execvp(words_argv[0], words_argv);
            perror("execvp");
            exit(EXIT_FAILURE);
        }
        break;

    default:
        /* Parent process */
        if (bg_flag != 0)
        {
            // Background process, do not wait for it to finish
            waitpid(pid, &status, WNOHANG | WUNTRACED);
            // Set $! to the pid of the last background process
            sprintf(int_buf, "%d", pid);
            setenv("!", int_buf, 1);
        }
        else
        {
            // Foreground process, wait for it to finish or stop
            pid = waitpid(pid, &status, WUNTRACED);

            if (WIFSIGNALED(status))
            {
                // Terminated by a signal
                status = 128 + WTERMSIG(status);
                sprintf(int_buf, "%d", status);
                setenv("?", int_buf, 1);
            }
            else if (WIFSTOPPED(status))
            {
                // Stopped by a signal
                fprintf(stderr, "Child process %d stopped. Continuing.\n", pid);
                if (kill(pid, SIGCONT) < 0)
                {
                    perror("kill");
                    exit(EXIT_FAILURE);
                }
                sprintf(int_buf, "%d", pid);
                setenv("!", int_buf, 1);
            }
            else if (WIFEXITED(status))
            {
                // Exited normally
                status = WEXITSTATUS(status);
                sprintf(int_buf, "%d", status);
                setenv("?", int_buf, 1);
            }
        }
        break;
    }
}

/*
 * Check un-waited background process
 * If a background process has finished, print a message
 */
int bg_handler()
{
    pid_t pid;
    int status;
    int signal;

    // Check if any background process has finished
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
    {
        pid_t pgid = getpgrp();
        if (pgid == ppgid)
        {
            if (WIFEXITED(status))
            {
                // Exited
                status = WEXITSTATUS(status);
                fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t)pid, status);
                fflush(stderr);
            }
            else if (WIFSIGNALED(status))
            {
                // Terminated by signal
                signal = WTERMSIG(status);
                fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t)pid, signal);
                fflush(stderr);
            }
            else if (WIFSTOPPED(status))
            {
                // Stopped by signal
                if (kill(pid, SIGCONT) < 0)
                {
                    perror("kill");
                    exit(EXIT_FAILURE);
                }
                fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t)pid);
                fflush(stderr);
            }
        }
    }
    return 0;
}