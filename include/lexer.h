#pragma once

#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    char ** items;
    size_t size;
} tokenlist;

typedef struct {
    char *commands[3];  // Store last 3 commands
    int count;          // Current number of commands stored
} command_history_t;

char * get_input(void);
tokenlist * get_tokens(char *input);
tokenlist * new_tokenlist(void);
void add_token(tokenlist *tokens, char *item);
void free_tokens(tokenlist *tokens);
