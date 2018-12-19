/* test-aslr.c

	This program tests "ASLR" or "address space randomization", a feature in
	modern operating systems that lays out memory in a random fashion so
	that hackers cannot predict the location of anything.

	This is not a *comprehensive* program. There are more complete tools
	like `paxtest` that will do a better job. This is just tests the most
	basic ASLR features.

	This program tests for *entropy*, the number of bits that are randomized
	in addresses. Furthermore, it shows a *mask* of where the addresses are
	located in memory.

    RUNNING

    Just run the program from the command-line. This will launch
    a hundred processes and measure the entropy for each one,
    then print the results.

*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <unistd.h>


/* A global variable */
static int global;

/**
 * This is a list of the tests that we run. You can change the order of tests
 * in this list in order to change both the order in which they are calculated
 * and the order they are printed.
 */
const char *test_names[] = {
    "exec",		/* executable program, .text */
    "static",	/* static constants, often also part of .text */
	"global",	/* global variabls, .bss */
    "libc",		/* dynamically loaded shared objects/libraries */
    "heap",		/* malloc() */
    "mmap",		/* big allocations, usually by mmap() or VirtualAlloc() */
    "stack",	/* the stack */
    0
};

/**
 * An array of results, where the result at each index corresponds to the 
 * test named in the `test_names` variable.
 */
struct test_results
{
    unsigned long long values[7];
};

struct spawned
{
    pid_t pid;
    int fd[2];
};


/**
 * Count the number of bits set to '1' in a bitmask.
 */
unsigned
count_bits(unsigned long long n) 
{ 
    unsigned count = 0; 
  
    while (n) {
        count += n & 1; 
        n >>= 1ULL; 
    } 
    return count; 
}

/**
 * Do a fork()/exec() to spawn the program
 */
static struct spawned
spawn_program(const char *progname, const char *argument)
{
    struct spawned child = {0};
    
    /* Create a pipe to get output from child */
    pipe(child.fd);

    /* Spawn child */
    child.pid = fork();

    if (child.pid == 0) {
        /* We are the CHILD */
        char * new_argv[3];
        new_argv[0] = (char *)progname;
        new_argv[1] = (char *)argument;
        new_argv[2] = 0;

        /* Close the 'read' end of the pipe, since child only writes to it */
        close(child.fd[0]);

        /* Set the 'write' end of the pipe 'stdout' */
        dup2(child.fd[1], 1);

        /* Now execute our child with new program */
        execve(progname, new_argv, 0);
    } else {
        /* We are the PARENT */

        /* Close the 'write' end of the pipe, since parent only reads
         * from it */
        close(child.fd[1]);
    }
    return child;
}

/**
 * Reads input from child and parses the results
 */
static struct test_results
parse_results(struct spawned *child)
{   
    struct test_results result = {{0}};
    char buf[1024];
    FILE *fp;
    
    /* Convert 'fd' file descript to 'FILE*' pointer so I can use 'fgets()' */
    fp = fdopen(child->fd[0], "rt");

    /* Get the line of output from the child program */
    if (fgets(buf, sizeof(buf), fp)) {
        size_t j;
        char *p = buf;

        for (j = 0; j<sizeof(result.values)/sizeof(result.values[0]); j++)
            result.values[j] = strtoull(p, &p, 16);
    }
    fclose(fp);
    close(child->fd[0]);

    return result;
}

/**
 * This spawns a bunch of child programs, which will print their locations.
 * This then collects the results and prints them.
 * @param progname
 *      The name of the current program (from argv[0]), so that we simply
 *      spawn ourselves.
 * @param loop_count
 *      The number of programs to spawn, the more programs, the more
 *      accurate the results.
 */
static void
run_tests(const char *progname, size_t loop_count)
{
    size_t i;
    struct test_results *results = calloc(loop_count, sizeof(*results));;
    struct test_results mask = {{0}};
    
    /* Spawn many processes and store their results */
    for (i=0; i<loop_count; i++) {
        struct spawned child;

        /* Launch a child program */
        child = spawn_program(progname, "testall");
        if (child.pid == 0) {
            fprintf(stderr, "error\n");
            abort();
        }

        /* Get results from child */
        results[i] = parse_results(&child);
    }
    

    /* Calculate results. We are looking for a bitmask that reflects all the
     * bits that have changed from one run to the next. This is probably
     * a stupid O(n^2) algorithm that compares every result against every
     * other, I could probably do this more efficiently if I spent two
     * seconds engaging my brain and thinking about this. */
    for (i=1; i<loop_count; i++) {
        size_t j;
        for (j=0; j<sizeof(mask.values)/sizeof(mask.values[0]); j++) {
            unsigned long long x;
            x = results[i-1].values[j] ^ results[i].values[j];
            mask.values[j] |= x;
        }
    }

    /* Print a header indicative of how bit the addresses can get,
     * which is typically around 40 bits for 64-bit systems */
    if (sizeof(size_t) == 8)
        printf("%-8s   --------+++++...\n", "");
    else
        printf("%-8s   --------+++++...\n", "");

    /* Print the results. For each NAMED test, we print both the MASK
     * reflecting which bits randomly changed, and also the COUNT of
     * the number of bits. */
    for (i=0; i<sizeof(mask.values)/sizeof(mask.values[0]); i++) 
        printf("%-8s 0x%016llx %02u-bits\n", 
                test_names[i], 
                mask.values[i], 
                count_bits(mask.values[i]));
}

/**
 * Prints pointers to things that rely in different memory segments.
 * Each time we run this program, different values should be
 * printed.
 */
static void
print_layout(void)
{
    size_t i;
    for (i=0; test_names[i]; i++) {
        const char *name = test_names[i];
        if (strcmp(name, "exec") == 0) {
            printf("%p ", (void *)print_layout);
        } else if (strcmp(name, "libc") == 0) {
            printf("%p ", (void *)sprintf);
        } else if (strcmp(name, "heap") == 0) {
            printf("%p ", malloc(5));
        } else if (strcmp(name, "mmap") == 0) {
            printf("%p ", malloc(5000000));
        } else if (strcmp(name, "stack") == 0) {
            printf("%p ", (void *)name);
        } else if (strcmp(name, "static") == 0) {
            printf("%p ", (void *)"static");
        } else if (strcmp(name, "global") == 0) {
            printf("%p ", (void *)&global);
        } else
            printf("%p ", (void*)0);
    }
    printf("\n");
}

int main(int argc, char *argv[])
{
	global = 5;
    if (argc == 2 && strcmp(argv[1], "testall") == 0) {
        print_layout();
        return 0;
    } else if (argc == 1 || (argc == 2 && atoi(argv[1]) > 0)) {
        int count = 100;

        /* Parse command line options */
        if (argc == 2)
            count = atoi(argv[1]);

        /* Automatically reap zombies */
        if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
            perror("signal(SIGCHLD, SIG_IGN)");
            exit(1);
        }

        run_tests(argv[0], count);
    } else {
        fprintf(stderr, "usage: just run with no parameters\n");
    }

    return 0;
}
            
