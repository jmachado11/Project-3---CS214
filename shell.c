#define _POSIX_C_SOURCE 200809L
#include "shell.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void reset_last_status(ShellState *state) {
    state->last_exit_code = 0;
    state->last_signal = 0;
    state->last_signaled = false;
}

void shell_state_init(ShellState *state, bool interactive) {
    state->interactive = interactive;
    state->should_exit = false;
    state->last_exit_code = 0;
    state->last_signal = 0;
    state->last_signaled = false;
    state->home = getenv("HOME");
}

void print_welcome(const ShellState *state) {
    if (state->interactive) {
        printf("Welcome to my shell!\n");
        fflush(stdout);
    }
}

void print_goodbye(const ShellState *state) {
    if (state->interactive) {
        printf("mysh: exiting\n");
        fflush(stdout);
    }
}

static void format_prompt_path(const ShellState *state, char *buffer, size_t size) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        snprintf(buffer, size, "?$ ");
        return;
    }

    if (state->home != NULL) {
        size_t home_len = strlen(state->home);
        if (strncmp(cwd, state->home, home_len) == 0 && (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
            if (cwd[home_len] == '\0') {
                snprintf(buffer, size, "~$ ");
            } else {
                snprintf(buffer, size, "~%s$ ", cwd + home_len);
            }
            return;
        }
    }

    snprintf(buffer, size, "%s$ ", cwd);
}

void print_prompt(const ShellState *state) {
    if (!state->interactive) {
        return;
    }

    char prompt[PATH_MAX + 4];
    format_prompt_path(state, prompt, sizeof(prompt));
    printf("%s", prompt);
    fflush(stdout);
}

void print_last_status(const ShellState *state) {
    if (!state->interactive) {
        return;
    }

    if (state->last_signaled) {
        const char *message = strsignal(state->last_signal);
        if (message == NULL) {
            message = "Unknown signal";
        }
        printf("Terminated by signal %d (%s)\n", state->last_signal, message);
        fflush(stdout);
    } else if (state->last_exit_code != 0) {
        printf("Exited with status %d\n", state->last_exit_code);
        fflush(stdout);
    }
}

int read_command_line(int fd, char **line_out) {
    size_t capacity = 128;
    size_t length = 0;
    char *buffer = malloc(capacity);
    if (buffer == NULL) {
        perror("malloc");
        return -1;
    }

    while (1) {
        char ch;
        ssize_t bytes_read = read(fd, &ch, READ_CHUNK_SIZE);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read");
            free(buffer);
            return -1;
        }

        if (bytes_read == 0) {
            if (length == 0) {
                free(buffer);
                *line_out = NULL;
                return 0;
            }
            break;
        }

        if (length + 1 >= capacity) {
            capacity *= 2;
            char *new_buffer = realloc(buffer, capacity);
            if (new_buffer == NULL) {
                perror("realloc");
                free(buffer);
                return -1;
            }
            buffer = new_buffer;
        }

        if (ch == '\n') {
            break;
        }

        buffer[length++] = ch;
    }

    buffer[length] = '\0';
    *line_out = buffer;
    return 1;
}

static void close_if_needed(int fd) {
    if (fd >= 0 && fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        close(fd);
    }
}

static int open_redirect_file(const char *path, int flags, mode_t mode) {
    int fd = open(path, flags, mode);
    if (fd < 0) {
        perror(path);
    }
    return fd;
}

static char *resolve_command_path(const char *name) {
    if (name == NULL) {
        return NULL;
    }

    if (strchr(name, '/') != NULL) {
        return xstrdup(name);
    }

    return search_program_path(name);
}

