#define _POSIX_C_SOURCE 200809L
#include "shell.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int input_fd = STDIN_FILENO;
    bool interactive = false;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [batch_file]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 2) {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
        interactive = false;
    } else {
        interactive = isatty(STDIN_FILENO);
    }

    ShellState state;
    shell_state_init(&state, interactive);
    int result = run_shell(input_fd, &state);

    if (argc == 2) {
        close(input_fd);
    }

    return result;
}
