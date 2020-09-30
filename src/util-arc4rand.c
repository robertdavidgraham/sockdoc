
/*	$OpenBSD: arc4random.c,v 1.55 2019/03/24 17:56:54 deraadt Exp $	*/

/*
 * Copyright (c) 1996, David Mazieres <dm@uun.org>
 * Copyright (c) 2008, Damien Miller <djm@openbsd.org>
 * Copyright (c) 2013, Markus Friedl <markus@openbsd.org>
 * Copyright (c) 2014, Theo de Raadt <deraadt@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * ChaCha based random number generator for OpenBSD.
 */

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define KEYSTREAM_ONLY

/*
chacha-merged.c version 20080118
D. J. Bernstein
Public domain.
*/

/* $OpenBSD: chacha_private.h,v 1.2 2013/10/04 07:02:27 djm Exp $ */

typedef unsigned char u8;
typedef unsigned int u32;

typedef struct
{
  u32 input[16]; /* could be compressed */
} chacha_ctx;

#define U8C(v) (v##U)
#define U32C(v) (v##U)

#define U8V(v) ((u8)(v) & U8C(0xFF))
#define U32V(v) ((u32)(v) & U32C(0xFFFFFFFF))

#define ROTL32(v, n) \
  (U32V((v) << (n)) | ((v) >> (32 - (n))))

#define U8TO32_LITTLE(p) \
  (((u32)((p)[0])      ) | \
   ((u32)((p)[1]) <<  8) | \
   ((u32)((p)[2]) << 16) | \
   ((u32)((p)[3]) << 24))

#define U32TO8_LITTLE(p, v) \
  do { \
    (p)[0] = U8V((v)      ); \
    (p)[1] = U8V((v) >>  8); \
    (p)[2] = U8V((v) >> 16); \
    (p)[3] = U8V((v) >> 24); \
  } while (0)

#define ROTATE(v,c) (ROTL32(v,c))
#define XOR(v,w) ((v) ^ (w))
#define PLUS(v,w) (U32V((v) + (w)))
#define PLUSONE(v) (PLUS((v),1))

#define QUARTERROUND(a,b,c,d) \
  a = PLUS(a,b); d = ROTATE(XOR(d,a),16); \
  c = PLUS(c,d); b = ROTATE(XOR(b,c),12); \
  a = PLUS(a,b); d = ROTATE(XOR(d,a), 8); \
  c = PLUS(c,d); b = ROTATE(XOR(b,c), 7);

static const char sigma[16] = "expand 32-byte k";
static const char tau[16] = "expand 16-byte k";

static void
chacha_keysetup(chacha_ctx *x,const u8 *k,u32 kbits,u32 ivbits)
{
  const char *constants;

  x->input[4] = U8TO32_LITTLE(k + 0);
  x->input[5] = U8TO32_LITTLE(k + 4);
  x->input[6] = U8TO32_LITTLE(k + 8);
  x->input[7] = U8TO32_LITTLE(k + 12);
  if (kbits == 256) { /* recommended */
    k += 16;
    constants = sigma;
  } else { /* kbits == 128 */
    constants = tau;
  }
  x->input[8] = U8TO32_LITTLE(k + 0);
  x->input[9] = U8TO32_LITTLE(k + 4);
  x->input[10] = U8TO32_LITTLE(k + 8);
  x->input[11] = U8TO32_LITTLE(k + 12);
  x->input[0] = U8TO32_LITTLE(constants + 0);
  x->input[1] = U8TO32_LITTLE(constants + 4);
  x->input[2] = U8TO32_LITTLE(constants + 8);
  x->input[3] = U8TO32_LITTLE(constants + 12);
}

static void
chacha_ivsetup(chacha_ctx *x,const u8 *iv)
{
  x->input[12] = 0;
  x->input[13] = 0;
  x->input[14] = U8TO32_LITTLE(iv + 0);
  x->input[15] = U8TO32_LITTLE(iv + 4);
}

