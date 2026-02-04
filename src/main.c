#include "lexer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

void print_prompt(void)
{
    char* user = getenv("USER");

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    char pwd[PATH_MAX];
    getcwd(pwd, sizeof(pwd));

    printf("%s@%s:%s> ", user, hostname, pwd);
    fflush(stdout);
}

void expand_tokens(tokenlist *tokens) {
    for (size_t i = 0; i < tokens->size; i++) {
        char *tok = tokens->items[i];
        if (tok[0] == '$') {
            char *var_name = tok + 1; 
            char *value = getenv(var_name);

            if (value != NULL) {
                free(tokens->items[i]);                 
                tokens->items[i] = strdup(value);      
            }
            else {
                free(tokens->items[i]);
                tokens->items[i] = strdup("");
            }
        }
    }
}

/**
 * Searches for a command in $PATH directories.
 * Returns the full path if found, NULL otherwise.
 * Caller must free the returned string.
 */
char *search_path(const char *command) {
    // If command contains '/', don't search PATH
    if (strchr(command, '/') != NULL) {
        if (access(command, X_OK) == 0) {
            return strdup(command);
        }
        return NULL;
    }

    char *path_env = getenv("PATH");
    if (path_env == NULL) {
        return NULL;
    }

    // Make a copy since strtok modifies the string
    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");

    while (dir != NULL) {
        // Build full path: dir + "/" + command
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, command);

        // Check if file exists and is executable
        if (access(full_path, X_OK) == 0) {
            free(path_copy);
            return strdup(full_path);
        }

        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return NULL;
}


int main(void) {
    while (1)
    {
        print_prompt();

        char *input = get_input();

        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }

        printf("whole input: %s\n", input);

        tokenlist *tokens = get_tokens(input);

        expand_tokens(tokens);
        
        for (size_t i = 0; i < tokens->size; i++) {
            printf("token %zu: (%s)\n", i, tokens->items[i]);
        }

        // Search for command in PATH (if tokens exist)
        if (tokens->size > 0) {
            char *cmd_path = search_path(tokens->items[0]);
            if (cmd_path != NULL) {
                printf("Found command at: %s\n", cmd_path);
                free(cmd_path);
            } else {
                printf("%s: command not found\n", tokens->items[0]);
            }
        }

        free(input);
        free_tokens(tokens);
    }
}