#include "shell.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int is_operator(const char* tok) {
    return strcmp(tok, "<") == 0 ||
           strcmp(tok, ">") == 0 ||
           strcmp(tok, ">>") == 0 ||
           strcmp(tok, "|") == 0;
}

// Validate pipe and redirection syntax before spawning any child processes
static int validate_pipeline(char** arglist) {
    int cmd_tokens = 0;

    for (int i = 0; arglist[i] != NULL; i++) {
        if (strcmp(arglist[i], "|") == 0) {
            if (cmd_tokens == 0) {
                fprintf(stderr, "myshell: syntax error near unexpected token '|'\n");
                return -1;
            }
            cmd_tokens = 0;
        } else if (strcmp(arglist[i], "<") == 0 ||
                   strcmp(arglist[i], ">") == 0 ||
                   strcmp(arglist[i], ">>") == 0) {
            if (arglist[i + 1] == NULL || is_operator(arglist[i + 1])) {
                fprintf(stderr, "myshell: syntax error near redirection\n");
                return -1;
            }
            i++; // Skip the target filename token
        } else {
            cmd_tokens++;
        }
    }

    if (cmd_tokens == 0) {
        fprintf(stderr, "myshell: syntax error near unexpected token '|'\n");
        return -1;
    }

    return 0;
}

// Apply <, >, and >> redirections inside the child process and build clean_argv
static int setup_io_and_args(char** arglist, int start, int end, char** clean_argv) {
    int argc = 0;

    for (int i = start; i < end; i++) {
        if (strcmp(arglist[i], "<") == 0) {
            const char* infile = arglist[++i];
            int fd = open(infile, O_RDONLY);
            if (fd < 0) {
                perror(infile);
                return -1;
            }
            if (dup2(fd, STDIN_FILENO) < 0) {
                perror("dup2");
                close(fd);
                return -1;
            }
            close(fd);
        } else if (strcmp(arglist[i], ">") == 0) {
            const char* outfile = arglist[++i];
            int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror(outfile);
                return -1;
            }
            if (dup2(fd, STDOUT_FILENO) < 0) {
                perror("dup2");
                close(fd);
                return -1;
            }
            close(fd);
        } else if (strcmp(arglist[i], ">>") == 0) {
            const char* outfile = arglist[++i];
 
           int fd = open(outfile, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) {
                perror(outfile);
                return -1;
            }
            if (dup2(fd, STDOUT_FILENO) < 0) {
                perror("dup2");
                close(fd);
                return -1;
            }
            close(fd);
        } else {
            if (argc < MAXARGS - 1) {
                clean_argv[argc++] = arglist[i];
            }
        }
    }

    clean_argv[argc] = NULL;
    return (argc > 0) ? 0 : -1;
}

void execute(char** arglist) {
    if (arglist == NULL || arglist[0] == NULL) {
        return;
    }

    if (validate_pipeline(arglist) != 0) {
        return;
    }

    // Count how many commands are separated by '|'
    int num_cmds = 1;
    int total_tokens = 0;
    while (arglist[total_tokens] != NULL) {
        if (strcmp(arglist[total_tokens], "|") == 0) {
            num_cmds++;
        }
        total_tokens++;
    }

    int* seg_start = malloc((size_t)num_cmds * sizeof(int));
    int* seg_end = malloc((size_t)num_cmds * sizeof(int));
    pid_t* pids = malloc((size_t)num_cmds * sizeof(pid_t));
    int (*pipes)[2] = (num_cmds > 1) ? malloc((size_t)(num_cmds - 1) * sizeof(int[2])) : NULL;

    if (!seg_start || !seg_end || !pids || (num_cmds > 1 && !pipes)) {
        perror("malloc");
        free(seg_start);
        free(seg_end);
        free(pids);
        free(pipes);
        return;
    }

    // Record [start, end) token boundaries for each command in the pipeline
    int cmd_idx = 0;
    seg_start[0] = 0;
    for (int i = 0; i < total_tokens; i++) {
        if (strcmp(arglist[i], "|") == 0) {
            seg_end[cmd_idx] = i;
            cmd_idx++;
            seg_start[cmd_idx] = i + 1;
        }
    }
    seg_end[cmd_idx] = total_tokens;

    // Create all (num_cmds - 1) pipes before forking children
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            free(seg_start);
            free(seg_end);
            free(pids);
            free(pipes);
            return;
        }
    }

    // Fork and launch each command in the pipeline
    for (int i = 0; i < num_cmds; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            break;
        }

        if (pids[i] == 0) {
            // Connect stdin to previous pipe's read end (if not first command)
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    perror("dup2");
                    _exit(1);
                }
            }

            // Connect stdout to current pipe's write end (if not last command)
            if (i < num_cmds - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("dup2");
                    _exit(1);
                }
            }

            // Close all pipe descriptors in the child so EOF works properly
            for (int j = 0; j < num_cmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Apply any file redirections (<, >, >>) and strip operators from argv
            char* clean_argv[MAXARGS];
            if (setup_io_and_args(arglist, seg_start[i], seg_end[i], clean_argv) != 0) {
                _exit(1);
            }

            execvp(clean_argv[0], clean_argv);
            perror(clean_argv[0]);
            _exit(1);
        }
    }

    // Parent closes all pipe descriptors and waits for every child to finish
    for (int j = 0; j < num_cmds - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
    }

    for (int i = 0; i < num_cmds; i++) {
        if (pids[i] > 0) {
            waitpid(pids[i], NULL, 0);
        }
    }

    free(seg_start);
    free(seg_end);
    free(pids);
    free(pipes);
}
