/*
    "Grab true randomness from the outside world, despite portability issues"

    Copyright: 2019 by Robert David Graham
    Authors: Robert David Graham
    License: MIT
        https://github.com/robertdavidgraham/sockdoc/blob/master/src/LICENSE
    Dependencies: util-sha512.c
*/

/* When compiling with the flag -std=c99, then this won't be defined. However,
 * we still need it to be defined for things like syscall(). Therefore, force
 * the definition before we include any files. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "util-entropy.h"
#include "util-sha512.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>

/*
 * LINUX
 */
#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

/*
 * Windows
 */
#if defined(WIN32) || defined(_WIN32)
/* Use only those APIs that will work on WinNT/4.0 */
#define _WIN32_WINNT 0x0400
#include <wincrypt.h>
#include <windows.h>
#endif

/*
 * Apple macOS and iOS
 */
#if defined(__APPLE__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <mach/mach_time.h>
#endif

typedef util_sha512_t pool_t;
#define pool_init(pool) util_sha512_init(pool)
#define pool_add(pool, buf, len) util_sha512_update(pool, buf, len)
#define pool_final(pool, result) util_sha512_final(pool, result)

/**
 * Get some entropy based upon high-resolution timestamps, which
 * are often nanosecond in resolution. We call this repeatedly
 * as we do other things, because syscalls and other activities
 * can take a variable amount of time.
 */
static unsigned
scavange_time_hires(pool_t *pool)
{
    struct timeval tv;
    int clock_sources[] = { CLOCK_REALTIME,
#ifdef CLOCK_MONOTONIC
        CLOCK_MONOTONIC,
#endif
#ifdef CLOCK_MONOTONIC_RAW
        CLOCK_MONOTONIC_RAW,
#endif
#ifdef CLOCK_UPTIME_RAW /* macOS */
        CLOCK_UPTIME_RAW,
#endif
#ifdef CLOCK_PROCESS_CPUTIME_ID /* macOS */
        CLOCK_PROCESS_CPUTIME_ID,
#endif
#ifdef CLOCK_BOOTTIME /* Linux */
        CLOCK_BOOTTIME,
#endif
        -1 };
    size_t i;
    unsigned entropy_count = 0;
    unsigned min_res = 1000000000;

    /* Get the traditional high-resolution timestamp, which should work
     * on almost any non-Windows operating system. */
    gettimeofday(&tv, 0);
    pool_add(pool, &tv, sizeof(tv));

    /* On older macOS, the clock_gettime() functions don't work, so we
     * use machgettime() instead */
#if defined(__APPLE__)
{
    uint64_t now = mach_absolute_time();
    if (now != 0 && now != ~0ULL) {
        mach_timebase_info_data_t tb;
        kern_return_t err = mach_timebase_info(&tb);
        if (err == KERN_SUCCESS) {
            uint64_t tv_nsec = tb.numer / tb.denom;
        }
    }
}
#endif

    /* Use `clock_gettime()` for the various time sources that this
     * system may support. */
    for (i = 0; clock_sources[i] != -1; i++) {
        int err;
        struct timespec ts;
        err = clock_gettime(clock_sources[i], &ts);
        if (err == 0) {
            struct timespec res;

            /* Calculate the smallest resolution of any of
             * the clocks, which we'll treat as the amount
             * of entropy for this call. */
            err = clock_getres(clock_sources[i], &res);
            if (min_res > res.tv_nsec)
                min_res = res.tv_nsec;

            /* Add the bytes to our pool */
            pool_add(pool, &ts, sizeof(ts));
        }
    }

    /* Calculate the number of bits of entropy given whichever
     * clock has the best resolution */
    entropy_count = 1;
    while (min_res < 100000) {
        entropy_count++;
        min_res *= 2;
    }
    return entropy_count;
}

static unsigned
scavange_basics(pool_t *pool)
{
    /* Use the current time. At best, it's only around 16-bits worth of
     * entropy. However, in practice is may be zero bits, as many
     * programs/protocols will allow somebody to deterministically
     * discover this value. For example, when gathering entropy
     * only on startup, then having a feature in the protocol that
     * reports "uptime", an adersary can precisely determine this
     * value */
    {
        time_t t = time(0);
        pool_add(pool, &t, sizeof(t));
    }

    /* Get the process ID. In theory, this can provided around 10-bits
     * worth of entropy. In practice, how a system launches programs
     * on startup may result in this being 100% predictable, providing
     * zero bits of entropy.
     */
    {
        int pid = getpid();
        pool_add(pool, &pid, sizeof(pid));
        pid = getppid();
        pool_add(pool, &pid, sizeof(pid));
    }

    /* Return the numbre of bits of entropy we think we were able to
     * retrieve. I'm not sure the best value to return. In some cases,
     * this number may be zero, because the program will either leak
     * this info or make it predictable. In other cases, it's a good
     * 20 bits worth of entropy. */
    return 0;
}

