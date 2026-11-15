#include "shell.h"

int main() {
    char* cmdline;
    char** arglist;

    while ((cmdline = read_cmd(PROMPT, stdin)) != NULL) {
        if ((arglist = tokenize(cmdline)) != NULL) {
            // Check and run built-in commands in the parent process first.
            // Only call execute() (which forks a child) if it is an external command.
            if (!handle_builtin(arglist)) {
                execute(arglist);
            }

            // Free the dynamically allocated argument list
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }
        free(cmdline);
    }

    printf("\n");
    return 0;
}
