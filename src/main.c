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

        free(input);
        free_tokens(tokens);
    }
}