static void
chacha_encrypt_bytes(chacha_ctx *x,const u8 *m,u8 *c,u32 bytes)
{
  u32 x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15;
  u32 j0, j1, j2, j3, j4, j5, j6, j7, j8, j9, j10, j11, j12, j13, j14, j15;
  u8 *ctarget = NULL;
  u8 tmp[64];
  unsigned i;

  if (!bytes) return;

  j0 = x->input[0];
  j1 = x->input[1];
  j2 = x->input[2];
  j3 = x->input[3];
  j4 = x->input[4];
  j5 = x->input[5];
  j6 = x->input[6];
  j7 = x->input[7];
  j8 = x->input[8];
  j9 = x->input[9];
  j10 = x->input[10];
  j11 = x->input[11];
  j12 = x->input[12];
  j13 = x->input[13];
  j14 = x->input[14];
  j15 = x->input[15];

  for (;;) {
    if (bytes < 64) {
      for (i = 0;i < bytes;++i) tmp[i] = m[i];
      m = tmp;
      ctarget = c;
      c = tmp;
    }
    x0 = j0;
    x1 = j1;
    x2 = j2;
    x3 = j3;
    x4 = j4;
    x5 = j5;
    x6 = j6;
    x7 = j7;
    x8 = j8;
    x9 = j9;
    x10 = j10;
    x11 = j11;
    x12 = j12;
    x13 = j13;
    x14 = j14;
    x15 = j15;
    for (i = 20;i > 0;i -= 2) {
      QUARTERROUND( x0, x4, x8,x12)
      QUARTERROUND( x1, x5, x9,x13)
      QUARTERROUND( x2, x6,x10,x14)
      QUARTERROUND( x3, x7,x11,x15)
      QUARTERROUND( x0, x5,x10,x15)
      QUARTERROUND( x1, x6,x11,x12)
      QUARTERROUND( x2, x7, x8,x13)
      QUARTERROUND( x3, x4, x9,x14)
    }
    x0 = PLUS(x0,j0);
    x1 = PLUS(x1,j1);
    x2 = PLUS(x2,j2);
    x3 = PLUS(x3,j3);
    x4 = PLUS(x4,j4);
    x5 = PLUS(x5,j5);
    x6 = PLUS(x6,j6);
    x7 = PLUS(x7,j7);
    x8 = PLUS(x8,j8);
    x9 = PLUS(x9,j9);
    x10 = PLUS(x10,j10);
    x11 = PLUS(x11,j11);
    x12 = PLUS(x12,j12);
    x13 = PLUS(x13,j13);
    x14 = PLUS(x14,j14);
    x15 = PLUS(x15,j15);

#ifndef KEYSTREAM_ONLY
    x0 = XOR(x0,U8TO32_LITTLE(m + 0));
    x1 = XOR(x1,U8TO32_LITTLE(m + 4));
    x2 = XOR(x2,U8TO32_LITTLE(m + 8));
    x3 = XOR(x3,U8TO32_LITTLE(m + 12));
    x4 = XOR(x4,U8TO32_LITTLE(m + 16));
    x5 = XOR(x5,U8TO32_LITTLE(m + 20));
    x6 = XOR(x6,U8TO32_LITTLE(m + 24));
    x7 = XOR(x7,U8TO32_LITTLE(m + 28));
    x8 = XOR(x8,U8TO32_LITTLE(m + 32));
    x9 = XOR(x9,U8TO32_LITTLE(m + 36));
    x10 = XOR(x10,U8TO32_LITTLE(m + 40));
    x11 = XOR(x11,U8TO32_LITTLE(m + 44));
    x12 = XOR(x12,U8TO32_LITTLE(m + 48));
    x13 = XOR(x13,U8TO32_LITTLE(m + 52));
    x14 = XOR(x14,U8TO32_LITTLE(m + 56));
    x15 = XOR(x15,U8TO32_LITTLE(m + 60));
#endif

    j12 = PLUSONE(j12);
    if (!j12) {
      j13 = PLUSONE(j13);
      /* stopping at 2^70 bytes per nonce is user's responsibility */
    }

    U32TO8_LITTLE(c + 0,x0);
    U32TO8_LITTLE(c + 4,x1);
    U32TO8_LITTLE(c + 8,x2);
    U32TO8_LITTLE(c + 12,x3);
    U32TO8_LITTLE(c + 16,x4);
    U32TO8_LITTLE(c + 20,x5);
    U32TO8_LITTLE(c + 24,x6);
    U32TO8_LITTLE(c + 28,x7);
    U32TO8_LITTLE(c + 32,x8);
    U32TO8_LITTLE(c + 36,x9);
    U32TO8_LITTLE(c + 40,x10);
    U32TO8_LITTLE(c + 44,x11);
    U32TO8_LITTLE(c + 48,x12);
    U32TO8_LITTLE(c + 52,x13);
    U32TO8_LITTLE(c + 56,x14);
    U32TO8_LITTLE(c + 60,x15);

    if (bytes <= 64) {
      if (bytes < 64) {
        for (i = 0;i < bytes;++i) ctarget[i] = c[i];
      }
      x->input[12] = j12;
      x->input[13] = j13;
      return;
    }
    bytes -= 64;
    c += 64;
#ifndef KEYSTREAM_ONLY
    m += 64;
#endif
  }
}

#define minimum(a, b) ((a) < (b) ? (a) : (b))

#define KEYSZ	32
#define IVSZ	8
#define BLOCKSZ	64
#define RSBUFSZ	(16*BLOCKSZ)



struct util_rand {
	chacha_ctx		rs_chacha;		/* chacha context for random keystream */
	unsigned char	rs_buf[RSBUFSZ];/* keystream blocks */
	size_t			rs_have;		/* valid bytes at end of rs_buf */
	size_t			rs_count;		/* bytes till reseed */
};



static void _rand_rekey(struct util_rand *rsx, unsigned char *dat, size_t datlen);

static unsigned long long
_hires_time(void)
{
#if defined(__APPLE__)
	return mach_absolute_time();
#else
	struct timespec ts = {0};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec << 32ULL | ts.tv_nsec;
#endif
}

