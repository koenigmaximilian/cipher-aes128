/*
 * Copyright (c) 2012-2013 Vincent Hanquez <vincent@snarc.org>
 * 
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef WITH_AESNI

#include <wmmintrin.h>
#include <tmmintrin.h>
#include <string.h>
#include "aes.h"
#include "aes_x86ni.h"
#include "block128.h"
#include "cpu.h"

#ifdef ARCH_X86
#define ALIGN_UP(addr, size) (((addr) + ((size) - 1)) & (~((size) - 1)))
#define ALIGNMENT(n) __attribute__((aligned(n)))

/* old GCC version doesn't cope with the shuffle parameters, that can take 2 values (0xff and 0xaa)
 * in our case, passed as argument despite being a immediate 8 bits constant anyway.
 * un-factorise aes_128_key_expansion into 2 version that have the shuffle parameter explicitly set */
static __m128i aes_128_key_expansion_ff(__m128i key, __m128i keygened)
{
	keygened = _mm_shuffle_epi32(keygened, 0xff);
	key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
	key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
	key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
	return _mm_xor_si128(key, keygened);
}

static __m128i aes_128_key_expansion_aa(__m128i key, __m128i keygened)
{
	keygened = _mm_shuffle_epi32(keygened, 0xaa);
	key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
	key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
	key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
	return _mm_xor_si128(key, keygened);
}

void tmd_aes_ni_init(aes_key *key, uint8_t *ikey, uint8_t size)
{
	__m128i k[28];
	uint64_t *out = (uint64_t *) key->data;
	int i;

	switch (size) {
	case 16:
		k[0] = _mm_loadu_si128((const __m128i*) ikey);

		#define AES_128_key_exp(K, RCON) aes_128_key_expansion_ff(K, _mm_aeskeygenassist_si128(K, RCON))
		k[1]  = AES_128_key_exp(k[0], 0x01);
		k[2]  = AES_128_key_exp(k[1], 0x02);
		k[3]  = AES_128_key_exp(k[2], 0x04);
		k[4]  = AES_128_key_exp(k[3], 0x08);
		k[5]  = AES_128_key_exp(k[4], 0x10);
		k[6]  = AES_128_key_exp(k[5], 0x20);
		k[7]  = AES_128_key_exp(k[6], 0x40);
		k[8]  = AES_128_key_exp(k[7], 0x80);
		k[9]  = AES_128_key_exp(k[8], 0x1B);
		k[10] = AES_128_key_exp(k[9], 0x36);

		/* generate decryption keys in reverse order.
		 * k[10] is shared by last encryption and first decryption rounds
		 * k[20] is shared by first encryption round (and is the original user key) */
		k[11] = _mm_aesimc_si128(k[9]);
		k[12] = _mm_aesimc_si128(k[8]);
		k[13] = _mm_aesimc_si128(k[7]);
		k[14] = _mm_aesimc_si128(k[6]);
		k[15] = _mm_aesimc_si128(k[5]);
		k[16] = _mm_aesimc_si128(k[4]);
		k[17] = _mm_aesimc_si128(k[3]);
		k[18] = _mm_aesimc_si128(k[2]);
		k[19] = _mm_aesimc_si128(k[1]);

		for (i = 0; i < 20; i++)
			_mm_storeu_si128(((__m128i *) out) + i, k[i]);
		break;
	case 32:
#define AES_256_key_exp_1(K1, K2, RCON) aes_128_key_expansion_ff(K1, _mm_aeskeygenassist_si128(K2, RCON))
#define AES_256_key_exp_2(K1, K2)       aes_128_key_expansion_aa(K1, _mm_aeskeygenassist_si128(K2, 0x00))
		k[0]  = _mm_loadu_si128((const __m128i*) ikey);
		k[1]  = _mm_loadu_si128((const __m128i*) (ikey+16));
		k[2]  = AES_256_key_exp_1(k[0], k[1], 0x01);
		k[3]  = AES_256_key_exp_2(k[1], k[2]);
		k[4]  = AES_256_key_exp_1(k[2], k[3], 0x02);
		k[5]  = AES_256_key_exp_2(k[3], k[4]);
		k[6]  = AES_256_key_exp_1(k[4], k[5], 0x04);
		k[7]  = AES_256_key_exp_2(k[5], k[6]);
		k[8]  = AES_256_key_exp_1(k[6], k[7], 0x08);
		k[9]  = AES_256_key_exp_2(k[7], k[8]);
		k[10] = AES_256_key_exp_1(k[8], k[9], 0x10);
		k[11] = AES_256_key_exp_2(k[9], k[10]);
		k[12] = AES_256_key_exp_1(k[10], k[11], 0x20);
		k[13] = AES_256_key_exp_2(k[11], k[12]);
		k[14] = AES_256_key_exp_1(k[12], k[13], 0x40);

		k[15] = _mm_aesimc_si128(k[13]);
		k[16] = _mm_aesimc_si128(k[12]);
		k[17] = _mm_aesimc_si128(k[11]);
		k[18] = _mm_aesimc_si128(k[10]);
		k[19] = _mm_aesimc_si128(k[9]);
		k[20] = _mm_aesimc_si128(k[8]);
		k[21] = _mm_aesimc_si128(k[7]);
		k[22] = _mm_aesimc_si128(k[6]);
		k[23] = _mm_aesimc_si128(k[5]);
		k[24] = _mm_aesimc_si128(k[4]);
		k[25] = _mm_aesimc_si128(k[3]);
		k[26] = _mm_aesimc_si128(k[2]);
		k[27] = _mm_aesimc_si128(k[1]);
		for (i = 0; i < 28; i++)
			_mm_storeu_si128(((__m128i *) out) + i, k[i]);
		break;
	default:
		break;
	}
}

