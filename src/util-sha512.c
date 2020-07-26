/*
    "Crypto implementing the SHA-512 hash algorithm"

    Copyright: 2019 by Robert David Graham
    Authors: Robert David Graham
    License: MIT
        https://github.com/robertdavidgraham/sockdoc/blob/master/src/LICENSE
    Dependencies: none
    Standard: FIPS 180-3
        http://csrc.nist.gov/publications/fips/fips180-3/fips180-3_final.pdf
    Standard: RFC 6234
    https://tools.ietf.org/html/rfc6234
 */
#include "util-sha512.h"
#include <string.h>

/* For securely wiping memory, prevents compilers from removing this
 * function due to optimizations. */
typedef void *(*memset_t)(void *, int, size_t);
static volatile memset_t secure_memset = memset;

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/**
 * Rotate right a 64-bit number by 'count' bits.
 */
#define ROTR64(number, count) \
    (((number) >> (count)) | ((number) << (64 - (count))))

/**
 * Read a buffer of 8 bytes in bit-endian format to form
 * a 64-bit integer.
 */
static uint64_t
READ64BE(const unsigned char *p)
{
    return (((uint64_t)((p)[0] & 0xFF)) << 56)
        | (((uint64_t)((p)[1] & 0xFF)) << 48)
        | (((uint64_t)((p)[2] & 0xFF)) << 40)
        | (((uint64_t)((p)[3] & 0xFF)) << 32)
        | (((uint64_t)((p)[4] & 0xFF)) << 24)
        | (((uint64_t)((p)[5] & 0xFF)) << 16)
        | (((uint64_t)((p)[6] & 0xFF)) << 8)
        | (((uint64_t)((p)[7] & 0xFF)) << 0);
}

/**
 * Convert an internal 64-bit integer into an array of 8 bytes
 * in big-endian format.
 */
static void
WRITE64BE(uint64_t x, unsigned char *p)
{
    p[0] = (unsigned char)(((x) >> 56) & 0xFF);
    p[1] = (unsigned char)(((x) >> 48) & 0xFF);
    p[2] = (unsigned char)(((x) >> 40) & 0xFF);
    p[3] = (unsigned char)(((x) >> 32) & 0xFF);
    p[4] = (unsigned char)(((x) >> 24) & 0xFF);
    p[5] = (unsigned char)(((x) >> 16) & 0xFF);
    p[6] = (unsigned char)(((x) >> 8) & 0xFF);
    p[7] = (unsigned char)(((x) >> 0) & 0xFF);
}

/* Round constants, usedin the macro "ROUND" */
static const uint64_t K[80] = { 0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
    0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
    0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL,
    0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
    0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL,
    0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL,
    0x76f988da831153b5ULL, 0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
    0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL,
    0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
    0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
    0x81c2c92e47edaee6ULL, 0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL,
    0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL,
    0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL,
    0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL,
    0x8cc702081a6439ecULL, 0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
    0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
    0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL,
    0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
    0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL,
    0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL };

#define BLOCK_SIZE 128

#define Ch(x, y, z) (z ^ (x & (y ^ z)))
#define Maj(x, y, z) (((x | y) & z) | (x & y))
#define Sigma0(x) (ROTR64(x, 28) ^ ROTR64(x, 34) ^ ROTR64(x, 39))
#define Sigma1(x) (ROTR64(x, 14) ^ ROTR64(x, 18) ^ ROTR64(x, 41))
#define Gamma0(x) (ROTR64(x, 1) ^ ROTR64(x, 8) ^ (x >> 7))
#define Gamma1(x) (ROTR64(x, 19) ^ ROTR64(x, 61) ^ (x >> 6))

#define ROUND(t0, t1, a, b, c, d, e, f, g, h, i)    \
    t0 = h + Sigma1(e) + Ch(e, f, g) + K[i] + W[i]; \
    t1 = Sigma0(a) + Maj(a, b, c);                  \
    d += t0;                                        \
    h = t0 + t1;

