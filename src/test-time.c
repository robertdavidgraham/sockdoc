#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int
my_getres(int id, const char *id_name)
{
    struct timespec ts = {0};
    int err;

    /* Get the resolution of the clocks */
    err = clock_getres(id, &ts);
    if (err) {
        fprintf(stderr, "[-] clock_getres(%s): %s\n", id_name, strerror(errno));
        return -1;
    } else {
        printf("[+] %s resolution = %ld-ns\n", id_name, ts.tv_nsec);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    printf("[+] sizeof(time_t) = %u\n", (unsigned)sizeof(time_t));
    
    /* Find out when MONOTONIC starts */
    {
        struct timespec ts = {0};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        printf("monotonic seconds = %lld\n", (long long)ts.tv_sec);
    }
    
    if (sizeof(time_t) != 8) {
        printf("[-] WARNING: time_t not 64-bits\n");
    }
    my_getres(CLOCK_REALTIME, "CLOCK_REALTIME");
    my_getres(CLOCK_MONOTONIC, "CLOCK_MONOTONIC");
#ifdef CLOCK_MONOTONIC_RAW
    my_getres(CLOCK_MONOTONIC_RAW, "CLOCK_MONOTONIC_RAW");
#endif
    
    printf("[+] clocks-per-second = %u\n", (unsigned)CLOCKS_PER_SEC);

    return 0;
}