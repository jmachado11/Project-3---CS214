#define _POSIX_C_SOURCE 200809L
#include "shell.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool is_builtin_name(const char *name) {
    return name != NULL && (
        strcmp(name, "cd") == 0 ||
        strcmp(name, "pwd") == 0 ||
        strcmp(name, "which") == 0 ||
        strcmp(name, "exit") == 0);
}

static int builtin_cd(Command *cmd, ShellState *state) {
    const char *target = NULL;

    if (cmd->argc > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return SHELL_FAILURE;
    }

    if (cmd->argc == 1) {
        target = state->home;
    } else {
        target = cmd->argv[1];
    }

    if (target == NULL) {
        fprintf(stderr, "cd: HOME not set\n");
        return SHELL_FAILURE;
    }

    if (chdir(target) != 0) {
        perror("cd");
        return SHELL_FAILURE;
    }

    return SHELL_SUCCESS;
}

static int builtin_pwd(Command *cmd) {
    char cwd[PATH_MAX];

    if (cmd->argc != 1) {
        return SHELL_FAILURE;
    }

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
        return SHELL_FAILURE;
    }

    printf("%s\n", cwd);
    fflush(stdout);
    return SHELL_SUCCESS;
}

static int builtin_which(Command *cmd) {
    if (cmd->argc != 2) {
        return SHELL_FAILURE;
    }

    if (is_builtin_name(cmd->argv[1])) {
        return SHELL_FAILURE;
    }

    if (strchr(cmd->argv[1], '/') != NULL) {
        if (access(cmd->argv[1], X_OK) == 0) {
            printf("%s\n", cmd->argv[1]);
            fflush(stdout);
            return SHELL_SUCCESS;
        }
        return SHELL_FAILURE;
    }

    char *path = search_program_path(cmd->argv[1]);
    if (path == NULL) {
        return SHELL_FAILURE;
    }

    printf("%s\n", path);
    fflush(stdout);
    free(path);
    return SHELL_SUCCESS;
}

static int builtin_exit(Command *cmd, ShellState *state) {
    if (cmd->argc != 1) {
        return SHELL_FAILURE;
    }

    state->should_exit = true;
    return SHELL_SUCCESS;
}

int run_builtin(Command *cmd, ShellState *state) {
    if (cmd == NULL || cmd->argc == 0) {
        return SHELL_FAILURE;
    }

    if (strcmp(cmd->argv[0], "cd") == 0) {
        return builtin_cd(cmd, state);
    }
    if (strcmp(cmd->argv[0], "pwd") == 0) {
        return builtin_pwd(cmd);
    }
    if (strcmp(cmd->argv[0], "which") == 0) {
        return builtin_which(cmd);
    }
    if (strcmp(cmd->argv[0], "exit") == 0) {
        return builtin_exit(cmd, state);
    }

    return SHELL_FAILURE;
}
