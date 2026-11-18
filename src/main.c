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

// Pad <, >, >>, |, and & outside of quotes
char* pad_operators(const char* cmd) {
    size_t len = strlen(cmd);
    char* padded = malloc(len * 3 + 1);
    if (padded == NULL) {
        return NULL;
    }

    size_t j = 0;
    char quote = 0;
    for (size_t i = 0; i < len; i++) {
        if (quote == 0 && (cmd[i] == '\'' || cmd[i] == '"')) {
            quote = cmd[i];
            padded[j++] = cmd[i];
        } else if (quote != 0 && cmd[i] == quote) {
            quote = 0;
            padded[j++] = cmd[i];
        } else if (quote == 0 && cmd[i] == '>' && cmd[i + 1] == '>') {
            padded[j++] = ' ';
            padded[j++] = '>';
            padded[j++] = '>';
            padded[j++] = ' ';
            i++;
        } else if (quote == 0 && (cmd[i] == '<' || cmd[i] == '>' || cmd[i] == '|' || cmd[i] == '&')) {
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

// Quote-aware tokenizer so strings like MESSAGE="Hello, World!" remain a single token
static char** tokenize_quote_aware(const char* line) {
    char** arglist = malloc(MAXARGS * sizeof(char*));
    if (arglist == NULL) {
        perror("malloc");
        return NULL;
    }

    int argc = 0;
    size_t i = 0;
    size_t len = strlen(line);

    while (i < len && argc < MAXARGS - 1) {
        while (i < len && (line[i] == ' ' || line[i] == '\t')) {
            i++;
        }
        if (i >= len) {
            break;
        }

        char* tok = malloc(len - i + 1);
        if (tok == NULL) {
            perror("malloc");
            break;
        }

        size_t t = 0;
        char quote = 0;
        while (i < len) {
            if (quote == 0 && (line[i] == ' ' || line[i] == '\t')) {
                break;
            }
            if (quote == 0 && (line[i] == '\'' || line[i] == '"')) {
                quote = line[i];
                tok[t++] = line[i++];
            } else if (quote != 0 && line[i] == quote) {
                quote = 0;
                tok[t++] = line[i++];
            } else {
                tok[t++] = line[i++];
            }
        }
        tok[t] = '\0';
        arglist[argc++] = tok;
    }

    if (argc == 0) {
        free(arglist);
        return NULL;
    }

    arglist[argc] = NULL;
    return arglist;
}

static void free_arglist(char** arglist) {
    for (int i = 0; arglist[i] != NULL; i++) {
        free(arglist[i]);
    }
    free(arglist);
}

// Lookup variable in shell linked list first, then environment variables
static const char* lookup_var_value(const char* name) {
    const char* val = get_variable(name);
    if (val != NULL) {
        return val;
    }
    const char* env_val = getenv(name);
    return (env_val != NULL) ? env_val : "";
}

// Allocate a new string with all $VAR and ${VAR} references expanded
static char* expand_single_token(const char* tok) {
    size_t cap = strlen(tok) + 32;
    char* out = malloc(cap);
    if (out == NULL) {
        return NULL;
    }

    size_t out_len = 0;
    char quote = 0;

    for (size_t i = 0; tok[i] != '\0'; ) {
        if (quote == 0 && (tok[i] == '\'' || tok[i] == '"')) {
            quote = tok[i];
            if (out_len + 2 > cap) {
                cap *= 2;
                char* tmp = realloc(out, cap);
                if (!tmp) { free(out); return NULL; }
                out = tmp;
            }
            out[out_len++] = tok[i++];
            continue;
        }
        if (quote != 0 && tok[i] == quote) {
            quote = 0;
            if (out_len + 2 > cap) {
                cap *= 2;
                char* tmp = realloc(out, cap);
                if (!tmp) { free(out); return NULL; }
                out = tmp;
            }
            out[out_len++] = tok[i++];
            continue;
        }

        // Expand $VARNAME or ${VARNAME} when not inside single quotes
        if (tok[i] == '$' && quote != '\'') {
            if (tok[i + 1] == '{') {
                const char* end_brace = strchr(tok + i + 2, '}');
                if (end_brace != NULL && end_brace > tok + i + 2) {
                    size_t name_len = (size_t)(end_brace - (tok + i + 2));
                    char* name = malloc(name_len + 1);
                    if (name) {
                        memcpy(name, tok + i + 2, name_len);
                        name[name_len] = '\0';
                        const char* val = lookup_var_value(name);
                        size_t vlen = strlen(val);
                        while (out_len + vlen + 1 > cap) {
                            cap *= 2;
                            char* tmp = realloc(out, cap);
                            if (!tmp) { free(name); free(out); return NULL; }
                            out = tmp;
                        }
                        memcpy(out + out_len, val, vlen);
                        out_len += vlen;
                        free(name);
                    }
                    i = (size_t)(end_brace - tok) + 1;
                    continue;
                }
            } else if (isalpha((unsigned char)tok[i + 1]) || tok[i + 1] == '_') {
                size_t start = i + 1;
                size_t end = start;
                while (isalnum((unsigned char)tok[end]) || tok[end] == '_') {
                    end++;
                }
                size_t name_len = end - start;
                char* name = malloc(name_len + 1);
                if (name) {
                    memcpy(name, tok + start, name_len);
                    name[name_len] = '\0';
                    const char* val = lookup_var_value(name);
                    size_t vlen = strlen(val);
                    while (out_len + vlen + 1 > cap) {
                        cap *= 2;
                        char* tmp = realloc(out, cap);
                        if (!tmp) { free(name); free(out); return NULL; }
                        out = tmp;
                    }
                    memcpy(out + out_len, val, vlen);
                    out_len += vlen;
                    free(name);
                }
                i = end;
                continue;
            }
        }

        if (out_len + 2 > cap) {
            cap *= 2;
            char* tmp = realloc(out, cap);
            if (!tmp) { free(out); return NULL; }
            out = tmp;
        }
        out[out_len++] = tok[i++];
    }

    out[out_len] = '\0';
    return out;
}

// Pre-execution variable expansion across all tokens in arglist
static void expand_variables(char** arglist) {
    for (int i = 0; arglist[i] != NULL; i++) {
        if (strchr(arglist[i], '$') != NULL) {
            char* expanded = expand_single_token(arglist[i]);
            if (expanded != NULL) {
                free(arglist[i]);
                arglist[i] = expanded;
            }
        }
    }
}

// Remove balanced single or double quotes from a token
static void strip_quotes_from_token(char* tok) {
    size_t r = 0, w = 0;
    char quote = 0;
    while (tok[r] != '\0') {
        if (quote == 0 && (tok[r] == '\'' || tok[r] == '"')) {
            char q = tok[r];
            if (strchr(tok + r + 1, q) != NULL) {
                quote = q;
                r++;
                continue;
            }
        } else if (quote != 0 && tok[r] == quote) {
            quote = 0;
            r++;
            continue;
        }
        tok[w++] = tok[r++];
    }
    tok[w] = '\0';
}

static void strip_quotes(char** arglist) {
    for (int i = 0; arglist[i] != NULL; i++) {
        strip_quotes_from_token(arglist[i]);
    }
}

// Check if a token matches valid shell assignment syntax: VARNAME=value
static int is_valid_assignment(const char* tok) {
    const char* eq = strchr(tok, '=');
    if (eq == NULL || eq == tok) {
        return 0;
    }

    if (!isalpha((unsigned char)tok[0]) && tok[0] != '_') {
        return 0;
    }
    for (const char* p = tok + 1; p < eq; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') {
            return 0;
        }
    }
    return 1;
}

// Handle built-in variable assignment(s) without forking
static int handle_variable_assignment(char** arglist) {
    if (arglist == NULL || arglist[0] == NULL) {
        return 0;
    }

    if (!is_valid_assignment(arglist[0])) {
        return 0;
    }

    // Verify all tokens on this command are valid VAR=value assignments
    for (int i = 1; arglist[i] != NULL; i++) {
        if (!is_valid_assignment(arglist[i])) {
            fprintf(stderr, "myshell: invalid assignment syntax near '%s'\n", arglist[i]);
            return 1;
        }
    }

    for (int i = 0; arglist[i] != NULL; i++) {
        char* eq = strchr(arglist[i], '=');
        size_t name_len = (size_t)(eq - arglist[i]);
        char* name = malloc(name_len + 1);
        if (name != NULL) {
            memcpy(name, arglist[i], name_len);
            name[name_len] = '\0';
            set_variable(name, eq + 1);
            free(name);
        }
    }
    return 1;
}

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

static int execute_line(const char* raw_line);

static int process_if_block(const char* first_line) {
    char* line_copy = strdup(first_line);
    if (!line_copy) {
        perror("strdup");
        return 1;
    }

    char* trimmed = trim_line(line_copy);
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

    if (starts_with_keyword(trimmed, "if")) {
        int status = process_if_block(trimmed);
        free(copy);
        return status;
    }

    int status = 0;
    char* normalized = pad_operators(trimmed);
    if (normalized != NULL) {
        char** arglist = tokenize_quote_aware(normalized);
        if (arglist != NULL) {
            // Step 1: Expand $VARNAME references before stripping quotes or executing
            expand_variables(arglist);

            // Step 2: Strip surrounding quotes from arguments and variable values
            strip_quotes(arglist);

            // Step 3: Check if this command is a VARNAME=value assignment
            if (handle_variable_assignment(arglist)) {
                free_arglist(arglist);
                free(normalized);
                free(copy);
                return 0;
            }

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
    free_variables();
    clear_history();
    printf("\n");
    return 0;
}
