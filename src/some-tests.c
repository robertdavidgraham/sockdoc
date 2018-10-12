#include <stdio.h>
#include <time.h>



int main(void)
{
	/* This tells us if we are running on a 32-bit or 64-bit system */
	printf("sizeof(size_t) = %u=bits\n", 8 * (unsigned)sizeof(size_t));

	/* virtually every system, from the 16-bit era through the 64-bit, have
	 * defined this to be 32-bits. Some 64-bit platforms make this 64-bit,
	 * however */
	printf("sizeof(int) = %u-bits\n", 8 * (unsigned)sizeof(int));

	/* This is defined to be 64-bits on most 64-bit platforms, though
	 * Win64 keeps this at 32-bits */	
	printf("sizeof(long) = %u-bits\n", 8 * (unsigned)sizeof(long));

	/* This should be 64-bits everywhere */
	printf("sizeof(long long) = %u-bits\n", 8 * (unsigned)sizeof(long long));

	/* On 64-bit systems, this should be 64-bits, otherwise the
	 * code will fail in 20 years */
	printf("sizeof(time_t) = %u-bits\n", 8 * (unsigned)sizeof(time_t));

	/* An example formatting timestamps. Microsoft gives a warning suggesting
	 * gmttime_s() instead. */
	{
		char buffer[80];
        struct tm *x;
        time_t now = time(0);
        x = gmtime(&now);
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", x);
		printf("timestamp = %s\n", buffer);
	}
	
	return 0;
}

