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
        } else if (cmd[i] == '<' || cmd[i] == '>' || cmd[i] == '|' || cmd[i] == '&') {
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

static void free_arglist(char** arglist) {
    for (int i = 0; arglist[i] != NULL; i++) {
        free(arglist[i]);
    }
    free(arglist);
}

int main() {
    char* cmdline;
    char** arglist;

    using_history();
    init_readline();
    init_signals();

    while ((cmdline = readline(PROMPT)) != NULL) {
        cleanup_done_jobs();

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
                int count = 0;
                int syntax_err = 0;
                while (arglist[count] != NULL) {
                    count++;
                }

                // Check for misplaced '&' anywhere before the final token
                for (int i = 0; i < count - 1; i++) {
                    if (strcmp(arglist[i], "&") == 0) {
                        fprintf(stderr, "myshell: syntax error near unexpected token '&'\n");
                        syntax_err = 1;
                        break;
                    }
                }

                int background = 0;
                if (!syntax_err && count > 0 && strcmp(arglist[count - 1], "&") == 0) {
                    if (count == 1) {
                        fprintf(stderr, "myshell: syntax error near unexpected token '&'\n");
                        syntax_err = 1;
                    } else {
                        background = 1;
                        free(arglist[count - 1]);
                        arglist[count - 1] = NULL;
                    }
                }

                if (!syntax_err && arglist[0] != NULL) {
                    if (!handle_builtin(arglist)) {
                        execute(arglist, background, cmdline);
                    }
                }

                free_arglist(arglist);
            }
            free(normalized);
        }
        free(cmdline);
    }

    free_history();
    free_jobs();
    clear_history();
    printf("\n");
    return 0;
}
