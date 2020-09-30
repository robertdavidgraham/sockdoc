#define _CRT_SECURE_NO_WARNINGS 1 /* A microsoft thingy */
#include "util-workers.h"

#include "dns-parse.h"
#include "dns-format.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>



#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <malloc.h> /* alloca() */
#include <direct.h> /* getcwd() */
#define snprintf _snprintf
#define getcwd _getcwd

typedef struct worker_t
{
    HANDLE hProcess;
} worker_t;

typedef struct workers_t
{
    struct worker_t *children;
    size_t children_count;
    HANDLE parent_stdout;
    HANDLE parent_stderr;
    HANDLE child_stdout;
    HANDLE child_stderr;
} workers_t;

size_t workers_count(struct workers_t *workers)
{
    return workers->children_count;
}

/**
 * Wrap the complicated Windows equivalent of strerror().
 */
static char *my_strerror(DWORD err)
{
    __declspec(thread) static const char* msg = NULL;
    if (msg) {
         LocalFree(msg);
    }
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg,
        0, NULL );
   
    return msg;
}

struct workers_t *
workers_init(unsigned *max_children)
{
    SECURITY_ATTRIBUTES saAttr = {0};
    BOOL is_success;
    struct workers_t *t;
    
    t = calloc(1, sizeof(*t));
    if (t == NULL) {
        fprintf(stderr, "[-] out-of-memory\n");
        abort();
    }

    /* Allocate space to track all our spawned workers */
    t->children = calloc(*max_children + 1, sizeof(t->children[0]));
    if (t->children == NULL) {
        fprintf(stderr, "[-] out-of-memory\n");
        abort();
    }
    t->children_count = 0;

    /*
     * Set the inherit flag so that children can inherit handles
     */
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    /*
     * Create the pipes, but set the parent half the pipe to non-inheritable,
     * so the child only inherits their side of the pipe.
     */
    is_success = CreatePipe(&t->parent_stdout, &t->child_stdout, &saAttr, 0);
    if (!is_success) {
        fprintf(stderr, "[-] CreatePipe() %s\n", my_strerror(GetLastError()));
        exit(1);
    }
    is_success = SetHandleInformation(t->parent_stdout, HANDLE_FLAG_INHERIT, 0);
    if (!is_success) {
        fprintf(stderr, "[-] SetHandleInfo(!INHERIT) %s\n", my_strerror(GetLastError()));
        exit(1);
    }

    is_success = CreatePipe(&t->parent_stderr, &t->child_stderr, &saAttr, 0);
    if (!is_success) {
        fprintf(stderr, "[-] CreatePipe() %s\n", my_strerror(GetLastError()));
        exit(1);
    }
    is_success = SetHandleInformation(t->parent_stderr, HANDLE_FLAG_INHERIT, 0);
    if (!is_success) {
        fprintf(stderr, "[-] SetHandleInfo(!INHERIT) %s\n", my_strerror(GetLastError()));
        exit(1);
    }
    return t;
}
static int
workers_spawn(struct workers_t *t, const char *progname, size_t argc, va_list marker)
{
    PROCESS_INFORMATION proc_info = {0};
    STARTUPINFOA start_info = {0};
    BOOL is_success;
    char *command_line = NULL;
    size_t i;
    size_t offset;

    
    /*
     * Create the command-line from the arguments
     */
    offset = strlen(progname);
    command_line = realloc(command_line, offset + 1);
    memcpy(command_line, progname, offset + 1);

    for (i=0; i<argc; i++) {
        char *arg = va_arg(marker, char*);
        size_t arglen = strlen(arg);
        command_line = realloc(command_line, offset + 1 + arglen + 1 + 1);
        command_line[offset++] = ' ';
        memcpy(command_line + offset, arg, arglen + 1);
        offset += arglen;
    }

    
    /*
     * Configure which pipes the child will use
     */
   start_info.cb = sizeof(STARTUPINFOA);
   start_info.hStdError = t->child_stderr;
   start_info.hStdOutput = t->child_stdout;
   start_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
   start_info.dwFlags |= STARTF_USESTDHANDLES;

   is_success = CreateProcessA(
       NULL,
        command_line,     // command line
        NULL,          // process security attributes
        NULL,          // primary thread security attributes
        TRUE,          // handles are inherited
        CREATE_NO_WINDOW,// creation flags
        NULL,          // use parent's environment
        NULL,          // use parent's current directory
        &start_info,  // STARTUPINFO pointer
        &proc_info);  // receives PROCESS_INFORMATION
   if (!is_success) {
       fprintf(stderr, "[-] CreateProcessA() failed %d\n", (int)GetLastError());
       exit(1);
   }
   free(command_line);

    /* This should automatically reap zombies, by indicating
     * we are not interested in any return results from the
     * process, only their pipes */
    //CloseHandle(proc_info.hProcess);
    CloseHandle(proc_info.hThread);

    {
       struct spawned *child = &t->children[t->children_count++];
       child->hProcess = proc_info.hProcess;
    }
    
    return 0;
}


