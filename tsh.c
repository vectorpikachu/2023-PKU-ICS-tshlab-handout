/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and login ID here>
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdarg.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF         0   /* undefined */
#define FG            1   /* running in foreground */
#define BG            2   /* running in background */
#define ST            3   /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Parsing states */
#define ST_NORMAL   0x0   /* next token is an argument */
#define ST_INFILE   0x1   /* next token is the input file */
#define ST_OUTFILE  0x2   /* next token is the output file */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */
int pipe_fd[2];             /* 管道的文件描述符 */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t job_list[MAXJOBS]; /* The job list */

struct cmdline_tokens {
    int argc;               /* Number of arguments */
    char *argv[MAXARGS];    /* The arguments list */
    char *infile;           /* The input file */
    char *outfile;          /* The output file */
    enum builtins_t {       /* Indicates if argv[0] is a builtin command */
        BUILTIN_NONE,
        BUILTIN_QUIT,
        BUILTIN_JOBS,
        BUILTIN_BG,
        BUILTIN_FG,
        BUILTIN_KILL,
        BUILTIN_NOHUP} builtins;
};

/* End global variables */

/* Function prototypes */
void eval(char *cmdline);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, struct cmdline_tokens *tok); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *job_list);
int maxjid(struct job_t *job_list); 
int addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *job_list, pid_t pid); 
pid_t fgpid(struct job_t *job_list);
struct job_t *getjobpid(struct job_t *job_list, pid_t pid);
struct job_t *getjobjid(struct job_t *job_list, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *job_list, int output_fd);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
ssize_t sio_puts(char s[]);
ssize_t sio_putl(long v);
ssize_t sio_put(const char *fmt, ...);
void sio_error(char s[]);
pid_t Fork(void); // Fork的错误处理包装函数
int builtin_cmd(char **argv, struct  cmdline_tokens *tok); // 判断是否是内建命令的函数
void Sigfillset(sigset_t *set);
void Sigemptyset(sigset_t *set);
void Sigaddset(sigset_t *set, int signum);
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
void Sigsuspend(const sigset_t *mask);
void Execve(const char *filename, char *const argv[], char *const envp[]);
int Kill(pid_t pid, int signum);
void waitfg(pid_t pid);
void conduct_bgfg(char **argv);
int Dup2(int oldfd, int newfd);
void conduct_kill(char **argv);

typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);


/*
 * main - The shell's main routine 
 */