static unsigned long
_cpu_entropy(void)
{

}
/**
 * Gather entropy
 */
static int
_getentropy(void *buf, size_t len)
{
	static const char *static_string = "getentropy";
	struct {
		unsigned long long hires[4];
		void *pointers[5];
		time_t now;
		int pid;
		int ppid;
	} foo;

	/* First, let's grab as many bits of entropy as we can from
	 * the underlying system. This won't grab enough, but that's
	 * okay, since we aren't going to rely upon it */
	foo.hires[0] = _hires_time();
	foo.pid = getpid();
	foo.ppid = getppid();
	foo.now = time(0);
	foo.pointers[0] = &foo; 				/* ASLR stack */
	foo.pointers[1] = (void*)_getentropy; 	/* ASLR text */
	foo.pointers[2] = static_string;		/* ASLR static strings */
	foo.pointers[3] = buf;
	foo.pointers[4] = strdup("4");			/* ASLR heap */
	foo.hires[1] = _hires_time();

	FILE *fp;
	fp = fopen("/dev/urandom", "rb");
	if (fp == NULL)
		fp = fopen("/dev/random", "rb");
	if (fp != NULL) {
		fread(buf, 1, len, fp);
		fclose(fp);
	} else
		fprintf(stderr, "[-] getentropy() can't read random file\n");
	foo.hires[2] = _hires_time();
	


	free(foo.pointers[4]);
}

static void
_rand_init(struct util_rand *rsx, unsigned char *buf, size_t n)
{
	if (n < KEYSZ + IVSZ)
		return;

	chacha_keysetup(&rsx->rs_chacha, buf, KEYSZ * 8, 0);
	chacha_ivsetup(&rsx->rs_chacha, buf + KEYSZ);
}

static void
_rand_stir(struct util_rand *rsx)
{
	unsigned char rnd[KEYSZ + IVSZ];

	if (getentropy(rnd, sizeof rnd) == -1)
		_getentropy_fail();

	_rand_rekey(rsx, rnd, sizeof(rnd));
	explicit_bzero(rnd, sizeof(rnd));	/* discard source seed */

	/* invalidate rs_buf */
	rsx->rs_have = 0;
	memset(rsx->rs_buf, 0, sizeof(rsx->rs_buf));

	rsx->rs_count = 1600000;
}

static void
_rand_stir_if_needed(struct util_rand *rsx, size_t len)
{
	if (!rsx || rsx->rs_count <= len)
		_rand_stir(rsx);
	if (rsx->rs_count <= len)
		rsx->rs_count = 0;
	else
		rsx->rs_count -= len;
}

static void
_rand_rekey(struct util_rand *rsx, unsigned char *dat, size_t datlen)
{
#ifndef KEYSTREAM_ONLY
	memset(rsx->rs_buf, 0, sizeof(rsx->rs_buf));
#endif

	/* fill rs_buf with the keystream */
	chacha_encrypt_bytes(&rsx->rs_chacha, rsx->rs_buf,
	    rsx->rs_buf, sizeof(rsx->rs_buf));
	
	/* mix in optional user provided data */
	if (dat) {
		size_t i, m;

		m = minimum(datlen, KEYSZ + IVSZ);
		for (i = 0; i < m; i++)
			rsx->rs_buf[i] ^= dat[i];
	}

	/* immediately reinit for backtracking resistance */
	_rand_init(rsx, rsx->rs_buf, KEYSZ + IVSZ);
	memset(rsx->rs_buf, 0, KEYSZ + IVSZ);
	rsx->rs_have = sizeof(rsx->rs_buf) - KEYSZ - IVSZ;
}

void
util_rand_buf(struct util_rand *rsx, void *_buf, size_t n)
{
	unsigned char *buf = (unsigned char *)_buf;
	unsigned char *keystream;
	size_t m;

	_rand_stir_if_needed(n);
	while (n > 0) {
		if (rsx->rs_have > 0) {
			m = minimum(n, rsx->rs_have);
			keystream = rsx->rs_buf + sizeof(rsx->rs_buf)
			    - rsx->rs_have;
			memcpy(buf, keystream, m);
			memset(keystream, 0, m);
			buf += m;
			n -= m;
			rsx->rs_have -= m;
		}
		if (rsx->rs_have == 0)
			_rand_rekey(rsx, NULL, 0);
	}
}

unsigned
util_rand_number(struct util_rand *rsx)
{
	unsigned val;
	unsigned char *keystream;

	_rand_stir_if_needed(sizeof(val));
	if (rsx->rs_have < sizeof(val))
		_rand_rekey(rsx, NULL, 0);
	keystream = rsx->rs_buf + sizeof(rsx->rs_buf) - rsx->rs_have;
	memcpy(&val, keystream, sizeof(val));
	memset(keystream, 0, sizeof(val));
	rsx->rs_have -= sizeof(val);
}

struct util_rand *
util_rand_create(void)
{
	/* TODO: FIXME: Protect this memory so it can't be swapped */
	struct util_rand *rsx = calloc(1, sizeof(*rsx));
	if (rsx == NULL)
		abort();

	return rsx;
}
