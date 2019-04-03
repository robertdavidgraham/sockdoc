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
#include <sys/timeb.h>
#include <time.h>


#define N 100000


#define TSC_OVERHEAD_N 100000

static inline void _sync_tsc(void)
{
  asm volatile("cpuid" : : : "%rax", "%rbx", "%rcx", "%rdx");
}

static inline uint64_t _rdtsc(void)
{
  unsigned a, d;
  asm volatile("rdtsc" : "=a" (a), "=d" (d) : : "%rbx", "%rcx");
  return ((uint64_t) a) | (((uint64_t) d) << 32);
}

static inline uint64_t _rdtscp(void)
{
  unsigned a, d;
  asm volatile("rdtscp" : "=a" (a), "=d" (d) : : "%rbx", "%rcx");
  return ((uint64_t) a) | (((uint64_t) d) << 32);
}

static inline uint64_t bench_start(void)
{
  // unsigned  cycles_low, cycles_high;
  // uint64_t t;
  //
  // asm volatile( "CPUID\n\t" // serialize
  //               "RDTSC\n\t" // read clock
  //               "mov %%edx, %0\n\t"
  //               "mov %%eax, %1\n\t"
  //               : "=r" (cycles_high), "=r" (cycles_low)
  //               :: "%rax", "%rbx", "%rcx", "%rdx" );
  // return ((uint64_t) cycles_high << 32) | cycles_low;

  _sync_tsc();
  return _rdtsc();
}

static inline uint64_t bench_end(void)
{
  // unsigned  cycles_low, cycles_high;
  // uint64_t t;
  //
  // asm volatile( "RDTSCP\n\t" // read clock + serialize
  //               "mov %%edx, %0\n\t"
  //               "mov %%eax, %1\n\t"
  //               "CPUID\n\t" // serialze -- but outside clock region!
  //               : "=r" (cycles_high), "=r" (cycles_low)
  //               :: "%rax", "%rbx", "%rcx", "%rdx" );
  // return ((uint64_t) cycles_high << 32) | cycles_low;

  uint64_t t = _rdtscp();
  _sync_tsc();
  return t;
}

static uint64_t measure_tsc_overhead(void)
{
  uint64_t t0, t1, overhead = ~0;
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

/*
# TSC Frequency
To convert from cycles to wall-clock time we need to know TSC frequency
Frequency scaling on modern Intel chips doesn't affect the TSC.

Sadly, there doesn't seem to be a good way to do this.

# Intel V3B: 17.14
That rate may be set by the maximum core-clock to bus-clock ratio of the
processor or may be set by the maximum resolved frequency at which the
processor is booted. The maximum resolved frequency may differ from the
processor base frequency, see Section 18.15.5 for more detail. On certain
processors, the TSC frequency may not be the same as the frequency in the brand
string.

# Linux Source
http://lxr.free-electrons.com/source/arch/x86/kernel/tsc.c?v=2.6.31#L399

Linux runs a calibration phase where it uses some hardware timers and checks
how many TSC cycles occur in 50ms.
*/
#define TSC_FREQ_MHZ 3500

static inline uint64_t cycles_to_ns(uint64_t cycles)
{
  // XXX: This is not safe! We don't have a good cross-platform way to
  // determine the TSC frequency for some strange reason.
  return cycles * 1000 / TSC_FREQ_MHZ;
}

/* measure the cost to call `time`. */
void time_overhead()
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
  printf("%-32s ", ctype);
  printf("%6ld-ns res, ", t.tv_sec * 1000000000 + t.tv_nsec);

  clock_overhead(clock);
}

#define eval_clock(clk) \
  measure_clock(#clk, clk)

/* benchmark various clock sources */
int main(void)
{
  printf("=> Testing Clock...\n");
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
  eval_clock(CLOCK_MONOTONIC_RAW);
#ifdef CLOCK_MONOTONIC_RAW_APPROX
  eval_clock(CLOCK_MONOTONIC_RAW_APPROX);
#endif
#ifdef CLOCK_BOOTTIME
  eval_clock(CLOCK_BOOTTIME);
#endif
#ifdef CLOCK_UPTIME
  eval_clock(CLOCK_UPTIME);
#endif
#ifdef CLOCK_UPTIME_RAW
  eval_clock(CLOCK_UPTIME_RAW);
#endif
#ifdef CLOCK_UPTIME_RAW_APPROX
  eval_clock(CLOCK_UPTIME_RAW_APPROX);
#endif
  eval_clock(CLOCK_PROCESS_CPUTIME_ID);
  eval_clock(CLOCK_THREAD_CPUTIME_ID);

  return 0;
}

