#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct VarNode {
    char* name;
    char* value;
    struct VarNode* next;
} VarNode;

static VarNode* var_head = NULL;

void set_variable(const char* name, const char* value) {
    if (name == NULL || name[0] == '\0') {
        return;
    }
    const char* safe_val = (value != NULL) ? value : "";

    // 1. Update existing variable in place if it already exists
    VarNode* curr = var_head;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            char* new_val = strdup(safe_val);
            if (new_val == NULL) {
                perror("strdup");
                return;
            }
            free(curr->value);
            curr->value = new_val;
            return;
        }
        curr = curr->next;
    }

    // 2. Otherwise allocate and insert a new node at the head of the list
    VarNode* node = malloc(sizeof(VarNode));
    if (node == NULL) {
        perror("malloc");
        return;
    }

    node->name = strdup(name);
    node->value = strdup(safe_val);
    if (node->name == NULL || node->value == NULL) {
        perror("strdup");
        free(node->name);
        free(node->value);
        free(node);
        return;
    }

    node->next = var_head;
    var_head = node;
}

const char* get_variable(const char* name) {
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }

    VarNode* curr = var_head;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            return curr->value;
        }
        curr = curr->next;
    }
    return NULL;
}

void print_variables(void) {
    if (var_head == NULL) {
        printf("No shell variables defined.\n");
        return;
    }

    VarNode* curr = var_head;
    while (curr != NULL) {
        printf("%s=%s\n", curr->name, curr->value);
        curr = curr->next;
    }
}

void free_variables(void) {
    VarNode* curr = var_head;
    while (curr != NULL) {
        VarNode* next = curr->next;
        free(curr->name);
        free(curr->value);
        free(curr);
        curr = next;
    }
    var_head = NULL;
}
