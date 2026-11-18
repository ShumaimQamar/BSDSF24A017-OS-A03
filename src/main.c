#include "shell.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

typedef enum {
    STATE_WAIT_THEN,
    STATE_IN_THEN,
    STATE_IN_ELSE,
    STATE_DONE
} IfParseState;

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

// Strip surrounding single or double quotes from tokenized arguments
static void strip_quotes(char** arglist) {
    for (int i = 0; arglist[i] != NULL; i++) {
        size_t len = strlen(arglist[i]);
        if (len >= 2 &&
            ((arglist[i][0] == '"' && arglist[i][len - 1] == '"') ||
             (arglist[i][0] == '\'' && arglist[i][len - 1] == '\''))) {
            memmove(arglist[i], arglist[i] + 1, len - 2);
            arglist[i][len - 2] = '\0';
        }
    }
}

// Return pointer to first non-whitespace char and trim trailing whitespace in place
static char* trim_line(char* str) {
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') {
        str++;
    }
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' ||
                       str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
    return str;
}

// Check if a trimmed line starts with a given keyword followed by whitespace or end-of-line
static int starts_with_keyword(const char* trimmed, const char* kw) {
    size_t klen = strlen(kw);
    if (strncmp(trimmed, kw, klen) != 0) {
        return 0;
    }
    return trimmed[klen] == '\0' || trimmed[klen] == ' ' || trimmed[klen] == '\t';
}

static int append_line(char*** list, int* count, int* cap, const char* line) {
    if (*count >= *cap) {
        int new_cap = (*cap == 0) ? 8 : (*cap * 2);
        char** resized = realloc(*list, (size_t)new_cap * sizeof(char*));
        if (!resized) {
            perror("realloc");
            return -1;
        }
        *list = resized;
        *cap = new_cap;
    }
    (*list)[*count] = strdup(line);
    if (!(*list)[*count]) {
        perror("strdup");
        return -1;
    }
    (*count)++;
    return 0;
}

static void free_line_list(char** list, int count) {
    for (int i = 0; i < count; i++) {
        free(list[i]);
    }
    free(list);
}

// Forward declaration so execute_line() and process_if_block() can support nested ifs
static int execute_line(const char* raw_line);

