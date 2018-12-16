#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/wait.h>

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

unsigned long long
spawn_program(const char *prog, const char *type)
{
	unsigned long long result = 0;	
	int fd[2];
	pid_t pid;

	/* Create a pipe to get output from child */
	pipe(fd);

	/* Spawn child */
	pid = fork();

	if (pid == 0) {
		/* If we are the child, then execute a new program */
		char * new_argv[3];
		new_argv[0] = (char *)prog;
		new_argv[1] = (char *)type;
		new_argv[2] = 0;
		close(fd[0]);
		dup2(fd[1], 1);
		execve(prog, new_argv, 0);
	} else {
		/* If we are the parent, grab the results */
		char buf[1024];
		close(fd[1]);
		FILE *fp = fdopen(fd[0], "rt");
		if (fgets(buf, sizeof(buf), fp)) {
			result = strtoull(buf, 0, 0);
		}
	}
	return result;
}

void
run_tests(const char *progname, const char *testname, size_t loop_count)
{
	size_t i;
	unsigned long long *results = calloc(loop_count, sizeof(*results));;
	unsigned long long mask = 0;
	size_t outstanding = 0;
	
	for (i=0; i<loop_count; i++) {
		results[i] = spawn_program(progname, testname);
		if (results[i] == 0) {
			fprintf(stderr, "error\n");
			abort();
		} else
			outstanding++;

		/* Only allow up to so many outstanding at a time, to avoid
		 * overloading small devices with too many processes */
		while (outstanding > 16) {
			wait(0);
			outstanding--;
		}
	}
	
	/* Wait until all children have exited */
	while (outstanding) {
		wait(0);
		outstanding--;
	}

	/* Calculate results */
	for (i=1; i<loop_count; i++) {
		unsigned long long x = results[i-1] ^ results[i];
		mask |= x;
	}

	printf("%-8s 0x%016llx %02u-bits\n", testname, mask, count_bits(mask));
}

int main(int argc, char *argv[])
{
	if (argc == 2 && strcmp(argv[1], "exec") == 0) {
		printf("%p\n", main);
		return 0;
	} else if (argc == 2 && strcmp(argv[1], "libc") == 0) {
		printf("%p\n", sprintf);
		return 0;
	} else if (argc == 2 && strcmp(argv[1], "heap") == 0) {
		printf("%p\n", malloc(5));
		return 0;
	} else if (argc == 2 && strcmp(argv[1], "heap2") == 0) {
		printf("%p\n", malloc(5000000));
		return 0;
	} else if (argc == 2 && strcmp(argv[1], "stack") == 0) {
		printf("%p\n", alloca(5));
		return 0;
	} else if (argc == 2 && strcmp(argv[1], "static") == 0) {
		printf("%p\n", "static");
		return 0;
	} else if (argc == 1 || (argc == 2 && atoi(argv[1]) > 0)) {
		int count = 100;
		if (argc == 2)
			count = atoi(argv[1]);

		fprintf(stderr, "This can take a while: %d\n", count); fflush(stderr);
		if (sizeof(size_t) == 8)
			printf("%-8s 0x%016llx %02u-bits\n", "-max-", 0xeeddccbbaaULL, 40);
		else
			printf("%-8s 0x%016llx %02u-bits\n", "-max-", 0xddccbbaaULL, 32);
		printf("%-8s 0x%016llx %02u-bits\n", "-page-", 0xFFFULL, 12);
		run_tests(argv[0], "stack", count);
		run_tests(argv[0], "heap", count);
		run_tests(argv[0], "heap2", count);
		run_tests(argv[0], "exec", count);
		run_tests(argv[0], "static", count);
		run_tests(argv[0], "libc", count);
	} else {
		fprintf(stderr, "usage: just run with no parameters\n");
	}

	return 0;
}
			
