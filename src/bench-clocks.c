/*
  Forked from:
    https://github.com/dterei/Scraps
  Author:
    David Terei
  License:
    BSD license
*/

// Evaluate cost of calling different clocks under Linux. On one machine at a
// point in time, we got these values:
//
// time                     :   3 cycles
// ftime                    :  54 cycles
// gettimeofday             :  42 cycles
//
// CLOCK_MONOTONIC_COARSE   :   9 cycles
// CLOCK_REALTIME_COARSE    :   9 cycles
// CLOCK_MONOTONIC          :  42 cycles
// CLOCK_REALTIME           :  42 cycles
// CLOCK_MONOTONIC_RAW      : 173 cycles
// CLOCK_BOOTTIME           : 179 cycles
// CLOCK_THREAD_CPUTIME_ID  : 349 cycles
// CLOCK_PROCESS_CPUTIME_ID : 370 cycles
//
// Numbers above generated on Intel Core i7-4771 CPU @ 3.50GHz on Linux 4.0.
//

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#ifndef __FreeBSD__
#include <sys/timeb.h>
#endif

#include "util-clockcycle.h"

#define N 100000


#define TSC_OVERHEAD_N 100000


static inline uint64_t bench_start(void)
{
  return util_clockcycle();
}

static inline uint64_t bench_end(void)
{
  return util_clockcycle();
}

static uint64_t measure_tsc_overhead(void)
{
  uint64_t t0, t1, overhead = ~0ULL;
  int i;

  for (i = 0; i < TSC_OVERHEAD_N; i++) {
    t0 = bench_start();
    asm volatile("");
    t1 = bench_end();
    if (t1 - t0 < overhead)
      overhead = t1 - t0;
  }

  return overhead;
}



/* measure the cost to call `time`. */
static void 
time_overhead(void)
{
  int i;
  uint64_t t0, t1, tsc_overhead;
  uint64_t min, max, avg;
  uint64_t times[N];

  tsc_overhead = measure_tsc_overhead();

  // we run N times and take the min
  for (i = 0; i < N; i++) {
    t0 = bench_start();
    time(NULL);
    t1 = bench_end();
    times[i] = t1 - t0 - tsc_overhead;
  }
  
  min = ~0, max = 0, avg = 0;
  for (i = 0; i < N; i++) {
    avg += times[i];
    if (times[i] < min) { min = times[i]; }
    if (times[i] > max) { max = times[i]; }
  }
  avg /= N;
  
  printf("%-32s ", "time()");
  printf("       1s res, ");
  printf("%6" PRIu64 "-cycles min, ", min);
  printf("%6" PRIu64 "-cycles avg\n", avg);
  //printf("Cost (max): %" PRIu64 " cycles\n", max);
}

/* measure the cost to call `ftime`. */
void ftime_overhead()
{
  int i;
  struct timeb t;
  uint64_t t0, t1, tsc_overhead;
  uint64_t min, max, avg;
  uint64_t times[N];

  tsc_overhead = measure_tsc_overhead();

  // we run N times and take the min
  for (i = 0; i < N; i++) {
    t0 = bench_start();
    ftime(&t);
    t1 = bench_end();
    times[i] = t1 - t0 - tsc_overhead;
  }
  
  min = ~0, max = 0, avg = 0;
  for (i = 0; i < N; i++) {
    avg += times[i];
    if (times[i] < min) { min = times[i]; }
    if (times[i] > max) { max = times[i]; }
  }
  avg /= N;
  
  printf("%-32s ", "ftime()");
  printf("      1ms res, ");
  printf("%6" PRIu64 "-cycles min, ", min);
  printf("%6" PRIu64 "-cycles avg\n", avg);
  //printf("Cost (max): %" PRIu64 " cycles\n", max);
}

/* measure the cost to call `gettimeofday`. */
void gettimeofday_overhead()
{
  int i;
  struct timeval t;
  uint64_t t0, t1, tsc_overhead;
  uint64_t min, max, avg;
  uint64_t times[N];

  tsc_overhead = measure_tsc_overhead();

  // we run N times and take the min
  for (i = 0; i < N; i++) {
    t0 = bench_start();
    gettimeofday(&t, NULL);
    t1 = bench_end();
    times[i] = t1 - t0 - tsc_overhead;
  }
  
  min = ~0, max = 0, avg = 0;
  for (i = 0; i < N; i++) {
    avg += times[i];
    if (times[i] < min) { min = times[i]; }
    if (times[i] > max) { max = times[i]; }
  }
  avg /= N;
  
  printf("%-32s ", "gettimeofday()");
  printf("        ? res, ");

  printf("%6" PRIu64 "-cycles min, ", min);
  printf("%6" PRIu64 "-cycles avg\n", avg);
  //printf("Cost (max): %" PRIu64 " cycles\n", max);
}

