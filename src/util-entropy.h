/*
    "Grab true randomness from the outside world, despite portability issues"

    Copyright: 2019 by Robert David Graham
    Authors: Robert David Graham
    License: MIT
        https://github.com/robertdavidgraham/sockdoc/blob/master/src/LICENSE
    Dependencies: util-sha512.c

    This module is used to gather TRUE RANDOM data from the outside
    world. It's intended to be used for simple threats, such as
    keying hashtables to prevent algorithmic overload attacks
    (such as an attacker predicting hash values to force everything
    to the same hash bucket).

    This is more a demonstration of basic entropy gathering. When
    doing cryptographic operations, such as generating keys, the
    programmer should probably instead use the functions built into
    such libraries as OpenSSL.
 */
#ifndef UTIL_ENTROPY_H
#define UTIL_ENTROPY_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h> /* size_t */

/**
 * Retrieves the requested number of bytes of random data.
 * This is intended for small amount of data, like a 64-bit
 * integer. Is more likely to fail due to insufficient
 * randomness if large amounts of data are requested.
 * This function may block for short periods of time
 * (a few milliseconds), but if it needs to block for
 * longer periods of time, will instead return an error.
 * @param buf
 *  The buffer where random bytes will be written. The
 *  caller must verify that this buffer is at least
 *  'length' bytes long.
 * @param length
 *  The number of random bytes requested. The maximum
 *  value is 64 (for 64-bytes, or 512-bits).
 * @return
 *      0 on success, or an 'errno' value on failure.
 *      EIO - if not enough entropy bytes are available to
 *          fulfill the request.
 */
unsigned util_entropy_get(void *buf, size_t length);

#ifdef __cplusplus
}
#endif
#endif