#ifndef SHELL_H
#define SHELL_H

#include <stdbool.h>
#include <stddef.h>

#define READ_CHUNK_SIZE 1
#define SHELL_SUCCESS 0
#define SHELL_FAILURE 1
#define SHELL_SYNTAX_ERROR 2

extern const char *const SEARCH_PATHS[];
extern const size_t SEARCH_PATH_COUNT;

typedef struct {
    char **items;
    size_t size;
    size_t capacity;
} StringList;

typedef struct {
    char **argv;
    int argc;
    char *input_redirect;
    char *output_redirect;
} Command;

typedef struct {
    Command *commands;
    int count;
    bool has_exit;
} Job;

typedef struct {
    bool interactive;
    bool should_exit;
    int last_exit_code;
    int last_signal;
    bool last_signaled;
    const char *home;
} ShellState;

void shell_state_init(ShellState *state, bool interactive);
void print_welcome(const ShellState *state);
void print_goodbye(const ShellState *state);
void print_prompt(const ShellState *state);
void print_last_status(const ShellState *state);
int read_command_line(int fd, char **line_out);
int run_shell(int input_fd, ShellState *state);

void string_list_init(StringList *list);
int string_list_push(StringList *list, char *item);
void string_list_free(StringList *list, bool free_items);
char **string_list_to_argv(StringList *list);

bool is_builtin_name(const char *name);
int run_builtin(Command *cmd, ShellState *state);
int run_job(Job *job, ShellState *state);

int parse_job(const char *line, Job *job);
void free_job(Job *job);

char *xstrdup(const char *src);
char *join_path(const char *dir, const char *name);
char *search_program_path(const char *name);
int expand_wildcard_token(const char *token, StringList *out);
void shell_error(const char *prefix, const char *detail);

#endif