/* measure the cost to call `clock_gettime` for the specified clock. */
void clock_overhead(clockid_t clock)
{
  int i;
  struct timespec t;
  uint64_t t0, t1, tsc_overhead;
  uint64_t min, max, avg;
  uint64_t times[N];

  tsc_overhead = measure_tsc_overhead();

  // we run N times and take the min
  for (i = 0; i < N; i++) {
    t0 = bench_start();
    clock_gettime(clock, &t);
    t1 = bench_end();
    times[i] = t1 - t0 - tsc_overhead;
  }

  
  min = ~0, max = 0, avg = 0;
  for (i = 0; i < N; i++) {
    avg += times[i];
    if (times[i] < min) { min = times[i]; }
    if (times[i] > max) { max = times[i]; }
  }
  avg /= N;
  
  printf("%6" PRIu64 "-cycles min, ", min);
  printf("%6" PRIu64 "-cycles avg\n", avg);
  //printf("Cost (max): %" PRIu64 " cycles\n", max);
}

/* measure + print the cost to call `clock_gettime` for the specified clock. */
void measure_clock(const char * ctype, clockid_t clock)
{
  struct timespec t;


  clock_getres(clock, &t);
  printf("%-30s", ctype);
  printf("%9ld-ns res, ", t.tv_sec * 1000000000 + t.tv_nsec);

  clock_overhead(clock);
}

#define eval_clock(clk) \
  measure_clock(#clk, clk)

/* benchmark various clock sources */
int main(void)
{
  printf("--- Testing clocks ---\n");

  /* Print the "monotonic" and "realtime" as integers */
  struct timespec ts = {0};
  int err;
  err = clock_gettime(CLOCK_REALTIME, &ts);
  if (err < 0) {
    fprintf(stderr, "[-] clock_gettime(CLOCK_REALTIME): %s\n", strerror(errno));
    return -1;
  }
  printf("time_t offset    = %10lld seconds\n", (long long)ts.tv_sec);
  err = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (err < 0) {
    fprintf(stderr, "[-] clock_gettime(CLOCK_MONOTONIC): %s\n", strerror(errno));
    return -1;
  }
  printf("monotonic offset = %10lld seconds\n", (long long)ts.tv_sec);

  time_overhead();
  ftime_overhead();
  gettimeofday_overhead();
  eval_clock(CLOCK_REALTIME);

#ifdef CLOCK_REALTIME_COARSE
  eval_clock(CLOCK_REALTIME_COARSE);
#endif

  eval_clock(CLOCK_MONOTONIC);

#ifdef CLOCK_MONOTONIC_COARSE
  eval_clock(CLOCK_MONOTONIC_COARSE);
#endif

#ifdef CLOCK_MONOTONIC_RAW
  eval_clock(CLOCK_MONOTONIC_RAW);
#endif

#ifdef CLOCK_MONOTONIC_RAW_APPROX
  eval_clock(CLOCK_MONOTONIC_RAW_APPROX);
#endif

#ifdef CLOCK_BOOTTIME
  eval_clock(CLOCK_BOOTTIME);
#endif

#ifdef CLOCK_UPTIME
  eval_clock(CLOCK_UPTIME);
#endif

#ifdef CLOCK_UPTIME_FAST
  eval_clock(CLOCK_UPTIME_FAST);
#endif

#ifdef CLOCK_UPTIME_PRECISE
  eval_clock(CLOCK_UPTIME_PRECISE);
#endif

#ifdef CLOCK_UPTIME_RAW
  eval_clock(CLOCK_UPTIME_RAW);
#endif

#ifdef CLOCK_UPTIME_RAW_APPROX
  eval_clock(CLOCK_UPTIME_RAW_APPROX);
#endif

#ifdef CLOCK_VIRTUAL /* Solaris 11, BSD*/
  eval_clock(CLOCK_VIRTUAL);
#endif

  eval_clock(CLOCK_PROCESS_CPUTIME_ID);
  eval_clock(CLOCK_THREAD_CPUTIME_ID);

  return 0;
}