/**
 * Grab random data from /dev/urandom (preferably) or /dev/random
 */
static unsigned
scavange_dev_random(pool_t *pool)
{
    int fd;
    char buf[64] = { 0 };
    size_t i;

    /* Try to open /dev/urandom. This is the best source of data
     * if it exists. Handle the extraordinarily rare case of an
     * interrupt preventing the open */
    do {
        fd = open("/dev/urandom", O_RDONLY, 0);
    } while (fd == -1 && errno == EINTR);

    /* If we can't open /dev/urandom, then fallback to /dev/random */
    if (fd == -1)
        do {
            fd = open("/dev/random", O_RDONLY, 0);
        } while (fd == -1 && errno == EINTR);

    /* If these files aren't accessible, then there isn't anything
     * we can do. Hopefully we'll have gotten data via the
     * system calls instead */
    if (fd == -1)
        return 0; /* zero bits of entropy found */

    /* Read this a byte at a time. This is because in theory,
     * a single read of 64 bytes may result only in a smaller
     * chunk of data. This makes testing when this rarely
     * occurs difficult, so instead just force the case of
     * a byte-at-a-time */
    for (i = 0; i < 64; i++) {
        int x;
        x = read(fd, buf + i, 1);
        if (x != 1)
            break;
    }

    /* Now add to our pool */
    pool_add(pool, buf, sizeof(buf));

    /* Return the number of bits of entropy we were able to scavange */
    return sizeof(buf) * 8;
}

/**
 * Retrieves random information from an operating system systemcall.
 */
static unsigned
scavange_syscall(pool_t *pool)
{
    char buf[64] = { 0 };
    int err;

#if defined(__linux__) && defined(SYS_getrandom)
    /* Call 'getrandom()'. Some older libraries may not have the function
     * even though it's supported by the operating system, so we call
     * the bare syscall ourselves instead of using the library. With
     * the flags set to zero, this should pull data from /dev/urandom,
     * and should block on system startup until enough random bits
     * exist. */
    err = syscall(SYS_getrandom, buf, sizeof(buf), 0);
    pool_add(pool, buf, sizeof(buf));
    return (err == 0) ? (sizeof(buf) * 8) : 0;
#endif

#if defined(SYS_getrandom)
#error sysrandom
    err = syscall(SYS_getentropy, buf, sizeof(buf));
    pool_add(pool, buf, sizeof(buf));
    return (err == 0) ? (sizeof(buf) * 8) : 0;
#endif

/* (defined(__FreeBSD__) || defined(__NetBSD__)) */
#if defined(CTL_KERN) && defined(KERN_ARND)
#error ctl_kern
    int mib[2] = { CTL_KERN, KERN_ARND };
    size_t total = 0;

    do {
        size_t length = sizeof(buf) - total;
        int x;

        x = sysctl(mib, 2, buf + total, &length, NULL, 0);
        if (x == -1)
            return 0;
        total += length;
    } while (total < sizeof(buf));
    pool_add(pool, buf, sizeof(buf));
    return sizeof(buf) * 8;
#endif

    return 0;
}

unsigned
util_entropy_get(void *buf, size_t length)
{
    pool_t pool[1];
    unsigned entropy_count = 0;

    /* Initialize a context for storing the intermediate
     * hash results. We'll keep updating this with the various
     * sources of entropy that we find */
    util_sha512_init(pool);

    /* Get entropy based upon the current timestamp. This queries
     * various high-resolution counters in the system which are
     * often microsecond or even nanosecond in resolution. */
    entropy_count += scavange_time_hires(pool);

    /* Scavange basic information from the system, such as
     * time(0) and getpid() */
    entropy_count += scavange_basics(pool);

    /* Get via system calls, if they are available */
    entropy_count += scavange_syscall(pool);

    /* Get via /dev/(u)random files, if they are available */
    entropy_count += scavange_dev_random(pool);


    util_sha512_final(pool, buf, length);

    /* Return an estimate of the number of bits we were able
     * to get. */
    return entropy_count;
}

#ifdef ENTROPYSTANDALONE
int main(void)
{
    unsigned count;
    unsigned char buf[64];


    count = util_entropy_get(buf, sizeof(buf));

    printf("[+] entropy = %u-bits\n", count);

    return 0;
}
#endif