/*
 * This is where all the crpytography happens. It's a typical
 * crypto algorithm that shuffles around the bits and adds
 * them together in ways that can't easily be reversed.
 * This is often called a "compress" function because it logically
 * contains all the information that's added to it. Even after
 * adding terabytes of data, any change in any of the bits will
 * cause the result to be different. Of course it's not real compression,
 * as it can't be uncompressed.
 */
static void
sha512_cryptomagic(util_sha512_t *ctx, uint8_t const *buf)
{
    uint64_t S[8];
    uint64_t W[80];
    uint64_t t0;
    uint64_t t1;
    unsigned i;

    /* Make temporary copy of the state */
    memcpy(S, ctx->state, 8 * sizeof(S[0]));

    /* Copy the plaintext block into a series of 64-bit integers W[0..15],
     * doing a big-endian translation */
    for (i = 0; i < 16; i++) {
        W[i] = READ64BE(buf + (8 * i));
    }

    /* Fill in the remainder of W, W[16..79] */
    for (i = 16; i < 80; i++) {
        W[i] = Gamma1(W[i - 2]) + W[i - 7] + Gamma0(W[i - 15]) + W[i - 16];
    }

    /* Do the compress function */
    for (i = 0; i < 80; i += 8) {
        ROUND(t0, t1, S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], i + 0);
        ROUND(t0, t1, S[7], S[0], S[1], S[2], S[3], S[4], S[5], S[6], i + 1);
        ROUND(t0, t1, S[6], S[7], S[0], S[1], S[2], S[3], S[4], S[5], i + 2);
        ROUND(t0, t1, S[5], S[6], S[7], S[0], S[1], S[2], S[3], S[4], i + 3);
        ROUND(t0, t1, S[4], S[5], S[6], S[7], S[0], S[1], S[2], S[3], i + 4);
        ROUND(t0, t1, S[3], S[4], S[5], S[6], S[7], S[0], S[1], S[2], i + 5);
        ROUND(t0, t1, S[2], S[3], S[4], S[5], S[6], S[7], S[0], S[1], i + 6);
        ROUND(t0, t1, S[1], S[2], S[3], S[4], S[5], S[6], S[7], S[0], i + 7);
    }

    /* Do the feedbck step */
    for (i = 0; i < 8; i++) {
        ctx->state[i] += S[i];
    }
}

void
util_sha512_init(util_sha512_t *ctx)
{
    /* Wipe out whatever might've already existed in this structure */
    memset(ctx, 0, sizeof(*ctx));

    /* Start with a hard-coded "initialization vector" (IV). Because we
     * are always worried about the algorithm creators choosing constants
     * that are a secret backdoor, these constants were chosen to be
     * the fractional portion of the square-roots of the first primes. */
    ctx->state[0] = 0x6a09e667f3bcc908ULL; /* frac(sqrt(2)) */
    ctx->state[1] = 0xbb67ae8584caa73bULL; /* frac(sqrt(3)) */
    ctx->state[2] = 0x3c6ef372fe94f82bULL; /* frac(sqrt(5)) */
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL; /* frac(sqrt(7)) */
    ctx->state[4] = 0x510e527fade682d1ULL; /* frac(sqrt(11)) */
    ctx->state[5] = 0x9b05688c2b3e6c1fULL; /* frac(sqrt(13)) */
    ctx->state[6] = 0x1f83d9abfb41bd6bULL; /* frac(sqrt(17)) */
    ctx->state[7] = 0x5be0cd19137e2179ULL; /* frac(sqrt(19)) */
}

/*
 * This is called iteratively. If the input data isn't aligned on an
 * even block, we must buffer the partial data until either more data
 * is added that will complete the block, or until the 'final()'
 * function is called that will pad the final block.
 */
