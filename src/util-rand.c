#include "util-rand.h"
#include "util-sha512.h"
#include "util-chacha20.h"
#include <string.h>

typedef struct  {
    unsigned char buf[64];
    uint32_t state[16];
    size_t partial;
} myrand_t;

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#define ROTL32(number, count) (((number) << (count)) | ((number) >> (32 - (count))))
#define READ32LE(p) \
  ((((uint32_t)(p)[0])      ) | \
   (((uint32_t)(p)[1]) <<  8) | \
   (((uint32_t)(p)[2]) << 16) | \
   (((uint32_t)(p)[3]) << 24))
#define QUARTERROUND(x, a, b, c, d) \
  x[a] += x[b]; x[d] = ROTL32(x[d] ^ x[a], 16); \
  x[c] += x[d]; x[b] = ROTL32(x[b] ^ x[c], 12); \
  x[a] += x[b]; x[d] = ROTL32(x[d] ^ x[a],  8); \
  x[c] += x[d]; x[b] = ROTL32(x[b] ^ x[c],  7)

static void 
chacha20_cryptomagic(unsigned char keystream[64], const uint32_t state[16])
{
    uint32_t x[16];
    size_t i;

    memcpy(x, state, sizeof(x[0]) * 16);
    for (i=0; i<10; i++) {
        QUARTERROUND(x, 0, 4, 8,12);
        QUARTERROUND(x, 1, 5, 9,13);
        QUARTERROUND(x, 2, 6,10,14);
        QUARTERROUND(x, 3, 7,11,15);
        QUARTERROUND(x, 0, 5,10,15);
        QUARTERROUND(x, 1, 6,11,12);
        QUARTERROUND(x, 2, 7, 8,13);
        QUARTERROUND(x, 3, 4, 9,14);
    }
    for (i=0; i<16; i++)
        x[i] += state[i];
    for (i=0; i<16; i++) {
        keystream[i * 4 + 0] = (unsigned char)(x[i] >> 0);
        keystream[i * 4 + 1] = (unsigned char)(x[i] >> 8);
        keystream[i * 4 + 2] = (unsigned char)(x[i] >>16);
        keystream[i * 4 + 3] = (unsigned char)(x[i] >>24);
    }
}

static void 
chacha20_init(myrand_t *ctx, const unsigned char key[32], const unsigned char nonce[8])
{
    ctx->state[0] = 0x61707865;
    ctx->state[1] = 0x3320646e;
    ctx->state[2] = 0x79622d32;
    ctx->state[3] = 0x6b206574;
    ctx->state[4] = READ32LE(key + 0);
    ctx->state[5] = READ32LE(key + 4);
    ctx->state[6] = READ32LE(key + 8);
    ctx->state[7] = READ32LE(key + 12);
    ctx->state[8] = READ32LE(key + 16);
    ctx->state[9] = READ32LE(key + 20);
    ctx->state[10] = READ32LE(key + 24);
    ctx->state[11] = READ32LE(key + 28);
    ctx->state[12] = 0;
    ctx->state[13] = 0;
    ctx->state[14] = READ32LE(nonce + 0);
    ctx->state[15] = READ32LE(nonce + 4);
    ctx->partial = 0;
}



void util_rand_seed(util_rand_t *vctx, const void *vseed, size_t seed_length)
{
    unsigned char digest[64];
    myrand_t *ctx = (myrand_t *)vctx;
    const unsigned char *seed = (const unsigned char *)vseed;

    util_sha512(seed, seed_length, digest, sizeof(digest));
    chacha20_init(ctx, digest, digest+32);
    chacha20_cryptomagic(ctx->buf, ctx->state);
}

void util_rand_stir(util_rand_t *vctx, const void *seed, size_t seed_length)
{
    myrand_t *ctx = (myrand_t *)vctx;
    unsigned char digest[64];
    unsigned char *key = digest + 0;
    unsigned char *nonce = digest + 32;

    /* Convert however many bytes the caller specified, which may
     * be zero, or may be billions, into a hash. This will either
     * distribute the bits across the entire 64-byte space if given
     * a small buffer (like from clock_getttime()), or reduce a large
     * buffer down to 64-bytes in case it's very large. */
    util_sha512(seed, seed_length, digest, sizeof(digest));

    /* Now we XOR the new data with the old data, which is the
     * 'nonce' and 'key' in the ChaCha20 encryption algorithm.
     * Because we are using XOR, we won't make this state any
     * less random. In other words, if the caller specifies 
     * clealry non-random data, it won't convert random seed
     * into non-random seed. */
    ctx->state[4] ^= READ32LE(key + 0);
    ctx->state[5] ^= READ32LE(key + 4);
    ctx->state[6] ^= READ32LE(key + 8);
    ctx->state[7] ^= READ32LE(key + 12);
    ctx->state[8] ^= READ32LE(key + 16);
    ctx->state[9] ^= READ32LE(key + 20);
    ctx->state[10] ^= READ32LE(key + 24);
    ctx->state[11] ^= READ32LE(key + 28);
    ctx->state[14] ^= READ32LE(nonce + 0);
    ctx->state[15] ^= READ32LE(nonce + 4);
}


