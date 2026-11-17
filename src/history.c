#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* history[HISTORY_SIZE];
static int history_count = 0;

void add_to_history(const char* cmd) {
    if (cmd == NULL || cmd[0] == '\0') {
        return;
    }

    char* copy = strdup(cmd);
    if (copy == NULL) {
        perror("strdup");
        return;
    }

    // When full (20 commands), free the oldest entry and shift everything left
    if (history_count == HISTORY_SIZE) {
        free(history[0]);
        for (int i = 1; i < HISTORY_SIZE; i++) {
            history[i - 1] = history[i];
        }
        history[HISTORY_SIZE - 1] = copy;
    } else {
        history[history_count++] = copy;
    }
}

void print_history(void) {
    for (int i = 0; i < history_count; i++) {
        printf("%d  %s\n", i + 1, history[i]);
    }
}

const char* get_history_command(int n) {
    if (n < 1 || n > history_count) {
        return NULL;
    }
    return history[n - 1];
}

void free_history(void) {
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
        history[i] = NULL;
    }
    history_count = 0;
}
