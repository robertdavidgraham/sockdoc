/*
  Measure CPU clock cycles elapsed for benchmark measurements.
  
  Forked from:
   https://raw.githubusercontent.com/google/benchmark/master/src/cycleclock.h
  License:
    BSD License
*/

// ----------------------------------------------------------------------
// CycleClock
//    A CycleClock tells you the current time in Cycles.  The "time"
//    is actually time since power-on.  This is like time() but doesn't
//    involve a system call and is much more precise.
//
// NOTE: Not all cpu/platform/kernel combinations guarantee that this
// clock increments at a constant rate or is synchronized across all logical
// cpus in a system.
//
// If you need the above guarantees, please consider using a different
// API. There are efforts to provide an interface which provides a millisecond
// granularity and implemented as a memory read. A memory read is generally
// cheaper than the CycleClock for many architectures.
//
// Also, in some out of order CPU implementations, the CycleClock is not
// serializing. So if you're trying to count at cycles granularity, your
// data might be inaccurate due to out of order instruction execution.
// ----------------------------------------------------------------------

#ifndef UTIL_CLOCKCYCLE_H
#define UTIL_CLOCKCYCLE_H
#if defined(__cplusplus)
extern "C" {
#endif
#include <time.h>

/**
 * Return the "monotonic" time of the number of nanoseconds
 */
static inline unsigned long long
_get_monotonic(void)
{
#if defined(CLOCK_MONOTONIC)
  struct timespec ts = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
#else
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (unsigned long long)(tv.tv_sec) * 1000000000ULL + tv.tv_usec * 1000ULL;
#endif

}


//#include "benchmark/benchmark.h"
//#include "internal_macros.h"


#if defined(__APPLE__) && defined(TARGET_OS_MAC)
#include <mach/mach_time.h>
#endif

// For MSVC, we want to use '_asm rdtsc' when possible (since it works
// with even ancient MSVC compilers), and when not possible the
// __rdtsc intrinsic, declared in <intrin.h>.  Unfortunately, in some
// environments, <windows.h> and <intrin.h> have conflicting
// declarations of some other intrinsics, breaking compilation.
// Therefore, we simply declare __rdtsc ourselves. See also
// http://connect.microsoft.com/VisualStudio/feedback/details/262047
#if defined(_MSC_VER) && !defined(_M_IX86)
unsigned long long __rdtsc();
#pragma intrinsic(__rdtsc)
#endif

#if !defined(_WIN32) || defined(__MINGW32__)
#include <sys/time.h>
#include <time.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif


// NOTE: only i386 and x86_64 have been well tested.
// PPC, sparc, alpha, and ia64 are based on
//    http://peter.kuscsik.com/wordpress/?p=14
// with modifications by m3b.  See also
//    https://setisvn.ssl.berkeley.edu/svn/lib/fftw-3.0.1/kernel/cycle.h

static inline unsigned long long 
util_clockcycle(void)
{
#if defined(__APPLE__) && defined(TARGET_OS_MAC)
  // this goes at the top because we need ALL Macs, regardless of
  // architecture, to return the number of "mach time units" that
  // have passed since startup.  See sysinfo.cc where
  // InitializeSystemInfo() sets the supposed cpu clock frequency of
  // macs to the number of mach time units per second, not actual
  // CPU clock frequency (which can change in the face of CPU
  // frequency scaling).  Also note that when the Mac sleeps, this
  // counter pauses; it does not continue counting, nor does it
  // reset to zero.
  return mach_absolute_time();
#elif defined(__EMSCRIPTEN__)
  // this goes above x86-specific code because old versions of Emscripten
  // define __x86_64__, although they have nothing to do with it.
  return (unsigned long long)(emscripten_get_now() * 1e+6);
#elif defined(__i386__)
  long long ret;
  __asm__ volatile("rdtsc" : "=A"(ret));
  return ret;
#elif defined(__x86_64__) || defined(__amd64__)
  unsigned long long low, high;
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  return (high << 32) | low;
#elif defined(__powerpc__) || defined(__ppc__)
  // This returns a time-base, which is not always precisely a cycle-count.
  long long tbl, tbu0, tbu1;
  asm("mftbu %0" : "=r"(tbu0));
  asm("mftb  %0" : "=r"(tbl));
  asm("mftbu %0" : "=r"(tbu1));
  tbl &= -(long long)(tbu0 == tbu1);
  // high 32 bits in tbu1; low 32 bits in tbl  (tbu0 is garbage)
  return (tbu1 << 32) | tbl;
#elif defined(__sparc__)
  long long tick;
  asm(".byte 0x83, 0x41, 0x00, 0x00");
  asm("mov   %%g1, %0" : "=r"(tick));
  return tick;
#elif defined(__ia64__)
  long long itc;
  asm("mov %0 = ar.itc" : "=r"(itc));
  return itc;
#elif defined(_MSC_VER) && defined(_M_IX86)
  // Older MSVC compilers (like 7.x) don't seem to support the
  // __rdtsc intrinsic properly, so I prefer to use _asm instead
  // when I know it will work.  Otherwise, I'll use __rdtsc and hope
  // the code is being compiled with a non-ancient compiler.
  _asm rdtsc
#elif defined(_MSC_VER)
  return __rdtsc();
#elif defined(__native_client__) /* NACL */
  // Native Client validator on x86/x86-64 allows RDTSC instructions,
  // and this case is handled above. Native Client validator on ARM
  // rejects MRC instructions (used in the ARM-specific sequence below),
  // so we handle it here. Portable Native Client compiles to
  // architecture-agnostic bytecode, which doesn't provide any
  // cycle counter access mnemonics.

  // Native Client does not provide any API to access cycle counter.
  // Use clock_gettime(CLOCK_MONOTONIC, ...) instead of gettimeofday
  // because is provides nanosecond resolution (which is noticable at
  // least for PNaCl modules running on x86 Mac & Linux).
  // Initialize to always return 0 if clock_gettime fails.
  return _get_monotonic();
#elif defined(__aarch64__)
  // System timer of ARMv8 runs at a different frequency than the CPU's.
  // The frequency is fixed, typically in the range 1-50MHz.  It can be
  // read at CNTFRQ special register.  We assume the OS has set up
  // the virtual timer properly.
  unsigned long long virtual_timer_value;
  asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
  return virtual_timer_value;
#elif defined(__ARM_ARCH)
  // V6 is the earliest arch that has a standard cyclecount
  // Native Client validator doesn't allow MRC instructions.
#if (__ARM_ARCH >= 6)
  unsigned pmccntr;
  unsigned pmuseren;
  unsigned pmcntenset;
  // Read the user mode perf monitor counter access permissions.
  asm volatile("mrc p15, 0, %0, c9, c14, 0" : "=r"(pmuseren));
  if (pmuseren & 1) {  // Allows reading perfmon counters for user mode code.
    asm volatile("mrc p15, 0, %0, c9, c12, 1" : "=r"(pmcntenset));
    if (pmcntenset & 0x80000000ul) {  // Is it counting?
      asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(pmccntr));
      // The counter is set up to count every 64th cycle
      return pmccntr * 64ULL;  // Should optimize to << 6
    }
  }
#endif
  return _get_monotonic();
#elif defined(__mips__)
  // mips apparently only allows rdtsc for superusers, so we fall
  // back to gettimeofday.  It's possible clock_gettime would be better.
  return _get_monotonic();
#elif defined(__s390__)  // Covers both s390 and s390x.
  // Return the CPU clock.
  uint64_t tsc;
  asm("stck %0" : "=Q"(tsc) : : "cc");
  return tsc;
#else
// The soft failover to a generic implementation is automatic only for ARM.
// For other platforms the developer is expected to make an attempt to create
// a fast implementation and use generic version if nothing better is available.
#error You need to define CycleTimer for your OS and CPU
  return _get_monotonic();
#endif
} /* end util_clockcycle() function */

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif  /* UTIL_CLOCKCYCLE_H */
