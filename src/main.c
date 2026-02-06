#include <pwd.h>

#include "lexer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>

static char *expand_tilde(const char *tok);

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
       
	if(tok[0]=='~'){  //Tilde Expansion
	 char *newtok = expand_tilde(tok);
	 free(tokens->items[i]);
	 tokens->items[i]=newtok;
	 tok = tokens->items[i];}

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

/**
 * Executes an external command using fork/exec.
 */
void execute_command(char *cmd_path, tokenlist *tokens) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        // Child process: build argv and execute
        char **argv = malloc((tokens->size + 1) * sizeof(char *));
        if (argv == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }

        for (size_t i = 0; i < tokens->size; i++) {
            argv[i] = tokens->items[i];
        }
        argv[tokens->size] = NULL;

        execv(cmd_path, argv);
        perror("execv");
        free(argv);
        exit(EXIT_FAILURE);
    } else {
        // Parent process: wait for child
        int status;
        waitpid(pid, &status, 0);
    }
}

static char *expand_tilde(const char *tok)
{
	//Dont expand if invalid, not ~, or not ~/
	if(!tok || tok[0]!='~' || ( tok[1]!='\0' && tok[1]!='/' ))  
	 return strdup(tok);



	const char *home=getenv("HOME");
/*	if(!home || home[0]=='\0'){
	 struct passwd *pw =getpwuid(getuid());
	 if (pw) home = pw->pw_dir; }*/

        if(!home || home[0]=='\0') //Unable to expand
	 return strdup(tok);

       const char *rest = (tok[1] =='/') ? (tok+1) : ""; //Check if  ~/ or ~
	size_t length=strlen(home) + strlen(rest) +1;
	char *out=malloc(length);
	if(!out) return strdup(tok);  //Not enough space

	strcpy(out,home);
	strcat(out,rest);
	return out;}

int main(void) {
    while (1)
    {
        print_prompt();

        char *input = get_input();

        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }

        tokenlist *tokens = get_tokens(input);

        expand_tokens(tokens);

        if (tokens->size > 0) {
            char *cmd_path = search_path(tokens->items[0]);
            if (cmd_path != NULL) {
                execute_command(cmd_path, tokens);
                free(cmd_path);
            } else {
                printf("%s: command not found\n", tokens->items[0]);
            }
        }

        free(input);
        free_tokens(tokens);
    }

    return 0;
}
