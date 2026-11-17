#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#ifndef MAXARGS
#define MAXARGS 64
#endif


#define MAX_LEN 512
#define ARGLEN 30
#define PROMPT "FCIT> "

// Function prototypes
#define HISTORY_SIZE 20
void add_to_history(const char* cmd);
void print_history(void);
const char* get_history_command(int n);
void free_history(void);

void init_readline(void);

char* pad_operators(const char* cmd);
void execute(char** arglist);

char* read_cmd(char* prompt, FILE* fp);
char** tokenize(char* cmdline);
int handle_builtin(char** arglist);

#endif // SHELL_H
