#define _POSIX_C_SOURCE 200809L
#include "shell.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void command_init(Command *cmd) {
    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->input_redirect = NULL;
    cmd->output_redirect = NULL;
}

static void command_free(Command *cmd) {
    if (cmd == NULL) {
        return;
    }

    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
    free(cmd->argv);
    free(cmd->input_redirect);
    free(cmd->output_redirect);

    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->input_redirect = NULL;
    cmd->output_redirect = NULL;
}

static int append_command(Job *job, Command *cmd) {
    Command *new_commands = realloc(job->commands, (size_t)(job->count + 1) * sizeof(Command));
    if (new_commands == NULL) {
        perror("realloc");
        return -1;
    }

    job->commands = new_commands;
    job->commands[job->count++] = *cmd;
    return 0;
}

static char *next_token(const char **cursor) {
    const char *p = *cursor;

    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '\0' || *p == '#') {
        *cursor = p;
        return NULL;
    }

    if (*p == '<' || *p == '>' || *p == '|') {
        char *token = malloc(2);
        if (token == NULL) {
            perror("malloc");
            return NULL;
        }
        token[0] = *p;
        token[1] = '\0';
        p++;
        *cursor = p;
        return token;
    }

    const char *start = p;
    while (*p != '\0' && !isspace((unsigned char)*p) && *p != '<' && *p != '>' && *p != '|' && *p != '#') {
        p++;
    }

    size_t len = (size_t)(p - start);
    char *token = malloc(len + 1);
    if (token == NULL) {
        perror("malloc");
        return NULL;
    }
    memcpy(token, start, len);
    token[len] = '\0';
    *cursor = p;
    return token;
}

int parse_job(const char *line, Job *job) {
    job->commands = NULL;
    job->count = 0;
    job->has_exit = false;

    StringList args;
    string_list_init(&args);
    Command current;
    command_init(&current);

    const char *cursor = line;
    char *token;
    bool have_anything = false;

    while ((token = next_token(&cursor)) != NULL) {
        have_anything = true;

        if (strcmp(token, "|") == 0) {
            free(token);
            if (args.size == 0) {
                string_list_free(&args, true);
                command_free(&current);
                free_job(job);
                return SHELL_SYNTAX_ERROR;
            }

            current.argc = (int)args.size;
            current.argv = string_list_to_argv(&args);
            if (current.argv == NULL) {
                string_list_free(&args, true);
                command_free(&current);
                free_job(job);
                return SHELL_FAILURE;
            }
            args.items = NULL;
            args.size = 0;
            args.capacity = 0;

            if (current.argv[0] != NULL && strcmp(current.argv[0], "exit") == 0) {
                job->has_exit = true;
            }

            if (current.input_redirect != NULL || current.output_redirect != NULL) {
                command_free(&current);
                free_job(job);
                return SHELL_SYNTAX_ERROR;
            }

            if (current.argv[0] != NULL && strcmp(current.argv[0], "exit") == 0) {
        job->has_exit = true;
    }

    if (append_command(job, &current) != 0) {
                command_free(&current);
                free_job(job);
                return SHELL_FAILURE;
            }
            command_init(&current);
            continue;
        }

        if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0) {
            bool input_redir = (token[0] == '<');
            free(token);
            token = next_token(&cursor);
            if (token == NULL || strcmp(token, "<") == 0 || strcmp(token, ">") == 0 || strcmp(token, "|") == 0) {
                free(token);
                string_list_free(&args, true);
                command_free(&current);
                free_job(job);
                return SHELL_SYNTAX_ERROR;
            }

            char **slot = input_redir ? &current.input_redirect : &current.output_redirect;
            if (*slot != NULL) {
                free(token);
                string_list_free(&args, true);
                command_free(&current);
                free_job(job);
                return SHELL_SYNTAX_ERROR;
            }
            *slot = token;
            continue;
        }

        if (expand_wildcard_token(token, &args) != 0) {
            free(token);
            string_list_free(&args, true);
            command_free(&current);
            free_job(job);
            return SHELL_FAILURE;
        }
        free(token);
    }

    if (!have_anything) {
        string_list_free(&args, true);
        return SHELL_SUCCESS;
    }

    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '#') {
        /* trailing comment is fine */
    }

    if (args.size == 0 && current.input_redirect == NULL && current.output_redirect == NULL) {
        string_list_free(&args, true);
        free_job(job);
        return SHELL_SYNTAX_ERROR;
    }

    if (args.size == 0) {
        string_list_free(&args, true);
        command_free(&current);
        free_job(job);
        return SHELL_SYNTAX_ERROR;
    }

    current.argc = (int)args.size;
    current.argv = string_list_to_argv(&args);
    if (current.argv == NULL) {
        string_list_free(&args, true);
        command_free(&current);
        free_job(job);
        return SHELL_FAILURE;
    }
    free(args.items);

    if (append_command(job, &current) != 0) {
        command_free(&current);
        free_job(job);
        return SHELL_FAILURE;
    }

    return SHELL_SUCCESS;
}

void free_job(Job *job) {
    if (job == NULL) {
        return;
    }

    for (int i = 0; i < job->count; i++) {
        command_free(&job->commands[i]);
    }
    free(job->commands);
    job->commands = NULL;
    job->count = 0;
    job->has_exit = false;
}
