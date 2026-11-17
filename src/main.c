#include "shell.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

int main() {
    char* cmdline;
    char** arglist;

    using_history();
    init_readline();

    while ((cmdline = readline(PROMPT)) != NULL) {
        // Skip empty lines without adding to history
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

        // Add to Readline's history (for Up/Down arrows) AND our Feature-3 history
        add_history(cmdline);
        add_to_history(cmdline);

        if ((arglist = tokenize(cmdline)) != NULL) {
            if (!handle_builtin(arglist)) {
                execute(arglist);
            }

            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }
        free(cmdline);
    }

    free_history();
    clear_history();
    printf("\n");
    return 0;
}