static HANDLE
my_echo(HANDLE h, FILE *fp, int *closed_count)
{
    BOOL is_success;
    char buf[1024];
    DWORD length;
    
    is_success = ReadFile(h, buf, sizeof(buf), &length, NULL);
    if (is_success) {
        fwrite(buf, 1, length, fp);
    } else {
        (*closed_count)++;
        CloseHandle(h);
        h = NULL;
    }
    return h;
}

/**
 * Reads input from child and parses the results
 */
static int
workers_read(struct workers_t *t, unsigned milliseconds,
             void (*write_stdout)(void *buf, size_t length, void *userdata),
             void (*write_stderr)(void *buf, size_t length, void *userdaata),
             void *userdata)
{
    size_t total_bytes_read = 0;
    int closed_count = 0;
    size_t i = 0;
    struct worker_t *children = t->children;
    size_t children_count = t->children_count;
    

    /*
     * Reap exited processes. Note that it only reaps a few processes
     * in each pass, rather than all possible processes.
     */
    i = 0;
    while (i < children_count) {
        HANDLE handles[MAXIMUM_WAIT_OBJECTS];
        DWORD handle_count = 0;
        DWORD result;

        for (; i<children_count; ) {
            handles[handle_count++] = children[i++].hProcess;
            if  (handle_count >= MAXIMUM_WAIT_OBJECTS)
                break;
        }

        /* Test to see if any processes have exited */
        result = WaitForMultipleObjects(handle_count, handles, FALSE, 0);
            
        /* If none have exited, then test the next batch */
        if (result == WAIT_TIMEOUT)
            continue;

        /* If there is a catostrophic failure, then print a message and
         * exit the program. This shouldn't be possible. */
        if (result == WAIT_FAILED) {
            fprintf(stderr, "[-] Wait() error: %s\n", my_strerror(GetLastError()));
            exit(1);
        }

        /* When the child process dies, it'll trigger this code below. We
         * want to simply close the handle and mark it close, so that we
         * know that we can open up new processes in its place */
        if (WAIT_OBJECT_0 <= result && result <= MAXIMUM_WAIT_OBJECTS) {
            size_t index;
            
            index = i;
            if (index % MAXIMUM_WAIT_OBJECTS == 0)
                index -= MAXIMUM_WAIT_OBJECTS;
            else
                index -= index % MAXIMUM_WAIT_OBJECTS;
            index += (result - WAIT_OBJECT_0);
            
            if (children[index].hProcess != handles[result - WAIT_OBJECT_0]) {
                fprintf(stderr, "bug\n");
                abort();
            }
            
            CloseHandle(children[index].hProcess);
            
            children[index].hProcess = NULL;

            closed_count++;
        }
    }

    /* Now wait for pipe input. All of the processes are writing to the same two
     * pipes. */
    for (;;) {
        char buffer[16384];
        DWORD length;
        BOOL is_success;
        DWORD combined_length = 0;

        if (PeekNamedPipe(t->parent_stdout, 0, 0, 0, &length, 0) && length) {
            is_success = ReadFile(t->parent_stdout, buffer, sizeof(buffer), &length, 0);
            if (is_success) {
                write_stdout(buffer, length, userdata);

                /* Remember this so we know if we need to sleep at the end of this function */
                total_bytes_read += length;

                /* Remember this so we know if we need to break out of this loop */
                combined_length += length;
            }
        }
        if (PeekNamedPipe(t->parent_stderr, 0, 0, 0, &length, 0) && length) {
            is_success = ReadFile(t->parent_stderr, buffer, sizeof(buffer), &length, 0);
            if (is_success) {
                wreite_stderr(buffer, length, userdata);

                /* Remember this so we know if we need to sleep at the end of this function */
                total_bytes_read += length;

                /* Remember this so we know if we need to break out of this loop */
                combined_length += length;
            }
        }

        /* Keep looping until there's nothing left to read from either pipe */
        if (combined_length == 0)
            break;
    }

    /* If there was no activity, then do a simple sleep so that we don't
     * burn through tons of CPU time */
    if (closed_count == 0 && total_bytes_read == 0)
        Sleep(milliseconds);

    /* Return the number of children that were closed, so that
     * the parent process can cleanup its tracking records */
    return closed_count;
}

/**
 * Called to cleanup any children records after their processes have
 * died. We simply move the entry at the end of the list to fill
 * the void of dead child.
 */
