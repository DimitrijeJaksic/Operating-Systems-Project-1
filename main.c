#include <pwd.h>
#include <fcntl.h>
#include <errno.h>


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
void pipeline(tokenlist *tokens, int pipe_count, bool background, job_list_t *jobs);

static int lexer_for_redirection(tokenlist *tokens, char **in_file, char **out_file);
void i_o_redirection( char *in_file, char *out_file);



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
void execute_command(char *cmd_path, tokenlist *tokens, bool background, job_list_t *jobs,char *in_file, char *out_file) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        // Child process: execute
        i_o_redirection(in_file,out_file);
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


static int lexer_for_redirection(tokenlist *tokens, char **in_file, char **out_file)
{
    *in_file = NULL;
    *out_file = NULL;

        //Scan all tokens to find < or >
    for (size_t i = 0; i < tokens->size; i++) {

        if (strcmp(tokens->items[i], "<") == 0) {//< found
            if (i + 1 >= tokens->size) {
                fprintf(stderr, " missing file name \n");
                return 0;
            }

            *in_file = strdup(tokens->items[i + 1]); //copy file name

            //fix token list
            free(tokens->items[i]);
            free(tokens->items[i + 1]);

            for (size_t j = i; j + 2 < tokens->size; j++)
                tokens->items[j] = tokens->items[j + 2];

            tokens->size -= 2;
            tokens->items[tokens->size] = NULL; 
            i--; 
        }

        else if (strcmp(tokens->items[i], ">") == 0) { //> found
            if (i + 1 >= tokens->size) {
                fprintf(stderr, " missing file name >\n");
                return 0;
            }

            *out_file = strdup(tokens->items[i + 1]);


            free(tokens->items[i]);
            free(tokens->items[i + 1]);

            for (size_t j = i; j + 2 < tokens->size; j++)
                tokens->items[j] = tokens->items[j + 2];

            tokens->size -= 2;
            tokens->items[tokens->size] = NULL;
            i--;
        }
    }

    return 1;
}

void i_o_redirection(char *in_file, char *out_file)
{
   
    if (in_file) {
        struct stat file_input;

        if (stat(in_file, &file_input) != 0) { //check if file exists and add it to structure
            fprintf(stderr, "input file error\n");
            exit(1);
        }

        if (!S_ISREG(file_input.st_mode)) {
            fprintf(stderr, " not a regular file\n");
            exit(1);
        }

        int fd = open(in_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "input file open failed\n");
            exit(1);
        }

        dup2(fd, 0);   // stdin
        close(fd);
    }

    
    if (out_file) {
        int fd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC,
				 S_IRUSR | S_IWUSR);
        if (fd < 0) {
            fprintf(stderr, "output file open failed\n");
            exit(1);
        }

        dup2(fd, 1);   // stdout
        close(fd);
    }
}

void pipeline(tokenlist *tokens, int pipe_count,bool background, job_list_t *jobs){

	int cmd_count=pipe_count+1;
	int pipes[2][2]; //2 fd per pipe	
	pid_t pids[3];

	//make pipes
	for(int i=0;i<pipe_count;i++)
		{if(pipe(pipes[i]) < 0){
		perror("pipe");
		return;}}

       //commands
      int cmd_start=0;
      int cmd_index=0;

     for(int i=0; i<= (int) tokens->size;i++){
        if(i==(int)tokens->size || strcmp(tokens->items[i],"|")==0){
	//end of 1 cmd found

	pid_t pid = fork();

	if(pid < 0 )
	{perror("fork");
	for(int j=0;j<pipe_count;j++)
	{close(pipes[j][0]);
	close(pipes[j][1]);}
	return;}

       if(pid==0){
	//child

       if(cmd_index > 0) //read from previous pipe
	dup2(pipes[cmd_index-1][0],STDIN_FILENO);
      if(cmd_index < pipe_count) //not last , write to next
	dup2(pipes[cmd_index][1],STDOUT_FILENO);

	//close pipes
      for(int j=0;j<pipe_count;j++)
        {close(pipes[j][0]);
        close(pipes[j][1]);}

	//build argv
	int argc= i - cmd_start;
       char **argv=malloc((argc+1) * sizeof(char *));
	for(int a=0; a<argc; a++){
		argv[a]=tokens->items[cmd_start+a];}
	argv[argc]=NULL;
	char *cmd_path=search_path(argv[0]);
	execv(cmd_path,argv);
	exit(1);
}
	//parent process
	pids[cmd_index]=pid;
	cmd_index++;
	cmd_start=i+1;
}}

 for (int p = 0; p < pipe_count; p++) {
        close(pipes[p][0]);
        close(pipes[p][1]);
    }

 if (!background) {
        for (int i = 0; i < cmd_count; i++) {
            waitpid(pids[i], NULL, 0);
        }
    } else {
        add_job(jobs, pids[cmd_count - 1], "pipeline");
        printf("[%d] %d\n",
               jobs->jobs[jobs->count - 1].job_num,
               pids[cmd_count - 1]);
    }
}

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

	
       //check for pipes
	int pipe_count=0;
	for(size_t i=0; i <tokens->size; i++)
	 if(strcmp(tokens->items[i],"|")==0)
		pipe_count++;

	if(pipe_count > 0){

	  if(pipe_count > 2){
           fprintf(stderr, "Max two pipes\n");
           free(input);
           free_tokens(tokens);
          continue;}
       
        bool is_background = false;
        if (tokens->size > 0 && strcmp(tokens->items[tokens->size - 1], "&") == 0) {
            is_background = true;
            free(tokens->items[tokens->size - 1]);
            tokens->size--;
        }

       pipeline(tokens,pipe_count,is_background,&jobs);

       free(input);
       free_tokens(tokens);
      continue;} 

        // Check for background execution
        bool is_background = false;
        if (tokens->size > 0 && strcmp(tokens->items[tokens->size - 1], "&") == 0) {
            is_background = true;
            free(tokens->items[tokens->size - 1]);
            tokens->size--;
        }

	char *in_file = NULL;
	char *out_file = NULL;

	//preventing memory leaks if < or > used withouth file name
	if (lexer_for_redirection(tokens, &in_file, &out_file) == 0) {
            free(in_file);
            free(out_file);
            free(input);
            free_tokens(tokens);
            continue;}


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
                    execute_command(cmd_path, tokens, is_background, &jobs,in_file,out_file);
                    free(cmd_path);
                } else {
                    printf("%s: command not found\n", tokens->items[0]);
                }
            } else if (!should_exit) {
                // Built-in command executed (but not exit)
                add_to_history(&history, cmd_str);
            }
            
            if (should_exit) {
                free(in_file);
                free(out_file);
	        free(input);
                free_tokens(tokens);
                break;
            }
        }
        free(in_file);
        free(out_file);
        free(input);
        free_tokens(tokens);
    }
    
    // Clean up history
    for (int i = 0; i < history.count; i++) {
        free(history.commands[i]);
    }

    return 0;
}
