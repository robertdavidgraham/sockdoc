/*
    "A (cryptographic) random number generator"
    License: public domain
    Dependencies: util-sha512
    Optional dependencies: util-entropy util-secmem

 This is a typical random number generator, with two major differences.

 The first difference is that it uses a cryptographic random number 
 generator. This is perhaps a bit slower than the normal LCG or 
 Mersienne Twister found in typical libaries, but cryptogrpahic
 robustness is probably necessary because programmers often make
 the expectation that hackers can't predict numbers -- which isn't
 the case for other random number algorithms.

 The second difference is 'reentrancy' instead of global varialbes.
 The traditional `rand()` function stores state with a hidden global
 variable. This module makes the state visible. If the programmer
 wants to store this as a global in a multi-threaded environment,
 they'll either have to wrap the functions with protection or
 use thread-local variables.

 For security, the programmer should probably seed the function
 with a buffer of 64-bytes returned from `util_entropy_get()`. This
 is a realistic threat programmers should care about.

 For security, the context should be allocated with `util_secmem_alloc()`.
 This will prevent the state from leaking via core dumps and swap files.
 This is kinda an abstract threat, though.

 If using the traditional `rand() % n` construction to get random numbers
 between [0...n), the programmer should be away that this will not result
 in an even distribution (unless 'n' is a power of 2). The programmer should
 instead call `util_rand_uniform(ctx,n)` to get an un-biased, uniform
 distribution of numbers.

 The ony function that gets random numbers internally is `util_rand_bytes()`,
 with the other functions wrappers around this. This products blocks
 64-bytes at a time internally, so using functions to get smaller integers
 will use up those bytes at a slower pace, meaning the function will be faster.
 Therefore, the programmer should use the appropraite function to grab the 
 smallest number of bytes at a time.
 */
#ifndef UTIL_RAND_H
#define UTIL_RAND_H
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint64_t opaque[64];
} util_rand_t;

/**
 * Start generating a series of random numbers based upon the
 * given 'seed'. If unpredictable numbers are desired, then this
 * should be seeded with entropy from the hardware. If predictable
 * numbers are desired, this should be seeded with a known value.
 * @param ctx
 *      Holds the current state of the random number generator.
 *      If security is a concern, then 'util_secmem_alloc()' 
 *      should be used to allocate this structure.
 * @param seed
 *      Used to seed the random number generator. If unpredictable
 *      stream is desired, then this should be seeded with
 *      the results from 'util_entropy_get()'.
 * @param seed_length
 *      The number of bytes being used to seed the generator. There's
 *      little reason to use more than 64-bytes, as any seed will be
 *      hashed down to that amount. On the other hand, more seed won't
 *      hurt either.
 */
void util_rand_seed(util_rand_t *ctx, const void *seed, size_t seed_length);

/**
 * Stir in some additional randomness. This will preserve whatever
 * randomness that already exists and simply add this randomness. In other
 * words, while this call isn't random "util_rand_stir(ctx, "0", 1)", it
 * doing so won't make the existing state any less random. If the existing
 * state is already based on a secure amount of entropy, it won't get 
 * any less entropy. In other words, this new seed data is hashed then
 * XORred with the existing data. The typical use of this function is 
 * to occasionally stir in small bits of additional randomness, such
 * as the result form a 'gettimeofday()' function which usually has
 * a microsecond resolution, stirring in another 20 bits of randomness.
 */
void util_rand_stir(util_rand_t *ctx, const void *seed, size_t seed_length);

/**
 * Get the next 64-bit random number from the sequence. This consumes a full
 * 8-bytes from the random number generator. Other functions that get smaller
 * integers will consume fewer bytes at a time, and hence be faster. In other
 * words, `util_rand32()` is twice as fast as this function, and `util_rand8()`
 * will be eight times as fast.
 * @see util_rand_bytes()
 */
uint64_t util_rand(util_rand_t *ctx);

/**
 * Get the next 32-bit random number from the sequence.
 * @see util_rand()
 */
uint32_t util_rand32(util_rand_t *ctx);

/**
 * Get the next 16-bit random number from the sequence.
 * @see util_rand()
 */
uint16_t util_rand16(util_rand_t *ctx);

/**
 * Get the next 8-bit random number from the sequence.
 * @see util_rand()
 */
unsigned char util_rand8(util_rand_t *ctx);

/**
 * Get a next random bytes from the sequence. The various
 * util_rand() functions and util_rand_uniform() functions are
 * simple wrappers around this function.
 * @param ctx
 *      Holds the state of the random number generator.
 *      If this is a global variable in a multi-threaded program,
 *      you'll have to wrap this fucntion with thread synchornization
 *      primitives or store it as a thread-local variable.
 * @param buf
 *      Contains the output of this function, a buffer of random
 *      bytes.
 * @param length
 *      The size of the buffer in bytes that we'll fill up. Said
 *      another way, this is the number of random bytes the caller
 *      desires.
 */
void util_rand_bytes(util_rand_t *ctx, void *buf, size_t length);

/**
 * The other functions return evenly distributed binary numbers,
 * but these generate uneven distribution when the desired range
 * isn't a power of two. For example, 
 */
uint64_t util_rand_uniform(util_rand_t *ctx, uint64_t upper_bound);

/**
 * Same as "util_rand_uniform()", but gets a smaller 32-bit value.
 */
uint32_t util_rand32_uniform(util_rand_t *ctx, uint32_t upper_bound);

/**
 * Same as "util_rand_uniform()", but gets a smaller 16-bit value.
 */
uint16_t util_rand16_uniform(util_rand_t *ctx, uint16_t upper_bound);

/**
 * Same as "util_rand_uniform()", but gets a smaller 8-bit value.
 */
unsigned char util_rand8_uniform(util_rand_t *ctx, unsigned char upper_bound);


#endif