/* TO OPTIMISE: use pcmulqdq... or some faster code.
 * this is the lamest way of doing it, but i'm out of time.
 * this is basically a copy of gf_mulx in gf.c */
static __m128i gfmulx(__m128i v)
{
	uint64_t v_[2] ALIGNMENT(16);
	const uint64_t gf_mask = 0x8000000000000000;

	_mm_store_si128((__m128i *) v_, v);
	uint64_t r = ((v_[1] & gf_mask) ? 0x87 : 0);
	v_[1] = (v_[1] << 1) | (v_[0] & gf_mask ? 1 : 0);
	v_[0] = (v_[0] << 1) ^ r;
	v = _mm_load_si128((__m128i *) v_);
	return v;
}

static void unopt_gf_mul(block128 *a, block128 *b)
{
	uint64_t a0, a1, v0, v1;
	int i, j;

	a0 = a1 = 0;
	v0 = cpu_to_be64(a->q[0]);
	v1 = cpu_to_be64(a->q[1]);

	for (i = 0; i < 16; i++)
		for (j = 0x80; j != 0; j >>= 1) {
			uint8_t x = b->b[i] & j;
			a0 ^= x ? v0 : 0;
			a1 ^= x ? v1 : 0;
			x = (uint8_t) v1 & 1;
			v1 = (v1 >> 1) | (v0 << 63);
			v0 = (v0 >> 1) ^ (x ? (0xe1ULL << 56) : 0);
		}
	a->q[0] = cpu_to_be64(a0);
	a->q[1] = cpu_to_be64(a1);
}

static __m128i ghash_add(__m128i tag, __m128i h, __m128i m)
{
	aes_block _t, _h;
	tag = _mm_xor_si128(tag, m);

	_mm_store_si128((__m128i *) &_t, tag);
	_mm_store_si128((__m128i *) &_h, h);
	unopt_gf_mul(&_t, &_h);
	tag = _mm_load_si128((__m128i *) &_t);
	return tag;
}

#define PRELOAD_ENC_KEYS128(k) \
	__m128i K0  = _mm_loadu_si128(((__m128i *) k)+0); \
	__m128i K1  = _mm_loadu_si128(((__m128i *) k)+1); \
	__m128i K2  = _mm_loadu_si128(((__m128i *) k)+2); \
	__m128i K3  = _mm_loadu_si128(((__m128i *) k)+3); \
	__m128i K4  = _mm_loadu_si128(((__m128i *) k)+4); \
	__m128i K5  = _mm_loadu_si128(((__m128i *) k)+5); \
	__m128i K6  = _mm_loadu_si128(((__m128i *) k)+6); \
	__m128i K7  = _mm_loadu_si128(((__m128i *) k)+7); \
	__m128i K8  = _mm_loadu_si128(((__m128i *) k)+8); \
	__m128i K9  = _mm_loadu_si128(((__m128i *) k)+9); \
	__m128i K10 = _mm_loadu_si128(((__m128i *) k)+10);

#define PRELOAD_ENC_KEYS256(k) \
	PRELOAD_ENC_KEYS128(k) \
	__m128i K11 = _mm_loadu_si128(((__m128i *) k)+11); \
	__m128i K12 = _mm_loadu_si128(((__m128i *) k)+12); \
	__m128i K13 = _mm_loadu_si128(((__m128i *) k)+13); \
	__m128i K14 = _mm_loadu_si128(((__m128i *) k)+14);

#define DO_ENC_BLOCK128(m) \
	m = _mm_xor_si128(m, K0); \
	m = _mm_aesenc_si128(m, K1); \
	m = _mm_aesenc_si128(m, K2); \
	m = _mm_aesenc_si128(m, K3); \
	m = _mm_aesenc_si128(m, K4); \
	m = _mm_aesenc_si128(m, K5); \
	m = _mm_aesenc_si128(m, K6); \
	m = _mm_aesenc_si128(m, K7); \
	m = _mm_aesenc_si128(m, K8); \
	m = _mm_aesenc_si128(m, K9); \
	m = _mm_aesenclast_si128(m, K10);