void
util_sha512_update(util_sha512_t *ctx, const void *vbuf, size_t length)
{
    const unsigned char *buf = vbuf;
    size_t offset = 0;

    /* Handle any remaining data left over from a previous call */
    if (ctx->partial) {
        size_t n = MIN(length, (BLOCK_SIZE - ctx->partial));
        memcpy(ctx->buf + ctx->partial, buf, n);
        ctx->partial += n;
        offset += n;

        /* If we've filled up a block, then process it, otherwise
         * return without doing any more processing. */
        if (ctx->partial == BLOCK_SIZE) {
            sha512_cryptomagic(ctx, ctx->buf);
            ctx->length += 8 * BLOCK_SIZE;
            ctx->partial = 0;
        }
    }

    /*
     * Process full blocks. This is where this code spends 99% of its time
     */
    while (length - offset >= BLOCK_SIZE) {
        sha512_cryptomagic(ctx, buf + offset);
        ctx->length += BLOCK_SIZE * 8;
        offset += BLOCK_SIZE;
    }

    /* If the last chunk of data isn't a complete block,
     * then buffer it, to be processed in future calls
     * to this function, or during the finalize function */
    if (length - offset) {
        size_t n = MIN(length - offset, (BLOCK_SIZE - ctx->partial));
        memcpy(ctx->buf + ctx->partial, buf + offset, n);
        ctx->partial += n;
    }
}

void
util_sha512_final(
    util_sha512_t *ctx, unsigned char *digest, size_t digest_length)
{
    size_t i;

    /* Increase the length of the message */
    ctx->length += ctx->partial * 8ULL;

    /* Append the '1' bit */
    ctx->buf[ctx->partial++] = 0x80;

    /* If the length is currently above 112 bytes we append zeros
     * then compress.  Then we can fall back to padding zeros and length
     * encoding like normal. */
    if (ctx->partial > 112) {
        while (ctx->partial < 128) {
            ctx->buf[ctx->partial++] = (uint8_t)0;
        }
        sha512_cryptomagic(ctx, ctx->buf);
        ctx->partial = 0;
    }

    /* Pad up to 120 bytes of zeroes */
    while (ctx->partial < 120) {
        ctx->buf[ctx->partial++] = (uint8_t)0;
    }

    /* Store length */
    WRITE64BE(ctx->length, ctx->buf + 120);
    sha512_cryptomagic(ctx, ctx->buf);

    /* Copy output */
    for (i = 0; i < 64 && i < digest_length; i += 8) {
        WRITE64BE(ctx->state[i / 8], digest + i);
    }

    secure_memset(ctx, 0, sizeof(*ctx));
}

void
util_sha512(
    const void *buf, size_t length, unsigned char *digest, size_t digest_length)
{
    util_sha512_t ctx;
    util_sha512_init(&ctx);
    util_sha512_update(&ctx, buf, length);
    util_sha512_final(&ctx, digest, digest_length);
}

static unsigned
TEST(const char *buf, size_t length, size_t repeat, unsigned long long x0,
    unsigned long long x1, unsigned long long x2, unsigned long long x3,
    unsigned long long x4, unsigned long long x5, unsigned long long x6,
    unsigned long long x7)
{
    unsigned count = 0;
    unsigned char digest[64];
    unsigned long long x;
    util_sha512_t ctx;
    size_t i;

    util_sha512_init(&ctx);
    for (i = 0; i < repeat; i++)
        util_sha512_update(&ctx, buf, length);
    util_sha512_final(&ctx, digest, 64);

    /* for (i=0; i<64; i += 8) {
        fprintf(stderr, "0x%016llxULL, ", READ64BE(*digest + i));
    }
    fprintf(stderr, "\n");*/
    x = READ64BE(digest + 0);
    count += (x == x0) ? 0 : 1;
    x = READ64BE(digest + 8);
    count += (x == x1) ? 0 : 1;
    x = READ64BE(digest + 16);
    count += (x == x2) ? 0 : 1;
    x = READ64BE(digest + 24);
    count += (x == x3) ? 0 : 1;
    x = READ64BE(digest + 32);
    count += (x == x4) ? 0 : 1;
    x = READ64BE(digest + 40);
    count += (x == x5) ? 0 : 1;
    x = READ64BE(digest + 48);
    count += (x == x6) ? 0 : 1;
    x = READ64BE(digest + 56);
    count += (x == x7) ? 0 : 1;

    return count;
}

