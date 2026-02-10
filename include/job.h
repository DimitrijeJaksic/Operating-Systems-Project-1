typedef struct {
    int job_num;          // Job number (1, 2, 3, ...)
    pid_t pid;            // Process ID
    char *command;        // Full command line
    int status;           // 0=running, non-zero=exit status
} job_t;

typedef struct {
    job_t jobs[10];       // Max 10 concurrent background jobs
    int count;            // Number of active jobs
    int next_job_num;     // Next job number to assign
} job_list_t;