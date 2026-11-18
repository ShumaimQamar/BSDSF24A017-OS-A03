#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int handle_builtin(char** arglist, int* exit_status) {
    if (arglist == NULL || arglist[0] == NULL) {
        return 0;
    }

    // 1. exit: Terminate the shell gracefully
    if (strcmp(arglist[0], "exit") == 0) {
        free_history();
        free_jobs();
        free_variables();
        printf("Exiting myshell...\n");
        exit(0);
    }

    // 2. cd <directory>: Change current working directory
    if (strcmp(arglist[0], "cd") == 0) {
        const char* target_dir = arglist[1];
        if (target_dir == NULL) {
            target_dir = getenv("HOME");
            if (target_dir == NULL) {
                fprintf(stderr, "cd: HOME not set\n");
                if (exit_status) *exit_status = 1;
                return 1;
            }
        }
        if (chdir(target_dir) != 0) {
            perror("cd");
            if (exit_status) *exit_status = 1;
        } else {
            if (exit_status) *exit_status = 0;
        }
        return 1;
    }

    // 3. help: List supported built-in commands
if (strcmp(arglist[0], "help") == 0) {
     printf("myshell built-in commands:\n");
     printf("  cd <dir>   - Change the current working directory\n");
     printf("  exit       - Terminate the shell\n");
     printf("  help       - Display this help message\n");
     printf("  history    - Display command history (use !n to re-run command n)\n");
     printf("  jobs       - List active background jobs\n");
     printf("  set        - Display all defined shell variables (assign via VAR=value)\n");
     if (exit_status) *exit_status = 0;
     return 1;
 }

    // 4. jobs: List active background jobs
    if (strcmp(arglist[0], "jobs") == 0) {
        print_jobs();
        if (exit_status) *exit_status = 0;
        return 1;
    }

    // 5. history: Display stored commands
    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        if (exit_status) *exit_status = 0;
        return 1;
    }

    // 6. set: Print all defined shell variables
    if (strcmp(arglist[0], "set") == 0) {
        print_variables();
        if (exit_status) *exit_status = 0;
        return 1;
    }

    return 0;
}

