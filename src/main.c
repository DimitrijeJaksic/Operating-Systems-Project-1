#include <pwd.h>

#include "lexer.h"
#include "job.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/stat.h>

static char *expand_tilde(const char *tok);
void add_job(job_list_t *jobs, pid_t pid, const char *cmd);

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
void execute_command(char *cmd_path, tokenlist *tokens, bool background, job_list_t *jobs) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        // Child process: execute
        char **argv = malloc((tokens->size + 1) * sizeof(char *));
        for (size_t i = 0; i < tokens->size; i++) {
            argv[i] = tokens->items[i];
        }
        argv[tokens->size] = NULL;
        execv(cmd_path, argv);
        perror("execv");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        if (background) {
            // Build command string from tokens
            char cmd_str[1024] = "";
            for (size_t i = 0; i < tokens->size; i++) {
                strncat(cmd_str, tokens->items[i], sizeof(cmd_str) - strlen(cmd_str) - 1);
                if (i < tokens->size - 1) {
                    strncat(cmd_str, " ", sizeof(cmd_str) - strlen(cmd_str) - 1);
                }
            }
            
            // Add to job list
            add_job(jobs, pid, cmd_str);
            printf("[%d] %d\n", jobs->jobs[jobs->count - 1].job_num, pid);
        } else {
            // Wait for child
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

void add_job(job_list_t *jobs, pid_t pid, const char *cmd) {
    if (jobs->count >= 10) return;
    
    jobs->jobs[jobs->count].job_num = jobs->next_job_num++;
    jobs->jobs[jobs->count].pid = pid;
    jobs->jobs[jobs->count].command = strdup(cmd);
    jobs->jobs[jobs->count].status = 0;  // Running
    jobs->count++;
}

void check_jobs(job_list_t *jobs) {
    for (int i = 0; i < jobs->count; i++) {
        int status;
        pid_t result = waitpid(jobs->jobs[i].pid, &status, WNOHANG);
        
        if (result > 0) {
            // Job completed
            printf("[%d] + complete %s\n", jobs->jobs[i].job_num, jobs->jobs[i].command);
            free(jobs->jobs[i].command);
            
            // Remove from list
            for (int j = i; j < jobs->count - 1; j++) {
                jobs->jobs[j] = jobs->jobs[j + 1];
            }
            jobs->count--;
            i--;
        }
    }
}

void add_to_history(command_history_t *history, const char *cmd) {
    // Shift commands if at capacity
    if (history->count == 3) {
        free(history->commands[0]);
        history->commands[0] = history->commands[1];
        history->commands[1] = history->commands[2];
        history->commands[2] = strdup(cmd);
    } else {
        history->commands[history->count] = strdup(cmd);
        history->count++;
    }
}

void display_history(command_history_t *history) {
    if (history->count == 0) {
        printf("No commands in history.\n");
        return;
    }
    
    for (int i = 0; i < history->count; i++) {
        printf("\t%s\n", history->commands[i]);
    }
}

void wait_for_jobs(job_list_t *jobs) {
    while (jobs->count > 0) {
        int status;
        // Blocking wait on the first job
        pid_t result = waitpid(jobs->jobs[0].pid, &status, 0);
        
        if (result > 0) {
            // Job completed
            printf("[%d] + complete %s\n", jobs->jobs[0].job_num, jobs->jobs[0].command);
            free(jobs->jobs[0].command);
            
            // Remove from list
            for (int j = 0; j < jobs->count - 1; j++) {
                jobs->jobs[j] = jobs->jobs[j + 1];
            }
            jobs->count--;
        }
    }
}

bool handle_builtin(tokenlist *tokens, job_list_t *jobs, command_history_t *history, bool *should_exit) {
    if (tokens->size == 0) {
        return false;
    }
    
    const char *cmd = tokens->items[0];
    
    // Handle 'exit' command
    if (strcmp(cmd, "exit") == 0) {
        printf("Waiting for background processes to complete...\n");
        wait_for_jobs(jobs);
        
        printf("Last valid commands:\n");
        display_history(history);
        
        *should_exit = true;
        return true;
    }
    
    // Handle 'cd' command
    if (strcmp(cmd, "cd") == 0) {
        if (tokens->size > 2) {
            printf("cd: too many arguments\n");
            return true;
        }
        
        const char *target_dir = (tokens->size == 2) ? tokens->items[1] : getenv("HOME");
        
        if (target_dir == NULL) {
            printf("cd: HOME not set\n");
            return true;
        }
        
        struct stat st;
        if (stat(target_dir, &st) != 0) {
            printf("cd: %s: No such file or directory\n", target_dir);
            return true;
        }
        
        if (!S_ISDIR(st.st_mode)) {
            printf("cd: %s: Not a directory\n", target_dir);
            return true;
        }
        
        if (chdir(target_dir) != 0) {
            perror("cd");
            return true;
        }
        
        return true;
    }
    
    // Handle 'jobs' command
    if (strcmp(cmd, "jobs") == 0) {
        if (jobs->count == 0) {
            printf("No active background processes.\n");
            return true;
        }
        
        for (int i = 0; i < jobs->count; i++) {
            printf("[%d]+ %d %s\n", jobs->jobs[i].job_num, jobs->jobs[i].pid, jobs->jobs[i].command);
        }
        
        return true;
    }
    
    return false;  // Not a built-in command
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
    job_list_t jobs = {0};
    jobs.next_job_num = 1;
    
    command_history_t history = {0};
    
    while (1) {
        check_jobs(&jobs);  // Check for completed background jobs
        
        print_prompt();
        char *input = get_input();

        tokenlist *tokens = get_tokens(input);
        expand_tokens(tokens);

        // Check for background execution
        bool is_background = false;
        if (tokens->size > 0 && strcmp(tokens->items[tokens->size - 1], "&") == 0) {
            is_background = true;
            free(tokens->items[tokens->size - 1]);
            tokens->size--;
        }

        if (tokens->size > 0) {
            // Record command to history before checking if it's valid
            char cmd_str[200] = "";
            for (size_t i = 0; i < tokens->size; i++) {
                strncat(cmd_str, tokens->items[i], sizeof(cmd_str) - strlen(cmd_str) - 1);
                if (i < tokens->size - 1) {
                    strncat(cmd_str, " ", sizeof(cmd_str) - strlen(cmd_str) - 1);
                }
            }
            if (is_background) {
                strncat(cmd_str, " &", sizeof(cmd_str) - strlen(cmd_str) - 1);
            }
            
            // Check for built-in commands first
            bool should_exit = false;
            if (!handle_builtin(tokens, &jobs, &history, &should_exit)) {
                // Not a built-in, try external command
                char *cmd_path = search_path(tokens->items[0]);
                if (cmd_path != NULL) {
                    // Add to history only if it's a valid command
                    add_to_history(&history, cmd_str);
                    execute_command(cmd_path, tokens, is_background, &jobs);
                    free(cmd_path);
                } else {
                    printf("%s: command not found\n", tokens->items[0]);
                }
            } else if (!should_exit) {
                // Built-in command executed (but not exit)
                add_to_history(&history, cmd_str);
            }
            
            if (should_exit) {
                free(input);
                free_tokens(tokens);
                break;
            }
        }

        free(input);
        free_tokens(tokens);
    }
    
    // Clean up history
    for (int i = 0; i < history.count; i++) {
        free(history.commands[i]);
    }

    return 0;
}