static void
workers_reap(struct workers_t *workers)
{
    size_t i;
    int count = 0;
    
    for (i = 0; i < workers->children_count; i++) {
        struct spawned *child = &workers->children[i];
        
        if (child->hProcess == NULL) {
            memcpy(child, &workers->children[workers->children_count - 1], sizeof(*child));
            workers->children_count--;
            i--;
            count++;
        }
    }
    return count;
}

#endif

#ifndef _WIN32
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

/**
 * A structure for tracking the spawned child program
 */
struct worker_t
{
    const char *name;
    int pid;
};

/* On Windows, we use a single set of pipes that we store here.
 * On POSIX, we are creating one pipe per process, though we
 * may change that model in the future */
typedef struct workers_t
{
    struct worker_t *children;
    size_t children_count;
    int parent_stdout;
    int parent_stderr;
    int child_stdout;
    int child_stderr;
    size_t count_closed;
} workers_t;

size_t workers_count(const struct workers_t *workers)
{
    return workers->children_count;
}

/* On POSIX, we want to disover the limits for filehandles and process
 * creation. */
struct workers_t *
workers_init(unsigned *max_children)
{
    struct rlimit limit;
    int err;
    struct workers_t *workers;
    
    workers = calloc(1, sizeof(*workers));
    if (workers == NULL) {
        fprintf(stderr, "[-] out-of-memory\n");
        abort();
    }
    
    /* Allocate space to track all our spawned workers */
    workers->children = calloc(*max_children + 1, sizeof(workers->children[0]));
    if (workers->children == NULL) {
        fprintf(stderr, "[-] out-of-memory\n");
        abort();
    }
    workers->children_count = 0;

    /* Discover how many child processes we can have active at
     * a time. */
#ifdef RLIMIT_NPROC
    err = getrlimit(RLIMIT_NPROC, &limit);
    if (err) {
        fprintf(stderr, "[-] getrlimit() %s\n", strerror(errno));
        exit(1);
    }
    if (*max_children > (unsigned)limit.rlim_max - 10 && limit.rlim_max > 10) {
        *max_children = (unsigned)limit.rlim_max - 10;
    }
    if (limit.rlim_cur + 10 < *max_children) {
        limit.rlim_cur = limit.rlim_max;
        setrlimit(RLIMIT_NPROC, &limit);
    }
#endif

    /* Discover how many file descriptors we can have open */
    err = getrlimit(RLIMIT_NOFILE, &limit);
    if (err) {
        fprintf(stderr, "[-] getrlimit() %s\n", strerror(errno));
        exit(1);
    }
    if (*max_children > (unsigned)limit.rlim_max/2 - 5 && limit.rlim_max > 10) {
        *max_children = (unsigned)limit.rlim_max/2 - 5;
    }
    if (limit.rlim_cur + 10 < *max_children * 2) {
        limit.rlim_cur = limit.rlim_max;
        setrlimit(RLIMIT_NOFILE, &limit);
    }

    /* Create a pipe to get output from children. All children
     * will write to the same pipe, which could in theory
     * cause some conflicts, but shouldn't in practice. */
    {
        int pipe_stdout[2];
        int pipe_stderr[2];
        
        err = pipe(pipe_stdout);
        if (err < 0) {
            fprintf(stderr, "[-] pipe(): %s\n", strerror(errno));
            exit(1);
        }
        err = pipe(pipe_stderr);
        if (err < 0) {
            fprintf(stderr, "[-] pipe(): %s\n", strerror(errno));
            exit(1);
        }
        
        /* Save the pipes that we'll use later */
        workers->parent_stdout = pipe_stdout[0];
        workers->parent_stderr = pipe_stderr[0];
        workers->child_stdout = pipe_stdout[1];
        workers->child_stderr = pipe_stderr[1];
        
        /* Configure the parent end of the pipes be be non-inheritable.
         * In other words, none of the children can read from these
         * pipes, nor will they exist in child process space */
        fcntl(workers->parent_stdout, F_SETFD, FD_CLOEXEC);
        fcntl(workers->parent_stderr, F_SETFD, FD_CLOEXEC);
    }
    
    return workers;
}


/**
 * Do a fork()/exec() to spawn the program
 */