#define DO_ENC_BLOCK256(m) \
	m = _mm_xor_si128(m, K0); \
	m = _mm_aesenc_si128(m, K1); \
	m = _mm_aesenc_si128(m, K2); \
	m = _mm_aesenc_si128(m, K3); \
	m = _mm_aesenc_si128(m, K4); \
	m = _mm_aesenc_si128(m, K5); \
	m = _mm_aesenc_si128(m, K6); \
	m = _mm_aesenc_si128(m, K7); \
	m = _mm_aesenc_si128(m, K8); \
	m = _mm_aesenc_si128(m, K9); \
	m = _mm_aesenc_si128(m, K10); \
	m = _mm_aesenc_si128(m, K11); \
	m = _mm_aesenc_si128(m, K12); \
	m = _mm_aesenc_si128(m, K13); \
	m = _mm_aesenclast_si128(m, K14);

/* load K0 at K9 from index 'at' */
#define PRELOAD_DEC_KEYS_AT(k, at) \
	__m128i K0  = _mm_loadu_si128(((__m128i *) k)+at+0); \
	__m128i K1  = _mm_loadu_si128(((__m128i *) k)+at+1); \
	__m128i K2  = _mm_loadu_si128(((__m128i *) k)+at+2); \
	__m128i K3  = _mm_loadu_si128(((__m128i *) k)+at+3); \
	__m128i K4  = _mm_loadu_si128(((__m128i *) k)+at+4); \
	__m128i K5  = _mm_loadu_si128(((__m128i *) k)+at+5); \
	__m128i K6  = _mm_loadu_si128(((__m128i *) k)+at+6); \
	__m128i K7  = _mm_loadu_si128(((__m128i *) k)+at+7); \
	__m128i K8  = _mm_loadu_si128(((__m128i *) k)+at+8); \
	__m128i K9  = _mm_loadu_si128(((__m128i *) k)+at+9); \

#define PRELOAD_DEC_KEYS128(k) \
	PRELOAD_DEC_KEYS_AT(k, 10) \
	__m128i K10 = _mm_loadu_si128(((__m128i *) k)+0);

#define PRELOAD_DEC_KEYS256(k) \
	PRELOAD_DEC_KEYS_AT(k, 14) \
	__m128i K10 = _mm_loadu_si128(((__m128i *) k)+14+10); \
	__m128i K11 = _mm_loadu_si128(((__m128i *) k)+14+11); \
	__m128i K12 = _mm_loadu_si128(((__m128i *) k)+14+12); \
	__m128i K13 = _mm_loadu_si128(((__m128i *) k)+14+13); \
	__m128i K14 = _mm_loadu_si128(((__m128i *) k)+0);

#define DO_DEC_BLOCK128(m) \
	m = _mm_xor_si128(m, K0); \
	m = _mm_aesdec_si128(m, K1); \
	m = _mm_aesdec_si128(m, K2); \
	m = _mm_aesdec_si128(m, K3); \
	m = _mm_aesdec_si128(m, K4); \
	m = _mm_aesdec_si128(m, K5); \
	m = _mm_aesdec_si128(m, K6); \
	m = _mm_aesdec_si128(m, K7); \
	m = _mm_aesdec_si128(m, K8); \
	m = _mm_aesdec_si128(m, K9); \
	m = _mm_aesdeclast_si128(m, K10);

#define DO_DEC_BLOCK256(m) \
	m = _mm_xor_si128(m, K0); \
	m = _mm_aesdec_si128(m, K1); \
	m = _mm_aesdec_si128(m, K2); \
	m = _mm_aesdec_si128(m, K3); \
	m = _mm_aesdec_si128(m, K4); \
	m = _mm_aesdec_si128(m, K5); \
	m = _mm_aesdec_si128(m, K6); \
	m = _mm_aesdec_si128(m, K7); \
	m = _mm_aesdec_si128(m, K8); \
	m = _mm_aesdec_si128(m, K9); \
	m = _mm_aesdec_si128(m, K10); \
	m = _mm_aesdec_si128(m, K11); \
	m = _mm_aesdec_si128(m, K12); \
	m = _mm_aesdec_si128(m, K13); \
	m = _mm_aesdeclast_si128(m, K14);

#define SIZE 128
#define SIZED(m) m##128
#define PRELOAD_ENC PRELOAD_ENC_KEYS128
#define DO_ENC_BLOCK DO_ENC_BLOCK128
#define PRELOAD_DEC PRELOAD_DEC_KEYS128
#define DO_DEC_BLOCK DO_DEC_BLOCK128
#include "aes_x86ni_impl.c"

#undef SIZE
#undef SIZED
#undef PRELOAD_ENC
#undef PRELOAD_DEC
#undef DO_ENC_BLOCK
#undef DO_DEC_BLOCK

#define SIZED(m) m##256
#define SIZE 256
#define PRELOAD_ENC PRELOAD_ENC_KEYS256
#define DO_ENC_BLOCK DO_ENC_BLOCK256
#define PRELOAD_DEC PRELOAD_DEC_KEYS256
#define DO_DEC_BLOCK DO_DEC_BLOCK256
#include "aes_x86ni_impl.c"

#undef SIZE
#undef SIZED
#undef PRELOAD_ENC
#undef PRELOAD_DEC
#undef DO_ENC_BLOCK
#undef DO_DEC_BLOCK

#endif

#endif