int
util_sha512_selftest(void)
{
    unsigned count = 0;

    /* First, test the test. This forces a deliberate failure to
     * make sure it'll return the correct *digest. */
    count += !TEST("abc", 3, 2, 1ULL, 1ULL, 1ULL, 1ULL, 1ULL, 1ULL, 1ULL, 1ULL);

    /* Test the empty string of no input */
    count += TEST("", 0, 1, 0xcf83e1357eefb8bdULL, 0xf1542850d66d8007ULL,
        0xd620e4050b5715dcULL, 0x83f4a921d36ce9ceULL, 0x47d0d13c5d85f2b0ULL,
        0xff8318d2877eec2fULL, 0x63b931bd47417a81ULL, 0xa538327af927da3eULL);
    count += TEST("abc", 3, 1, 0xddaf35a193617abaULL, 0xcc417349ae204131ULL,
        0x12e6fa4e89a97ea2ULL, 0x0a9eeee64b55d39aULL, 0x2192992a274fc1a8ULL,
        0x36ba3c23a3feebbdULL, 0x454d4423643ce80eULL, 0x2a9ac94fa54ca49fULL);

    count += TEST("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        56, 1, 0x204a8fc6dda82f0aULL, 0x0ced7beb8e08a416ULL,
        0x57c16ef468b228a8ULL, 0x279be331a703c335ULL, 0x96fd15c13b1b07f9ULL,
        0xaa1d3bea57789ca0ULL, 0x31ad85c7a71dd703ULL, 0x54ec631238ca3445ULL);

    count += TEST("a", 1, 1000000, 0xe718483d0ce76964ULL, 0x4e2e42c7bc15b463ULL,
        0x8e1f98b13b204428ULL, 0x5632a803afa973ebULL, 0xde0ff244877ea60aULL,
        0x4cb0432ce577c31bULL, 0xeb009c5c2c49aa2eULL, 0x4eadb217ad8cc09bULL);

    /* Test deliberately misaligned data. */
    count += TEST("abcdefg", 7, 1000, 0x72d01dde5b253701ULL,
        0xc64947b6cb4015f6ULL, 0xf76a0b181f340bc9ULL, 0x02caeadcf740c3d9ULL,
        0x10a7747964fa1dafULL, 0x276603719f0db6baULL, 0xa7236d3662cda042ULL,
        0x55c06216419230c7ULL);

    /* Input size = 33 bits */
#if 0
    count += TEST("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno",
                    64,
                    16777216,
                    0xb47c933421ea2db1LL, 0x49ad6e10fce6c7f9LL, 
                    0x3d0752380180ffd7LL, 0xf4629a712134831dLL, 
                    0x77be6091b819ed35LL, 0x2c2967a2e2d4fa50LL, 
                    0x50723c9630691f1aLL, 0x05a7281dbe6c1086LL
                    );
#endif

    if (count)
        return 0; /* failure */
    else
        return 1; /* success */
}

#ifdef SHA512STANDALONE
int
main(int argc, char *argv[])
{
    int i;

    if (argc > 1 && strcmp(argv[1], "--test") == 0) {
        if (util_sha512_selftest()) {
            fprintf(stderr, "[+] sha512: success\n");
            return 0;
        } else {
            fprintf(stderr, "[+] sha512: fail\n");
            return 1;
        }
    } else if (argv[1][0] == '-' && argv[1][1] != '\0') {
        fprintf(stderr, "usage:\n sha512 <file> [<file> ...]\n");
        fprintf(stderr, "Calculates SHA512 hashes of files\n");
        fprintf(stderr, "use filename of - for stdin\n");
        return 1;
    }

    for (i=1; i<argc; i++) {
        const char *filename = argv[i];
        FILE *fp = fopen(filename, "rb");
        util_sha512_t ctx;
        size_t j;
        unsigned char digest[64];
        
        if (fp == NULL) {
            perror(filename);
            return 1;
        }

        util_sha512_init(&ctx);

        for (;;) {
            char buf[1024];
            ssize_t count;

            count = fread(buf, 1, sizeof(buf), fp);
            if (count <= 0)
                break;
            util_sha512_update(&ctx, buf, count);
        }
        fclose(fp);
        util_sha512_final(&ctx, digest, sizeof(digest));
        for (j=0; j<64; j++)
            printf("%02x", digest[j]);
        printf(" %s\n", filename);
    }
    return 0;
}
#endif
