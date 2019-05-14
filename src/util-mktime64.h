#ifndef UTIL_MKTIME64_H
#define UTIL_MKTIME64_H

/**
 * A function for converting time into a 64-bit time_t value,
 * which counts the number of seconds since January 1 1970
 * UTC. Note, this isn't 'mktime()' using local timezone,
 * but more like 'timegm()' using UTC. This is intended for
 * use with code that must also compile on 32-bit platforms,
 * where a custom 64-bit time type is used to handle
 * the 2038.
 */
long long 
util_mktime64(long year0, unsigned mon0,
		unsigned day, unsigned hour,
		unsigned min, unsigned sec);

#endif
