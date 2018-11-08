/* test-aslr

	Demostrations ASLR (address space randomization). Just compile and run
	the program. Every time you run it, it should produce different
	output, as the addresses for these locations will change on
	each invocation of the program
*/
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	static const char *s = "static string";
	char *buf = malloc(10);

	printf("stack  = 0x%016llx\n", (unsigned long long)&buf);
	printf("heap   = 0x%016llx\n", (unsigned long long)buf);
	printf("text   = 0x%016llx\n", (unsigned long long)main);
	printf("static = 0x%016llx\n", (unsigned long long)s);
	printf("lib    = 0x%016llx\n", (unsigned long long)printf);

	return 0;
}