int
workers_spawn(struct workers_t *workers, const char *progname, size_t argc, char **argv)
{
    struct worker_t *child;
    char **new_argv;
    
    
    child = &workers->children[workers->children_count++];
    
    /* Spawn child */
again:
    child->pid = fork();
    
    /* Test for fork errors */
    if (child->pid == -1 && errno == EAGAIN) {
        /* we've run out of max processes for this user, so wait and try again,
         * hopefull some of the processes will have exited in the meantime */
        static int is_printed = 0;
        if (is_printed++ == 0)
            fprintf(stderr, "[-] fork() hit process limit\n");
        sleep(1);
        goto again;
    } else if (child->pid == -1) {
        fprintf(stderr, "[-] fork() error: %s\n", strerror(errno));
        exit(1);
    }
    
    /* Setup child parameters */
    {
        size_t i;
        new_argv = malloc((argc + 2) * sizeof(char*));
        new_argv[0] = (char *)progname;
        for (i=0; i<argc; i++) {
            new_argv[i+1] = argv[i];
        }
        new_argv[i+1] = NULL;
    }
    child->name = new_argv[1];

    
    if (child->pid == 0) {
        int err;
        /* Set the 'write' end of the pipe 'stdout' */
        dup2(workers->child_stdout, 1);
        dup2(workers->child_stderr, 2);
        
        /* Now execute our child with new program */
        err = execve(progname, new_argv, 0);
        if (err) {
            fprintf(stderr, "[+] execve(%s) failed: %s\n", progname, strerror(errno));
            exit(1);
        }
    } else {
        /* we are the parent */
        ;
    }
    return 0;
}

/**
 * Reads input from child and parses the results
 */
int
workers_read(struct workers_t *t, unsigned milliseconds,
             void (*write_stdout)(void *buf, size_t length, void *data),
             void (*write_stderr)(void *buf, size_t length, void *data),
             void *userdata
             )
{
    fd_set fds;
    int nfds = 0;
    struct timeval tv;
    int err;
    int closed_count = 1;
    
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds * 1000) % 1000000;
    
    /* Fill in all the file descriptors */
    FD_ZERO(&fds);
    FD_SET(t->parent_stdout, &fds);
    if (nfds < t->parent_stdout)
        nfds = t->parent_stdout;
    FD_SET(t->parent_stderr, &fds);
    if (nfds < t->parent_stderr)
        nfds = t->parent_stderr;
    
    /* Do the select */
again:
    err = select(nfds + 1, &fds, 0, 0, &tv);
    if (err < 0) {
        if (errno == EINTR)
            goto again; /* A signal from an exiting child interrupted this */
        fprintf(stderr, "[-] select(): %s\n", strerror(errno));
        exit(1);
    } else if (err == 0)
        return closed_count; /* okay, timeout */
    
    /* Check all the file descriptors */
    if (FD_ISSET(t->parent_stdout, &fds)) {
        char buf[16384];
        ssize_t count;
        
        count = read(t->parent_stdout, buf, sizeof(buf));
        if (count < 0) {
            fprintf(stderr, "[-] read(): %s\n", strerror(errno));
            exit(1);
        } else {
            write_stdout(buf, count, userdata);
        }
    }
    if (FD_ISSET(t->parent_stderr, &fds)) {
        char buf[16384];
        ssize_t count;
        
        count = read(t->parent_stderr, buf, sizeof(buf));
        if (count < 0) {
            fprintf(stderr, "[-] read(): %s\n", strerror(errno));
            exit(1);
        } else {
            write_stderr(buf, count, userdata);
        }
    }
    
    /* Return the number of children that were closed, so that
     * the parent process can cleanup its tracking records */
    return closed_count;
}

/**
 * Called to cleanup any children records after their processes have
 * died. We simply move the entry at the end of the list to fill
 * the void of dead child.
 */
int
workers_reap(struct workers_t *workers)
{
    int count = 0;
    
    for (;;) {
        int pid;
        
        /* Reap children.
         * The first parameter is set to -1 to indicate that we want
         * information about ANY of our children processes.
         * The second paremeter is set to NULL to indicate that we
         * aren't interested in knowing the status/result code from
         * the process.
         * The third parameter is WNOHHANG, meaning that we want to return
         * immediately
         */
        pid = waitpid(-1, 0, WNOHANG);
        
        if (pid > 0) {
            /* If we get back a valid PID, that means the child process
             * has terminated. We want to decrement our count by one
             * then loop around looking for more child processes. */
            workers->children_count--;
            count++;
            //fprintf(stderr, "[ ] children left = %u\n", (unsigned)*children_count);
            continue;
        } else if (pid == 0) {
            /* if none of our children are currently exited, then this
             * value of zero is returned. */
            break;
        } else if (pid == -1 && errno == ECHILD) {
            /* In this condition, there are no child processes. In this
             * case, we just want to handle this the same as pid=0 */
            //fprintf(stderr, "[ ] no children left\n");
            break;
        } else if (pid < 0) {
            /* Some extraordinary error occured */
            //fprintf(stderr, "[-] waitpid() %s\n", strerror(errno));
            exit(1);
        }
    }
    return count;
}

#endif