int 
main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];    /* cmdline for fgets */
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
            break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
            break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
            break;
        default:
            usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */
    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(job_list);

    /* Execute the shell's read/eval loop */
    while (1) {

        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { 
            /* End of file (ctrl-d) */
            printf ("\n");
            fflush(stdout);
            fflush(stderr);
            exit(0);
        }
        
        /* Remove the trailing newline */
        cmdline[strlen(cmdline)-1] = '\0';
        
        /* Evaluate the command line */
        eval(cmdline);
        
        fflush(stdout);
        fflush(stdout);
    } 
    
    exit(0); /* control never reaches here */
}

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 */
void 
eval(char *cmdline) 
{
    int bg;              /* should the job run in bg or fg? */
    struct cmdline_tokens tok;
    pid_t pid;
    /* Parse command line */
    bg = parseline(cmdline, &tok); 
    if (bg == -1) /* parsing error */
        return;
    if (tok.argv[0] == NULL) /* ignore empty lines */
        return;

    // 在调用Fork之前，先屏蔽SIGCHLD信号
    sigset_t mask_all, mask_one, prev_one;
    Sigfillset(&mask_all);
    Sigemptyset(&mask_one);
    Sigaddset(&mask_one, SIGCHLD);

    if (pipe(pipe_fd) < 0) { // 创建管道
        unix_error("pipe error");
        exit(EXIT_FAILURE);
    }

    if (!builtin_cmd(tok.argv, &tok)) {
        // 如果不是内建命令，那么就fork一个子进程
        Sigprocmask(SIG_BLOCK, &mask_one, &prev_one); // 在fork之前，先屏蔽SIGCHLD信号
        if ((pid = Fork()) == 0) {
            // 当前是在子进程里了
            Sigprocmask(SIG_SETMASK, &prev_one, NULL);  // 解除屏蔽

            // 关于重定向的部分应该写在子进程里面
            // 如果有重定向的话，那么就需要先打开文件
            int fd_in = -1, fd_out = -1;
            if (tok.infile != NULL) {
                fd_in = open(tok.infile, O_RDONLY);
                if (fd_in < 0) {
                    printf("%s: No such file or directory\n", tok.infile);
                    fflush(stdout);
                    exit(EXIT_FAILURE);
                }
            }
            if (tok.outfile != NULL) {
                fd_out = open(tok.outfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
                if (fd_out < 0) {
                        printf("%s: No such file or directory\n", tok.outfile);
                        fflush(stdout);
                        exit(EXIT_FAILURE);
                }
            }

            // 从重定向的文件中读取输入
            if (fd_in != -1) {
                Dup2(fd_in, STDIN_FILENO);
                close(fd_in);
            }
            // 将输出重定向到文件中
            if (fd_out != -1) {
                Dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }

            // 设置子进程的进程组
            // After the fork, but before the execve, the child process should call
            // setpgid(0, 0), which puts the child in a new process group whose group ID is identical to the
            // child’s PID. This ensures that there will be only one process, your shell, in the foreground process
            // group. 
            setpgid(0, 0);
            fflush(stdout);
            // 一定要等到父进程设置完子进程的进程组之后，才能执行execve
            close(pipe_fd[1]); // 关闭管道的写端，因为只读
            // 等待父进程的通知
            char dummy;
            while (read(pipe_fd[0], &dummy, 1) != 0) { }
            close(pipe_fd[0]);
            
            // 执行命令
            Execve(tok.argv[0], tok.argv, environ);
            _exit(0);
        }
        else {
            // 父进程
            // 如果是后台进程，那么就不需要等待子进程结束
            if (bg) {
                Sigprocmask(SIG_BLOCK, &mask_all, NULL); // 在添加到job_list之前，先屏蔽所有信号
                // 添加到job_list
                addjob(job_list, pid, BG, cmdline);
                fflush(stdout);
                // 通知子进程可以执行execve了
                close(pipe_fd[0]); // 关闭管道的读端，因为只写
                char dummy = 'a';
                write(pipe_fd[1], &dummy, 1);
                close(pipe_fd[1]);

                Sigprocmask(SIG_SETMASK, &mask_one, NULL);  // 解除屏蔽
                // 打印信息
                printf("[%d] (%d) %s\n", pid2jid(pid), pid, cmdline);
                fflush(stdout);
            }
            else {
                Sigprocmask(SIG_BLOCK, &mask_all, NULL); // 在添加到job_list之前，先屏蔽所有信号
                // 如果是前台进程，那么就需要等待子进程结束
                addjob(job_list, pid, FG, cmdline);
                // printf("[%d] (%d) %s\n", pid2jid(pid), pid, cmdline); // 确认是加入了job_list，而且在收到SIGCHLD信号之前，不会有其他进程修改job_list
                fflush(stdout);

                // 通知子进程可以执行execve了
                close(pipe_fd[0]); // 关闭管道的读端，因为只写
                char dummy = 'a';
                write(pipe_fd[1], &dummy, 1);
                close(pipe_fd[1]);

                // 等待前台进程结束
                Sigprocmask(SIG_SETMASK, &mask_one, NULL);  // 解除屏蔽
                waitfg(pid);
            }
        }
        Sigprocmask(SIG_SETMASK, &prev_one, NULL);  // 解除屏蔽
    }
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Parameters:
 *   cmdline:  The command line, in the form:
 *
 *                command [arguments...] [< infile] [> oufile] [&]
 *
 *   tok:      Pointer to a cmdline_tokens structure. The elements of this
 *             structure will be populated with the parsed tokens. Characters 
 *             enclosed in single or double quotes are treated as a single
 *             argument. 
 * Returns:
 *   1:        if the user has requested a BG job
 *   0:        if the user has requested a FG job  
 *  -1:        if cmdline is incorrectly formatted
 * 
 * Note:       The string elements of tok (e.g., argv[], infile, outfile) 
 *             are statically allocated inside parseline() and will be 
 *             overwritten the next time this function is invoked.
 */
int 
parseline(const char *cmdline, struct cmdline_tokens *tok) 
{

    static char array[MAXLINE];          /* holds local copy of command line */
    const char delims[10] = " \t\r\n";   /* argument delimiters (white-space) */
    char *buf = array;                   /* ptr that traverses command line */
    char *next;                          /* ptr to the end of the current arg */
    char *endbuf;                        /* ptr to end of cmdline string */
    int is_bg;                           /* background job? */

    int parsing_state;                   /* indicates if the next token is the
                                            input or output file */

    if (cmdline == NULL) {
        (void) fprintf(stderr, "Error: command line is NULL\n");
        return -1;
    }

    (void) strncpy(buf, cmdline, MAXLINE);
    endbuf = buf + strlen(buf);

    tok->infile = NULL;
    tok->outfile = NULL;

    /* Build the argv list */
    parsing_state = ST_NORMAL;
    tok->argc = 0;

    while (buf < endbuf) {
        /* Skip the white-spaces */
        buf += strspn (buf, delims);
        if (buf >= endbuf) break;

        /* Check for I/O redirection specifiers */
        if (*buf == '<') {
            if (tok->infile) {
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
            }
            parsing_state |= ST_INFILE;
            buf++;
            continue;
        }
        if (*buf == '>') {
            if (tok->outfile) {
                (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
                return -1;
            }
            parsing_state |= ST_OUTFILE;
            buf ++;
            continue;
        }

        if (*buf == '\'' || *buf == '\"') {
            /* Detect quoted tokens */
            buf++;
            next = strchr (buf, *(buf-1));
        } else {
            /* Find next delimiter */
            next = buf + strcspn (buf, delims);
        }
        
        if (next == NULL) {
            /* Returned by strchr(); this means that the closing
               quote was not found. */
            (void) fprintf (stderr, "Error: unmatched %c.\n", *(buf-1));
            return -1;
        }

        /* Terminate the token */
        *next = '\0';

        /* Record the token as either the next argument or the i/o file */
        switch (parsing_state) {
        case ST_NORMAL:
            tok->argv[tok->argc++] = buf;
            break;
        case ST_INFILE:
            tok->infile = buf;
            break;
        case ST_OUTFILE:
            tok->outfile = buf;
            break;
        default:
            (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
            return -1;
        }
        parsing_state = ST_NORMAL;

        /* Check if argv is full */
        if (tok->argc >= MAXARGS-1) break;

        buf = next + 1;
    }

    if (parsing_state != ST_NORMAL) {
        (void) fprintf(stderr,
                       "Error: must provide file name for redirection\n");
        return -1;
    }

    /* The argument list must end with a NULL pointer */
    tok->argv[tok->argc] = NULL;

    if (tok->argc == 0)  /* ignore blank line */
        return 1;

    if (!strcmp(tok->argv[0], "quit")) {                 /* quit command */
        tok->builtins = BUILTIN_QUIT;
    } else if (!strcmp(tok->argv[0], "jobs")) {          /* jobs command */
        tok->builtins = BUILTIN_JOBS;
    } else if (!strcmp(tok->argv[0], "bg")) {            /* bg command */
        tok->builtins = BUILTIN_BG;
    } else if (!strcmp(tok->argv[0], "fg")) {            /* fg command */
        tok->builtins = BUILTIN_FG;
    } else if (!strcmp(tok->argv[0], "kill")) {          /* kill command */
        tok->builtins = BUILTIN_KILL;
    } else if (!strcmp(tok->argv[0], "nohup")) {            /* kill command */
        tok->builtins = BUILTIN_NOHUP;
    } else {
        tok->builtins = BUILTIN_NONE;
    }

    /* Should the job run in the background? */
    if ((is_bg = (*tok->argv[tok->argc-1] == '&')) != 0)
        tok->argv[--tok->argc] = NULL;

    return is_bg;
}


/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP, SIGTSTP, SIGTTIN or SIGTTOU signal. The 
 *     handler reaps all available zombie children, but doesn't wait 
 *     for any other currently running children to terminate.  
 */
void 
sigchld_handler(int sig) 
{
    // P536 要保存和恢复errno
    int olderrno = errno;
    int status; // waitpid的一个参数
    pid_t pid;
    sigset_t mask_all, prev_all; 
    // 如果处理程序和主程序共享一个全局数据结构，那么就需要在处理程序中屏蔽所有信号
    Sigfillset(&mask_all);

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        // WNOHANG | WUNTRACED: 如果没有子进程终止或者停止，那么waitpid就会立即返回0
        // 如果有子进程终止或者停止，那么waitpid就会返回子进程的pid
        Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        if (WIFEXITED(status)) {
            // 如果子进程正常终止，那么就删除job_list中的记录
            deletejob(job_list, pid);
            fflush(stdout);
        }
        else if (WIFSIGNALED(status)) {
            // 如果子进程是因为信号终止的，那么就打印信息
            // printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
            // 在信号处理程序里不可以使用异步信号不安全的函数，比如printf
            // 所以使用sio_put来代替printf
            // WTERMSIG(status)返回导致子进程终止的信号的编号
            sio_puts("Job ["); // 因为sio_put不支持%d，所以只能一个一个输出
            sio_putl(pid2jid(pid));
            sio_puts("] (");
            sio_putl(pid);
            sio_puts(") terminated by signal ");
            sio_putl(WTERMSIG(status));
            sio_puts("\n");
            fflush(stdout);
            // 然后删除job_list中的记录
            deletejob(job_list, pid);
            fflush(stdout);
            // trace13 passed
        }
        else if (WIFSTOPPED(status)) {
            // 如果子进程是因为信号停止的，那么就打印信息
            // printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
            sio_puts("Job [");
            sio_putl(pid2jid(pid));
            sio_puts("] (");
            sio_putl(pid);
            sio_puts(") stopped by signal ");
            sio_putl(WSTOPSIG(status)); // 和WTERMSIG一样，返回导致子进程停止的信号的编号
            sio_puts("\n");
            fflush(stdout);
            // 然后修改job_list中的记录
            getjobpid(job_list, pid)->state = ST;
            fflush(stdout);
            // trace14 passed
        }
        Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }

    errno = olderrno;
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void 
sigint_handler(int sig) 
{
    // 中断当前前台进程组
    int olderrno = errno;
    pid_t pid;
    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);
    Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    pid = fgpid(job_list);
    if (pid != 0) {
        Sigprocmask(SIG_SETMASK, &mask_all, &prev_all);
        // 如果当前有前台进程，那么就中断它
        Kill(-pid, sig);
        fflush(stdout);
    }
    errno = olderrno;
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void 
sigtstp_handler(int sig) 
{
    // trace 09 passed
    // 停止当前前台进程组
    int olderrno = errno;
    pid_t pid;
    sigset_t mask_all, prev_all;
    Sigfillset(&mask_all);
    Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);

    pid = fgpid(job_list);
    if (pid != 0) {
        Sigprocmask(SIG_SETMASK, &prev_all, NULL);
        // 如果当前有前台进程，那么就停止它,而且应该是停止一个组的
        Kill(-pid, sig);
        fflush(stdout);
    }
    errno = olderrno;
    return;
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void 
sigquit_handler(int sig) 
{
    sio_error("Terminating after receipt of SIGQUIT signal\n");
}



/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void 
clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void 
initjobs(struct job_t *job_list) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&job_list[i]);
}

/* maxjid - Returns largest allocated job ID */
int 
maxjid(struct job_t *job_list) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].jid > max)
            max = job_list[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int 
addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline) 
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == 0) {
            job_list[i].pid = pid;
            job_list[i].state = state;
            job_list[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(job_list[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n",
                       job_list[i].jid,
                       job_list[i].pid,
                       job_list[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int 
deletejob(struct job_t *job_list, pid_t pid) 
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (job_list[i].pid == pid) {
            clearjob(&job_list[i]);
            nextjid = maxjid(job_list)+1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t 
fgpid(struct job_t *job_list) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].state == FG)
            return job_list[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t 
*getjobpid(struct job_t *job_list, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].pid == pid)
            return &job_list[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *job_list, int jid) 
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].jid == jid)
            return &job_list[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int 
pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (job_list[i].pid == pid) {
            return job_list[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void 
listjobs(struct job_t *job_list, int output_fd) // trace07 passed
{
    int i;
    char buf[MAXLINE << 2];

    for (i = 0; i < MAXJOBS; i++) {
        memset(buf, '\0', MAXLINE);
        if (job_list[i].pid != 0) {
            sprintf(buf, "[%d] (%d) ", job_list[i].jid, job_list[i].pid);
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
            memset(buf, '\0', MAXLINE);
            switch (job_list[i].state) {
            case BG:
                sprintf(buf, "Running    ");
                break;
            case FG:
                sprintf(buf, "Foreground ");
                break;
            case ST:
                sprintf(buf, "Stopped    ");
                break;
            default:
                sprintf(buf, "listjobs: Internal error: job[%d].state=%d ",
                        i, job_list[i].state);
            }
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
            memset(buf, '\0', MAXLINE);
            sprintf(buf, "%s\n", job_list[i].cmdline);
            if(write(output_fd, buf, strlen(buf)) < 0) {
                fprintf(stderr, "Error writing to output file\n");
                exit(1);
            }
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void 
usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void 
unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void 
app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/* Private sio_functions */
/* sio_reverse - Reverse a string (from K&R) */
static void sio_reverse(char s[])
{
    int c, i, j;

    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* sio_ltoa - Convert long to base b string (from K&R) */
static void sio_ltoa(long v, char s[], int b) 
{
    int c, i = 0;
    
    do {  
        s[i++] = ((c = (v % b)) < 10)  ?  c + '0' : c - 10 + 'a';
    } while ((v /= b) > 0);
    s[i] = '\0';
    sio_reverse(s);
}

/* sio_strlen - Return length of string (from K&R) */
static size_t sio_strlen(const char s[])
{
    int i = 0;

    while (s[i] != '\0')
        ++i;
    return i;
}

/* sio_copy - Copy len chars from fmt to s (by Ding Rui) */
void sio_copy(char *s, const char *fmt, size_t len)
{
    if(!len)
        return;

    for(size_t i = 0;i < len;i++)
        s[i] = fmt[i];
}

/* Public Sio functions */
ssize_t sio_puts(char s[]) /* Put string */
{
    return write(STDOUT_FILENO, s, sio_strlen(s));
}

ssize_t sio_putl(long v) /* Put long */
{
    char s[128];
    
    sio_ltoa(v, s, 10); /* Based on K&R itoa() */ 
    return sio_puts(s);
}

ssize_t sio_put(const char *fmt, ...) // Put to the console. only understands %d
{
  va_list ap;
  char str[MAXLINE]; // formatted string
  char arg[128];
  const char *mess = "sio_put: Line too long!\n";
  int i = 0, j = 0;
  int sp = 0;
  int v;

  if (fmt == 0)
    return -1;

  va_start(ap, fmt);
  while(fmt[j]){
    if(fmt[j] != '%'){
        j++;
        continue;
    }

    sio_copy(str + sp, fmt + i, j - i);
    sp += j - i;

    switch(fmt[j + 1]){
    case 0:
    		va_end(ap);
        if(sp >= MAXLINE){
            write(STDOUT_FILENO, mess, sio_strlen(mess));
            return -1;
        }
        
        str[sp] = 0;
        return write(STDOUT_FILENO, str, sp);

    case 'd':
        v = va_arg(ap, int);
        sio_ltoa(v, arg, 10);
        sio_copy(str + sp, arg, sio_strlen(arg));
        sp += sio_strlen(arg);
        i = j + 2;
        j = i;
        break;

    case '%':
        sio_copy(str + sp, "%", 1);
        sp += 1;
        i = j + 2;
        j = i;
        break;

    default:
        sio_copy(str + sp, fmt + j, 2);
        sp += 2;
        i = j + 2;
        j = i;
        break;
    }
  } // end while

  sio_copy(str + sp, fmt + i, j - i);
  sp += j - i;

	va_end(ap);
  if(sp >= MAXLINE){
    write(STDOUT_FILENO, mess, sio_strlen(mess));
    return -1;
  }
  
  str[sp] = 0;
  return write(STDOUT_FILENO, str, sp);
}

void sio_error(char s[]) /* Put error message and exit */
{
    sio_puts(s);
    _exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t 
*Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}
/*
 * Fork - fork函数的包装函数 
 */
pid_t Fork(void)
{
    pid_t pid; // 书上介绍的Fork的错误处理包装函数
    if((pid = fork()) < 0)
        unix_error("Fork error");
    return pid;
}

/*
 * builtin_cmd - 如果是内建命令，那么就直接执行，如果不是，那么就返回0
 */
int builtin_cmd(char **argv, struct cmdline_tokens * tok) // 书上介绍的判断是否是内建命令的函数
{
    if(!strcmp(argv[0], "quit")) // quit命令直接结束shell
        exit(0); // trace01
    else if(!strcmp(argv[0], "jobs")) {
        // 重定向到文件中
        if(tok->outfile != NULL) {
            int fd_out = open(tok->outfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
            if(fd_out < 0) {
                printf("%s: No such file or directory\n", tok->outfile);
                fflush(stdout);
                return 1;
            }
            // printf("fd_out: %d\n", fd_out);
            listjobs(job_list, fd_out);
            fflush(stdout);
            close(fd_out);
            // trace 23.24 passed
        }
        else 
            listjobs(job_list, STDOUT_FILENO); // 使用标准输出来输出所有的jobs
        fflush(stdout);
        // trace07 passed
        return 1;
    }
    else if(!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) {
        conduct_bgfg(argv); // 这里面只可能会打印错误，我们不能把错误打印到文件中
        return 1;
    }
    else if(!strcmp(argv[0], "kill")) {
        conduct_kill(argv);
        return 1;
    }
    else if(!strcmp(argv[0], "nohup")) {
        // 只要对外部命令解决这个问题就好了
        // 让跟在后面的命令忽略SIGHUP信号
        Signal(SIGHUP, SIG_IGN);
        return 1;
    }
    else
        return 0;
}

/*
 * Sigfillset - sigfillset函数的包装函数
 */
void Sigfillset(sigset_t *set)
{
    if(sigfillset(set) < 0)
        unix_error("Sigfillset error");
    fflush(stdout);
}

/*
 * Sigemptyset - sigemptyset函数的包装函数
 */
void Sigemptyset(sigset_t *set)
{
    if(sigemptyset(set) < 0)
        unix_error("Sigemptyset error");
    fflush(stdout);
}

/*
 * Sigaddset - sigaddset函数的包装函数
 */
void Sigaddset(sigset_t *set, int signum)
{
    if(sigaddset(set, signum) < 0)
        unix_error("Sigaddset error");
    fflush(stdout);
}

/*
 * Sigprocmask - sigprocmask函数的包装函数
 */
void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    if(sigprocmask(how, set, oldset) < 0)
        unix_error("Sigprocmask error");
    fflush(stdout);
}

/*
 * Setsuspend - setsuspend函数的包装函数
 */
void Sigsuspend(const sigset_t *mask)
{
    // write(STDOUT_FILENO, "sigsuspend\n", 11);
    if(sigsuspend(mask) != -1)
        unix_error("Sigsuspend error");
    fflush(stdout);
}

/*
 * Kill - kill函数的包装函数
 */
int Kill(pid_t pid, int signum)
{
    int success;
    if((success = kill(pid, signum)) < 0)
        unix_error("Kill error");
    fflush(stdout);
    return success;
}

/*
 * Execve - execve函数的包装函数
 */
void Execve(const char *filename, char *const argv[], char *const envp[])
{
    // printf("filename: %s\n", filename);
    // printf("argv[0]: %s\n", argv[0]);

    if(execve(filename, argv, envp) < 0)
        printf("%s: Command not found\n", filename);
    fflush(stdout);
}

/*
 * Waitpid - waitpid函数的包装函数
 */
void waitfg(pid_t pid)
{
    sigset_t mask_all;
    Sigemptyset(&mask_all);
    while(fgpid(job_list) != 0)
        Sigsuspend(&mask_all);
    // write(STDOUT_FILENO, "waitfg finished\n", 16);
    fflush(stdout);
    return;
}


/*
 * conduct_bgfg - 执行bg和fg命令。
 * bg(pid) or bg(%jid) 这个命令需要先修改属性，然后发送SIGCONT信号
 * fg(pid) or fg(%jid) 这个命令需要先修改属性，然后发送SIGCONT信号，然后等待前台进程结束
 */
void conduct_bgfg(char **argv) {
    // IDs should be denoted on the command line by the prefix ’%’. 
    // For example, “%5” denotes JID 5, and “5” denotes PID 5
    // 同时通过发送SIGCONT信号来恢复进程组，也就是一个job，但是给的表示这个job的参数不同
    struct job_t *job;
    char *id = argv[1]; // id是一个字符串，到底是JID还是PID
    if (id[0] == '%') {
        // JID
        int jid = atoi(id + 1);
        job = getjobjid(job_list, jid);
        if (job == NULL) {
            printf("%s: No such job\n", id);
            fflush(stdout);
            return;
        }
    }
    else {
        // PID
        pid_t pid = atoi(id);
        job = getjobpid(job_list, pid);
        if (job == NULL) {
            printf("(%s): No such process\n", id);
            fflush(stdout);
            return;
        }
    }

    // 如果是bg命令，那么就把job的状态改为BG
    if (!strcmp(argv[0], "bg")) {
        job->state = BG;
        printf("[%d] (%d) %s\n", job->jid, job->pid, job->cmdline);
        fflush(stdout);
        // 使用kill发送信号
        Kill(-(job->pid), SIGCONT); // 给当前的进程组发送SIGCONT信号
        fflush(stdout);
    }
    else {
        // 如果是fg命令，那么就把job的状态改为FG
        job->state = FG;
        // 使用kill发送信号
        Kill(-(job->pid), SIGCONT); // 给当前的进程组发送SIGCONT信号
        fflush(stdout);
        waitfg(job->pid);
    }
    return;
}

/*
 * Dup2 - dup2函数的包装函数
 */
int Dup2(int oldfd, int newfd)
{
    int success;
    if((success = dup2(oldfd, newfd)) < 0)
        unix_error("Dup2 error");
    return success;
}

/*
 * conduct_kill - 执行kill命令
 * 通过kill发送SIGTERM信号
 */
void conduct_kill(char **argv) {
    struct job_t *job;
    char *id = argv[1]; // id是一个字符串，到底是JID还是PID
    if (id[0] == '%') {
        // JID
        if (id[1] == '-') {
            // 要通过杀死jid为首的进程组
            int jid = atoi(id + 2);
            job = getjobjid(job_list, jid);
            if (job == NULL) {
                printf("%%%s: No such process group\n", id+2);
                fflush(stdout);
                return;
            }
            Kill(-(job->pid), SIGTERM);
            fflush(stdout);
        }
        else {
            // 要通过杀死jid为首的进程
            int jid = atoi(id + 1);
            job = getjobjid(job_list, jid);
            if (job == NULL) {
                printf("%s: No such job\n", id);
                fflush(stdout);
                return;
            }
            Kill(job->pid, SIGTERM);
        }
    }
    else {
        // PID
        if (id[0] == '-') {
            // 要通过杀死pid为首的进程组
            pid_t pid = atoi(id + 1);
            job = getjobpid(job_list, pid);
            if (job == NULL) {
                printf("(%s): No such process group\n", id+1);
                fflush(stdout);
                return;
            }
            Kill(-(job->pid), SIGTERM);
        }
        else {
            // 要通过杀死pid为首的进程
            pid_t pid = atoi(id);
            job = getjobpid(job_list, pid);
            if (job == NULL) {
                printf("(%s): No such process\n", id);
                fflush(stdout);
                return;
            }
            Kill(job->pid, SIGTERM);
            fflush(stdout);
        }
    }
}