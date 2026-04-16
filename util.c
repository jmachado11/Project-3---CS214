#define _POSIX_C_SOURCE 200809L
#include "shell.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *const SEARCH_PATHS[] = {
    "/usr/local/bin",
    "/usr/bin",
    "/bin"
};

const size_t SEARCH_PATH_COUNT = sizeof(SEARCH_PATHS) / sizeof(SEARCH_PATHS[0]);

static int cmp_strings(const void *lhs, const void *rhs) {
    const char *const *a = lhs;
    const char *const *b = rhs;
    return strcmp(*a, *b);
}

char *xstrdup(const char *src) {
    if (src == NULL) {
        return NULL;
    }

    size_t len = strlen(src) + 1;
    char *copy = malloc(len);
    if (copy == NULL) {
        perror("malloc");
        return NULL;
    }
    memcpy(copy, src, len);
    return copy;
}

void string_list_init(StringList *list) {
    list->items = NULL;
    list->size = 0;
    list->capacity = 0;
}

int string_list_push(StringList *list, char *item) {
    if (list->size == list->capacity) {
        size_t new_capacity = (list->capacity == 0) ? 8 : list->capacity * 2;
        char **new_items = realloc(list->items, new_capacity * sizeof(char *));
        if (new_items == NULL) {
            perror("realloc");
            return -1;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->size++] = item;
    return 0;
}

void string_list_free(StringList *list, bool free_items) {
    if (list == NULL) {
        return;
    }

    if (free_items) {
        for (size_t i = 0; i < list->size; i++) {
            free(list->items[i]);
        }
    }

    free(list->items);
    list->items = NULL;
    list->size = 0;
    list->capacity = 0;
}

char **string_list_to_argv(StringList *list) {
    char **argv = calloc(list->size + 1, sizeof(char *));
    if (argv == NULL) {
        perror("calloc");
        return NULL;
    }

    for (size_t i = 0; i < list->size; i++) {
        argv[i] = list->items[i];
    }
    argv[list->size] = NULL;
    return argv;
}

void shell_error(const char *prefix, const char *detail) {
    if (detail != NULL) {
        fprintf(stderr, "%s: %s\n", prefix, detail);
    } else {
        perror(prefix);
    }
}

char *join_path(const char *dir, const char *name) {
    if (dir == NULL || name == NULL) {
        return NULL;
    }

    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    bool has_slash = (dir_len > 0 && dir[dir_len - 1] == '/');
    size_t total = dir_len + (has_slash ? 0 : 1) + name_len + 1;

    char *result = malloc(total);
    if (result == NULL) {
        perror("malloc");
        return NULL;
    }

    if (has_slash) {
        snprintf(result, total, "%s%s", dir, name);
    } else {
        snprintf(result, total, "%s/%s", dir, name);
    }

    return result;
}

char *search_program_path(const char *name) {
    if (name == NULL || strchr(name, '/') != NULL || is_builtin_name(name)) {
        return NULL;
    }

    for (size_t i = 0; i < SEARCH_PATH_COUNT; i++) {
        char *candidate = join_path(SEARCH_PATHS[i], name);
        if (candidate == NULL) {
            return NULL;
        }

        if (access(candidate, X_OK) == 0) {
            return candidate;
        }

        free(candidate);
    }

    return NULL;
}

static bool wildcard_matches(const char *pattern, const char *name) {
    const char *star = strchr(pattern, '*');
    if (star == NULL) {
        return strcmp(pattern, name) == 0;
    }

    size_t prefix_len = (size_t)(star - pattern);
    const char *suffix = star + 1;
    size_t suffix_len = strlen(suffix);
    size_t name_len = strlen(name);

    if (pattern[0] == '*' && name[0] == '.') {
        return false;
    }

    if (name_len < prefix_len + suffix_len) {
        return false;
    }

    if (prefix_len > 0 && strncmp(pattern, name, prefix_len) != 0) {
        return false;
    }

    if (suffix_len > 0 && strcmp(name + name_len - suffix_len, suffix) != 0) {
        return false;
    }

    return true;
}

int expand_wildcard_token(const char *token, StringList *out) {
    const char *star = strchr(token, '*');
    if (star == NULL) {
        return string_list_push(out, xstrdup(token));
    }

    const char *slash = strrchr(token, '/');
    char *dir_path = NULL;
    const char *pattern = token;
    bool prefix_path = false;

    if (slash != NULL) {
        size_t dir_len = (size_t)(slash - token);
        dir_path = malloc(dir_len + 1);
        if (dir_path == NULL) {
            perror("malloc");
            return -1;
        }
        memcpy(dir_path, token, dir_len);
        dir_path[dir_len] = '\0';
        pattern = slash + 1;
        prefix_path = true;
        if (dir_len == 0) {
            free(dir_path);
            dir_path = xstrdup("/");
            if (dir_path == NULL) {
                return -1;
            }
        }
    } else {
        dir_path = xstrdup(".");
        if (dir_path == NULL) {
            return -1;
        }
    }

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        free(dir_path);
        return string_list_push(out, xstrdup(token));
    }

    StringList matches;
    string_list_init(&matches);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!wildcard_matches(pattern, entry->d_name)) {
            continue;
        }

        char *result = NULL;
        if (prefix_path) {
            result = join_path(dir_path, entry->d_name);
        } else {
            result = xstrdup(entry->d_name);
        }

        if (result == NULL || string_list_push(&matches, result) != 0) {
            free(result);
            string_list_free(&matches, true);
            closedir(dir);
            free(dir_path);
            return -1;
        }
    }

    closedir(dir);
    free(dir_path);

    if (matches.size == 0) {
        string_list_free(&matches, true);
        return string_list_push(out, xstrdup(token));
    }

    qsort(matches.items, matches.size, sizeof(char *), cmp_strings);

    for (size_t i = 0; i < matches.size; i++) {
        if (string_list_push(out, matches.items[i]) != 0) {
            for (size_t j = i; j < matches.size; j++) {
                free(matches.items[j]);
            }
            free(matches.items);
            return -1;
        }
    }

    free(matches.items);
    return 0;
}
