#include "shell.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

char* pad_operators(const char* cmd) {
    size_t len = strlen(cmd);
    char* padded = malloc(len * 3 + 1);
    if (padded == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (cmd[i] == '>' && cmd[i + 1] == '>') {
            padded[j++] = ' ';
            padded[j++] = '>';
            padded[j++] = '>';
            padded[j++] = ' ';
            i++;
        } else if (cmd[i] == '<' || cmd[i] == '>' || cmd[i] == '|') {
            padded[j++] = ' ';
            padded[j++] = cmd[i];
            padded[j++] = ' ';
        } else {
            padded[j++] = cmd[i];
        }
    }
    padded[j] = '\0';
    return padded;
}

int main() {
    char* cmdline;
    char** arglist;

    using_history();
    init_readline();

    while ((cmdline = readline(PROMPT)) != NULL) {
        if (cmdline[0] == '\0') {
            free(cmdline);
            continue;
        }

        // Handle !n re-execution BEFORE adding to history or tokenizing
        if (cmdline[0] == '!') {
            if (!isdigit((unsigned char)cmdline[1])) {
                fprintf(stderr, "Invalid history reference: %s\n", cmdline);
                free(cmdline);
                continue;
            }

            char* endptr;
            errno = 0;
            long n = strtol(cmdline + 1, &endptr, 10);

            while (*endptr == ' ' || *endptr == '\t') {
                endptr++;
            }

            if (*endptr != '\0' || errno != 0) {
                fprintf(stderr, "Invalid history reference: %s\n", cmdline);
                free(cmdline);
                continue;
            }

            const char* saved_cmd = get_history_command((int)n);
            if (saved_cmd == NULL) {
                fprintf(stderr, "No such command in history: !%ld\n", n);
                free(cmdline);
                continue;
            }

            char* resolved = strdup(saved_cmd);
            free(cmdline);
            if (resolved == NULL) {
                perror("strdup");
                continue;
            }
            cmdline = resolved;
            printf("%s\n", cmdline);
        }

        add_history(cmdline);
        add_to_history(cmdline);

        char* normalized = pad_operators(cmdline);
        if (normalized != NULL) {
            if ((arglist = tokenize(normalized)) != NULL) {
                if (!handle_builtin(arglist)) {
                    execute(arglist);
                }

                for (int i = 0; arglist[i] != NULL; i++) {
                    free(arglist[i]);
                }
                free(arglist);
            }
            free(normalized);
        }
        free(cmdline);
    }

    free_history();
    clear_history();
    printf("\n");
    return 0;
}