static int process_if_block(const char* first_line) {
    char* line_copy = strdup(first_line);
    if (!line_copy) {
        perror("strdup");
        return 1;
    }

    char* trimmed = trim_line(line_copy);
    // Extract condition command right after "if"
    char* cond_start = trimmed + 2;
    while (*cond_start == ' ' || *cond_start == '\t') {
        cond_start++;
    }

    if (*cond_start == '\0') {
        fprintf(stderr, "myshell: syntax error near unexpected token 'if'\n");
        free(line_copy);
        return 1;
    }

    char* cond_cmd = strdup(cond_start);
    free(line_copy);
    if (!cond_cmd) {
        perror("strdup");
        return 1;
    }

    char** then_cmds = NULL;
    int then_count = 0, then_cap = 0;

    char** else_cmds = NULL;
    int else_count = 0, else_cap = 0;

    IfParseState state = STATE_WAIT_THEN;
    int depth = 1;
    int syntax_error = 0;

    while (state != STATE_DONE) {
        char* next_line = readline("> ");
        if (next_line == NULL) {
            fprintf(stderr, "myshell: syntax error: unexpected EOF while looking for 'fi'\n");
            syntax_error = 1;
            break;
        }

        if (next_line[0] != '\0') {
            add_history(next_line);
            add_to_history(next_line);
        }

        char* scan_copy = strdup(next_line);
        char* t = trim_line(scan_copy);

        // Skip blank lines and comment lines inside the block
        if (t[0] == '\0' || t[0] == '#') {
            free(scan_copy);
            free(next_line);
            continue;
        }

        if (state == STATE_WAIT_THEN) {
            if (strcmp(t, "then") == 0) {
                state = STATE_IN_THEN;
            } else {
                fprintf(stderr, "myshell: syntax error near '%s' (expected 'then')\n", t);
                syntax_error = 1;
            }
        } else if (state == STATE_IN_THEN) {
            if (starts_with_keyword(t, "if")) {
                depth++;
                append_line(&then_cmds, &then_count, &then_cap, next_line);
            } else if (depth == 1 && strcmp(t, "else") == 0) {
                state = STATE_IN_ELSE;
            } else if (strcmp(t, "fi") == 0) {
                depth--;
                if (depth == 0) {
                    state = STATE_DONE;
                } else {
                    append_line(&then_cmds, &then_count, &then_cap, next_line);
                }
            } else {
                append_line(&then_cmds, &then_count, &then_cap, next_line);
            }
        } else if (state == STATE_IN_ELSE) {
            if (starts_with_keyword(t, "if")) {
                depth++;
                append_line(&else_cmds, &else_count, &else_cap, next_line);
            } else if (depth == 1 && strcmp(t, "else") == 0) {
                fprintf(stderr, "myshell: syntax error near unexpected token 'else'\n");
                syntax_error = 1;
            } else if (strcmp(t, "fi") == 0) {
                depth--;
                if (depth == 0) {
                    state = STATE_DONE;
                } else {
                    append_line(&else_cmds, &else_count, &else_cap, next_line);
                }
            } else {
                append_line(&else_cmds, &else_count, &else_cap, next_line);
            }
        }

        free(scan_copy);
        free(next_line);

        if (syntax_error) {
            break;
        }
    }

    int result_status = 1;
    if (!syntax_error) {
        // Execute the condition command and inspect its exit status (0 = true, non-zero = false)
        int cond_status = execute_line(cond_cmd);

        if (cond_status == 0) {
            result_status = 0;
            for (int i = 0; i < then_count; i++) {
                result_status = execute_line(then_cmds[i]);
            }
        } else {
            result_status = 0;
            for (int i = 0; i < else_count; i++) {
                result_status = execute_line(else_cmds[i]);
            }
        }
    }

    free(cond_cmd);
    free_line_list(then_cmds, then_count);
    free_line_list(else_cmds, else_count);
    return result_status;
}

static int execute_line(const char* raw_line) {
    char* copy = strdup(raw_line);
    if (!copy) {
        perror("strdup");
        return 1;
    }

    char* trimmed = trim_line(copy);
    if (trimmed[0] == '\0' || trimmed[0] == '#') {
        free(copy);
        return 0;
    }

    // Intercept multi-line if-then-else-fi control blocks
    if (starts_with_keyword(trimmed, "if")) {
        int status = process_if_block(trimmed);
        free(copy);
        return status;
    }

    int status = 0;
    char* normalized = pad_operators(trimmed);
    if (normalized != NULL) {
        char** arglist = tokenize(normalized);
        if (arglist != NULL) {
            strip_quotes(arglist);

            int count = 0;
            int syntax_err = 0;
            while (arglist[count] != NULL) {
                count++;
            }

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

            if (syntax_err) {
                status = 1;
            } else if (arglist[0] != NULL) {
                int builtin_status = 0;
                if (handle_builtin(arglist, &builtin_status)) {
                    status = builtin_status;
                } else {
                    status = execute(arglist, background, trimmed);
                }
            }

            free_arglist(arglist);
        }
        free(normalized);
    }

    free(copy);
    return status;
}

int main() {
    char* cmdline;

    using_history();
    init_readline();
    init_signals();

    while ((cmdline = readline(PROMPT)) != NULL) {
        cleanup_done_jobs();

        if (cmdline[0] == '\0') {
            free(cmdline);
            continue;
        }

        // Handle !n re-execution BEFORE adding to history or executing
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

        execute_line(cmdline);
        free(cmdline);
    }

    free_history();
    free_jobs();
    clear_history();
    printf("\n");
    return 0;
}
