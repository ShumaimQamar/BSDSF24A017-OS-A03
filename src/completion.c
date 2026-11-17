#include "shell.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>

static const char* builtins[] = {
    "cd", "exit", "help", "history", "jobs", NULL
};

// Generator function called repeatedly by Readline to find matching commands
static char* command_generator(const char* text, int state) {
    static int builtin_idx;
    static size_t len;
    static char* path_copy;
    static char* current_dir;
    static DIR* dirp;

    // Initialize state on the first call (state == 0)
    if (state == 0) {
        builtin_idx = 0;
        len = strlen(text);
        free(path_copy);
        path_copy = NULL;
        current_dir = NULL;
        if (dirp != NULL) {
            closedir(dirp);
            dirp = NULL;
        }

        const char* env_path = getenv("PATH");
        if (env_path != NULL) {
            path_copy = strdup(env_path);
            if (path_copy != NULL) {
                current_dir = strtok(path_copy, ":");
            }
        }
    }

    // 1. Check built-in commands first
    while (builtins[builtin_idx] != NULL) {
        const char* name = builtins[builtin_idx++];
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    // 2. Scan directories in PATH for matching executables
    while (current_dir != NULL) {
        if (dirp == NULL) {
            dirp = opendir(current_dir);
        }
        if (dirp != NULL) {
            struct dirent* entry;
            while ((entry = readdir(dirp)) != NULL) {
                if (entry->d_name[0] == '.') {
                    continue;
                }
                if (strncmp(entry->d_name, text, len) == 0) {
                    return strdup(entry->d_name);
                }
            }
            closedir(dirp);
            dirp = NULL;
        }
        current_dir = strtok(NULL, ":");
    }

    free(path_copy);
    path_copy = NULL;
    return NULL;
}

// Custom completion hook: completes commands for the first word,
// and falls back to default filename completion for arguments
static char** shell_completion(const char* text, int start, int end) {
    (void)end;

    // If typing the first word (start == 0) and not a relative path (like ./bin/...)
    if (start == 0 && strchr(text, '/') == NULL) {
        return rl_completion_matches(text, command_generator);
    }

    // Returning NULL tells Readline to use its default filename completion
    return NULL;
}

void init_readline(void) {
    rl_attempted_completion_function = shell_completion;
}