static void apply_child_streams(int input_fd, int output_fd) {
    if (input_fd >= 0 && input_fd != STDIN_FILENO) {
        if (dup2(input_fd, STDIN_FILENO) < 0) {
            perror("dup2");
            _exit(126);
        }
    }
    if (output_fd >= 0 && output_fd != STDOUT_FILENO) {
        if (dup2(output_fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            _exit(126);
        }
    }
}

static void execute_child_command(Command *cmd, ShellState *state, int input_fd, int output_fd) {
    apply_child_streams(input_fd, output_fd);
    close_if_needed(input_fd);
    close_if_needed(output_fd);

    if (is_builtin_name(cmd->argv[0])) {
        int status = run_builtin(cmd, state);
        _exit(status == SHELL_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    char *path = resolve_command_path(cmd->argv[0]);
    if (path == NULL) {
        fprintf(stderr, "%s: command not found\n", cmd->argv[0]);
        _exit(127);
    }

    execv(path, cmd->argv);
    perror(path);
    free(path);
    _exit(126);
}

static int run_single_external_or_builtin(Command *cmd, ShellState *state) {
    int stdin_backup = -1;
    int stdout_backup = -1;
    int input_fd = -1;
    int output_fd = -1;
    int status = SHELL_FAILURE;

    state->last_signaled = false;
    state->last_signal = 0;
    state->last_exit_code = SHELL_FAILURE;

    if (cmd->input_redirect != NULL) {
        input_fd = open_redirect_file(cmd->input_redirect, O_RDONLY, 0);
        if (input_fd < 0) {
            return SHELL_FAILURE;
        }
    }

    if (cmd->output_redirect != NULL) {
        output_fd = open_redirect_file(cmd->output_redirect, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP);
        if (output_fd < 0) {
            close_if_needed(input_fd);
            return SHELL_FAILURE;
        }
    }

    if (is_builtin_name(cmd->argv[0])) {
        if (cmd->input_redirect != NULL) {
            stdin_backup = dup(STDIN_FILENO);
            if (stdin_backup < 0 || dup2(input_fd, STDIN_FILENO) < 0) {
                perror("dup2");
                goto cleanup;
            }
        }
        if (cmd->output_redirect != NULL) {
            stdout_backup = dup(STDOUT_FILENO);
            if (stdout_backup < 0 || dup2(output_fd, STDOUT_FILENO) < 0) {
                perror("dup2");
                goto cleanup;
            }
        }

        status = run_builtin(cmd, state);
        state->last_exit_code = (status == SHELL_SUCCESS) ? 0 : 1;
        goto cleanup;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        goto cleanup;
    }

    if (pid == 0) {
        int child_input = input_fd;
        if (child_input < 0) {
            if (state->interactive) {
                child_input = STDIN_FILENO;
            } else {
                child_input = open("/dev/null", O_RDONLY);
                if (child_input < 0) {
                    perror("/dev/null");
                    _exit(126);
                }
            }
        }

        int child_output = (output_fd >= 0) ? output_fd : STDOUT_FILENO;
        execute_child_command(cmd, state, child_input, child_output);
    }

    int wait_status;
    if (waitpid(pid, &wait_status, 0) < 0) {
        perror("waitpid");
        goto cleanup;
    }

    if (WIFSIGNALED(wait_status)) {
        state->last_signaled = true;
        state->last_signal = WTERMSIG(wait_status);
        state->last_exit_code = 128 + WTERMSIG(wait_status);
        status = SHELL_FAILURE;
    } else if (WIFEXITED(wait_status)) {
        state->last_exit_code = WEXITSTATUS(wait_status);
        state->last_signaled = false;
        state->last_signal = 0;
        status = (state->last_exit_code == 0) ? SHELL_SUCCESS : SHELL_FAILURE;
    }

cleanup:
    if (stdout_backup >= 0) {
        dup2(stdout_backup, STDOUT_FILENO);
        close(stdout_backup);
    }
    if (stdin_backup >= 0) {
        dup2(stdin_backup, STDIN_FILENO);
        close(stdin_backup);
    }
    close_if_needed(input_fd);
    close_if_needed(output_fd);
    return status;
}

static int create_pipeline_children(Job *job, ShellState *state, pid_t *pids) {
    int prev_read = -1;

    for (int i = 0; i < job->count; i++) {
        int pipefd[2] = {-1, -1};
        if (i < job->count - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                close_if_needed(prev_read);
                return -1;
            }
        }

        int input_fd = -1;
        int output_fd = -1;

        if (i == 0) {
            if (job->commands[i].input_redirect != NULL) {
                input_fd = open_redirect_file(job->commands[i].input_redirect, O_RDONLY, 0);
                if (input_fd < 0) {
                    close_if_needed(prev_read);
                    close_if_needed(pipefd[0]);
                    close_if_needed(pipefd[1]);
                    return -1;
                }
            } else if (state->interactive) {
                input_fd = STDIN_FILENO;
            } else {
                input_fd = open("/dev/null", O_RDONLY);
                if (input_fd < 0) {
                    perror("/dev/null");
                    close_if_needed(prev_read);
                    close_if_needed(pipefd[0]);
                    close_if_needed(pipefd[1]);
                    return -1;
                }
            }
        } else {
            input_fd = prev_read;
        }

        if (i == job->count - 1) {
            if (job->commands[i].output_redirect != NULL) {
                output_fd = open_redirect_file(job->commands[i].output_redirect, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP);
                if (output_fd < 0) {
                    close_if_needed(input_fd);
                    close_if_needed(pipefd[0]);
                    close_if_needed(pipefd[1]);
                    return -1;
                }
            } else {
                output_fd = STDOUT_FILENO;
            }
        } else {
            output_fd = pipefd[1];
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close_if_needed(input_fd);
            close_if_needed(output_fd);
            close_if_needed(pipefd[0]);
            close_if_needed(pipefd[1]);
            return -1;
        }

        if (pid == 0) {
            close_if_needed(pipefd[0]);
            execute_child_command(&job->commands[i], state, input_fd, output_fd);
        }

        pids[i] = pid;
        close_if_needed(output_fd);
        if (i > 0) {
            close_if_needed(input_fd);
        } else if (!state->interactive && job->commands[i].input_redirect == NULL) {
            close_if_needed(input_fd);
        } else if (job->commands[i].input_redirect != NULL) {
            close_if_needed(input_fd);
        }
        prev_read = pipefd[0];
    }

    close_if_needed(prev_read);
    return 0;
}

static int run_pipeline(Job *job, ShellState *state) {
    pid_t *pids = calloc((size_t)job->count, sizeof(pid_t));
    if (pids == NULL) {
        perror("calloc");
        return SHELL_FAILURE;
    }

    if (create_pipeline_children(job, state, pids) != 0) {
        state->last_signaled = false;
        state->last_signal = 0;
        state->last_exit_code = SHELL_FAILURE;
        free(pids);
        return SHELL_FAILURE;
    }

    int overall = SHELL_FAILURE;
    for (int i = 0; i < job->count; i++) {
        int wait_status;
        if (waitpid(pids[i], &wait_status, 0) < 0) {
            perror("waitpid");
            continue;
        }

        if (i == job->count - 1) {
            if (WIFSIGNALED(wait_status)) {
                state->last_signaled = true;
                state->last_signal = WTERMSIG(wait_status);
                state->last_exit_code = 128 + WTERMSIG(wait_status);
                overall = SHELL_FAILURE;
            } else if (WIFEXITED(wait_status)) {
                state->last_signaled = false;
                state->last_signal = 0;
                state->last_exit_code = WEXITSTATUS(wait_status);
                overall = (state->last_exit_code == 0) ? SHELL_SUCCESS : SHELL_FAILURE;
            }
        }
    }

    free(pids);
    return overall;
}

int run_job(Job *job, ShellState *state) {
    reset_last_status(state);

    if (job == NULL || job->count == 0) {
        return SHELL_SUCCESS;
    }

    if (job->count == 1) {
        int status = run_single_external_or_builtin(&job->commands[0], state);
        if (strcmp(job->commands[0].argv[0], "exit") == 0 && status == SHELL_SUCCESS) {
            state->should_exit = true;
        }
        return status;
    }

    int status = run_pipeline(job, state);
    if (job->has_exit) {
        state->should_exit = true;
    }
    return status;
}

int run_shell(int input_fd, ShellState *state) {
    print_welcome(state);

    while (!state->should_exit) {
        print_last_status(state);
        print_prompt(state);

        char *line = NULL;
        int read_status = read_command_line(input_fd, &line);
        if (read_status < 0) {
            return EXIT_SUCCESS;
        }
        if (read_status == 0) {
            break;
        }

        Job job;
        int parse_status = parse_job(line, &job);
        free(line);

        if (parse_status == SHELL_SYNTAX_ERROR) {
            fprintf(stderr, "syntax error\n");
            state->last_exit_code = SHELL_SYNTAX_ERROR;
            state->last_signaled = false;
            state->last_signal = 0;
            continue;
        }
        if (parse_status != SHELL_SUCCESS) {
            state->last_exit_code = SHELL_FAILURE;
            state->last_signaled = false;
            state->last_signal = 0;
            continue;
        }

        run_job(&job, state);
        free_job(&job);
    }

    print_goodbye(state);
    return EXIT_SUCCESS;
}