void util_rand_bytes(util_rand_t *vctx, void *vbuf, size_t length)
{
    myrand_t *ctx = (myrand_t*)vctx;
    size_t i;
    unsigned char *buf = (unsigned char *)vbuf;

    for (i=0; i<length; ) {
        size_t j;
        size_t jlen;
        
        /* Get as many bytes from the pre-calculated buffer as
         * until we've exhausted those. */
        jlen = MIN(64, ctx->partial + length - i);
        for (j=ctx->partial; j<jlen; j++)
            buf[i++] = ctx->buf[j];
        ctx->partial = j;

        /* Once we reach the end of the current pre-calculated numbers,
         * grab the next buffer of numbers. */
        if (ctx->partial >= 64) {
            /* Increment the ChaCha20 block counter. In this code, however
             * once we've incremented past the 64-bit counter boundary,
             * we start incrementing the nonce, giving us a 96-bit length
             * instead of 64-bit length. */
            ctx->state[12]++;
            if (ctx->state[12] == 0) {
                ctx->state[13]++;
                if (ctx->state[13] == 0)
                    ctx->state[14]++;
            }
            chacha20_cryptomagic(ctx->buf, ctx->state);
            ctx->partial = 0;
        }
    }
}

uint64_t util_rand(util_rand_t *ctx)
{
    uint64_t result;
    util_rand_bytes(ctx, &result, sizeof(result));
    return result;
}

uint32_t util_rand32(util_rand_t *ctx)
{
    uint32_t result;
    util_rand_bytes(ctx, &result, sizeof(result));
    return result;
}

uint16_t util_rand16(util_rand_t *ctx)
{
    uint16_t result;
    util_rand_bytes(ctx, &result, sizeof(result));
    return result;
}

unsigned char util_rand8(util_rand_t *ctx)
{
    unsigned char result;
    util_rand_bytes(ctx, &result, sizeof(result));
    return result;
}

/*
 * This is a traditional way of making uniform non-binary numbers out
 * of binary numbers, by continuing to call the rand() function until
 * it delivers a value that'll give a uniform distribution.
 */
uint64_t util_rand_uniform(util_rand_t *ctx, uint64_t upper_bound)
{
    uint64_t threshold;

    if (upper_bound <= 1)
        return 0;
    
    threshold = -upper_bound % upper_bound;
    for (;;) {
        uint64_t result = util_rand(ctx);
        if (result >= threshold)
            return result % upper_bound;
    }
}

/* see `util_rand_uniform()` for more info */
uint32_t util_rand32_uniform(util_rand_t *ctx, uint32_t upper_bound)
{
    uint32_t threshold;

    if (upper_bound <= 1)
        return 0;
    
    threshold = -upper_bound % upper_bound;
    for (;;) {
        uint32_t result = util_rand32(ctx);
        if (result >= threshold)
            return result % upper_bound;
    }
}

/* see `util_rand_uniform()` for more info */
uint16_t util_rand16_uniform(util_rand_t *ctx, uint16_t upper_bound)
{
    uint32_t threshold;

    if (upper_bound <= 1)
        return 0;
    
    threshold = -upper_bound % upper_bound;
    for (;;) {
        uint32_t result = util_rand16(ctx);
        if (result >= threshold)
            return result % upper_bound;
    }
}

/* see `util_rand_uniform()` for more info */
unsigned char util_rand8_uniform(util_rand_t *ctx, unsigned char upper_bound)
{
    uint32_t threshold;

    if (upper_bound <= 1)
        return 0;
    
    threshold = -upper_bound % upper_bound;
    for (;;) {
        uint32_t result = util_rand8(ctx);
        if (result >= threshold)
            return result % upper_bound;
    }
}
