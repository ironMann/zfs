/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

#include <sys/types.h>
#include <sys/vdev_raidz.h>
#include <linux/simd_x86.h>

/*
 * Only in 64bit for now
 */
#if defined(RAIDZ_AVX2_x86_64)

#include <sys/vdev_raidz_impl.h>

#define	__asm __asm__ __volatile__

#define	_REG_CNT(_0, _1, _2, _3, _4, _5, _6, _7, N, ...) N
#define	REG_CNT(r...) _REG_CNT(r, 8, 7, 6, 5, 4, 3, 2, 1)

#define	VR0_(REG, ...) "ymm"#REG
#define	VR1_(_1, REG, ...) "ymm"#REG
#define	VR2_(_1, _2, REG, ...) "ymm"#REG
#define	VR3_(_1, _2, _3, REG, ...) "ymm"#REG
#define	VR4_(_1, _2, _3, _4, REG, ...) "ymm"#REG
#define	VR5_(_1, _2, _3, _4, _5, REG, ...) "ymm"#REG
#define	VR6_(_1, _2, _3, _4, _5, _6, REG, ...) "ymm"#REG
#define	VR7_(_1, _2, _3, _4, _5, _6, _7, REG, ...) "ymm"#REG

#define	VR0(r...) VR0_(r)
#define	VR1(r...) VR1_(r)
#define	VR2(r...) VR2_(r, 1)
#define	VR3(r...) VR3_(r, 1, 2)
#define	VR4(r...) VR4_(r, 1)
#define	VR5(r...) VR5_(r, 1, 2)
#define	VR6(r...) VR6_(r, 1, 2, 3)
#define	VR7(r...) VR7_(r, 1, 2, 3, 4)

#define	R_01(REG1, REG2, ...) REG1, REG2
#define	R_23_(_0, _1, REG2, REG3, ...) REG2, REG3
#define	R_23(REG...) R_23_(REG, 1, 2, 3)

#define	ELEM_SIZE 32

extern const uint8_t sse_gf_mod_lt[2*256][16] __attribute__((aligned(256)));
extern const uint8_t sse_clmul_mod_lt[2*256][16] __attribute__((aligned(256)));

typedef struct elem {
	unsigned char b[ELEM_SIZE];
} elem_t __attribute__((aligned(ELEM_SIZE)));

/* Convert from GF log to perform multiplications using SIMD */
inline static int
fix_mul_exponent(int e) {
	return ((int)vdev_raidz_pow2[e]);
}

#define	PREFETCHNTA(ptr, offset, stride) \
{	\
	switch (stride) {	\
	case 4:	\
	__asm("prefetchnta %0" :: "m" (((elem_t *)ptr)[offset+3])); \
	__asm("prefetchnta %0" :: "m" (((elem_t *)ptr)[offset+2])); \
	case 2:	\
	__asm("prefetchnta %0" :: "m" (((elem_t *)ptr)[offset+1])); \
	__asm("prefetchnta %0" :: "m" (((elem_t *)ptr)[offset+0])); \
	}\
}

#define	XOR_ACC(src, r...) \
{	\
	switch (REG_CNT(r)) {	\
	case 4:	\
	__asm("vpxor %0, %%" VR3(r)", %%" VR3(r) : : "m" (((elem_t *)src)[3]));\
	__asm("vpxor %0, %%" VR2(r)", %%" VR2(r) : : "m" (((elem_t *)src)[2]));\
	case 2:	\
	__asm("vpxor %0, %%" VR1(r)", %%" VR1(r) : : "m" (((elem_t *)src)[1]));\
	case 1:	\
	__asm("vpxor %0, %%" VR0(r)", %%" VR0(r) : : "m" (((elem_t *)src)[0]));\
	}\
}

#define	XOR(r...) \
{	\
	switch (REG_CNT(r)) {	\
	case 8:\
	__asm("vpxor %" VR3(r) ", %" VR7(r)", %" VR7(r));\
	__asm("vpxor %" VR2(r) ", %" VR6(r)", %" VR6(r));\
	__asm("vpxor %" VR1(r) ", %" VR5(r)", %" VR5(r));\
	__asm("vpxor %" VR0(r) ", %" VR4(r)", %" VR4(r));\
	break;\
	case 4:\
	__asm("vpxor %" VR1(r) ", %" VR3(r)", %" VR3(r));\
	__asm("vpxor %" VR0(r) ", %" VR2(r)", %" VR2(r));\
	break;\
	case 2:\
	__asm("vpxor %" VR0(r) ", %" VR1(r)", %" VR1(r));\
	break;\
	}\
}

#define	COPY(r...) \
{	\
	switch (REG_CNT(r)) {	\
	case 8:	\
		__asm("vmovdqa %" VR0(r) ", %" VR4(r));	\
		__asm("vmovdqa %" VR1(r) ", %" VR5(r));	\
		__asm("vmovdqa %" VR2(r) ", %" VR6(r));	\
		__asm("vmovdqa %" VR3(r) ", %" VR7(r));	\
	break;	\
	case 4:	\
		__asm("vmovdqa %" VR0(r) ", %" VR2(r));	\
		__asm("vmovdqa %" VR1(r) ", %" VR3(r));	\
	break;	\
	case 2:	\
		__asm("vmovdqa %" VR0(r) ", %" VR1(r));	\
	break;	\
	}	\
}

#define	LOAD(src, r...) \
{	\
	switch (REG_CNT(r)) {	\
	case 4:	\
	__asm("vmovdqa %0, %%" VR3(r) : : "m" (((elem_t *)src)[3]));\
	__asm("vmovdqa %0, %%" VR2(r) : : "m" (((elem_t *)src)[2]));\
	case 2:	\
	__asm("vmovdqa %0, %%" VR1(r) : : "m" (((elem_t *)src)[1]));\
	case 1:	\
	__asm("vmovdqa %0, %%" VR0(r) : : "m" (((elem_t *)src)[0]));\
	}\
}

#define	ZERO(r...) \
{	\
	switch (REG_CNT(r)) {	\
		case 4:	\
			__asm("vpxor %" VR3(r)", %" VR3(r)", %" VR3(r));\
			__asm("vpxor %" VR2(r)", %" VR2(r)", %" VR2(r));\
		case 2:	\
			__asm("vpxor %" VR1(r)", %" VR1(r)", %" VR1(r));\
		case 1:	\
			__asm("vpxor %" VR0(r)", %" VR0(r)", %" VR0(r));\
	}\
}

#define	STORE(dst, r...)   \
{	\
	switch (REG_CNT(r)) {	\
	case 4:	\
		__asm("vmovdqa %%" VR3(r)", %0" : "=m" (((elem_t *)dst)[3]));\
		__asm("vmovdqa %%" VR2(r)", %0" : "=m" (((elem_t *)dst)[2]));\
	case 2:	\
		__asm("vmovdqa %%" VR1(r)", %0" : "=m" (((elem_t *)dst)[1]));\
	case 1:	\
		__asm("vmovdqa %%" VR0(r)", %0" : "=m" (((elem_t *)dst)[0]));\
	}\
}

#define	FLUSH() \
{   \
	__asm("vzeroupper");	\
}

#define	MUL2_SETUP() \
{   \
	__asm("vmovd %0, %%xmm14" :: "r"(0x1d1d1d1d1d1d1d1d));	\
	__asm("vpbroadcastq %xmm14, %ymm14");	\
	__asm("vpxor          %ymm15, %ymm15 ,%ymm15");	\
}

#define	MUL2_(r...) \
{	\
	switch	(REG_CNT(r)) {	\
	case 2:\
	__asm("vpcmpgtb %" VR0(r)", %ymm15,     %ymm12");\
	__asm("vpcmpgtb %" VR1(r)", %ymm15,     %ymm13");\
	__asm("vpaddb   %" VR0(r)", %" VR0(r)", %" VR0(r));\
	__asm("vpaddb   %" VR1(r)", %" VR1(r)", %" VR1(r));\
	__asm("vpand    %ymm14,     %ymm12,     %ymm12");\
	__asm("vpand    %ymm14,     %ymm13,     %ymm13");\
	__asm("vpxor    %ymm12,     %" VR0(r)", %" VR0(r));\
	__asm("vpxor    %ymm13,     %" VR1(r)", %" VR1(r));\
	break;\
	}\
}

#define	MUL2(r...) \
{	\
	switch (REG_CNT(r)) {	\
	case 2:	\
		MUL2_(r);\
		break;\
	case 4:	\
		MUL2_(R_01(r));\
		MUL2_(R_23(r));\
		break;\
	}\
}

#define	MUL4(r...) \
{	\
	MUL2(r); \
	MUL2(r); \
}

#define	_0f_x		"xmm15"
#define	_0f			"ymm15"
#define	_a_save		"ymm14"
#define	_b_save		"ymm13"
#define	_lt_mod		"ymm12"
#define	_lt_clmul	"ymm11"
#define	_ta			"ymm10"
#define	_tb			"ymm15"

#define	MULx2(c, r...)\
{\
	__asm("vmovd %0, %%" _0f_x :: "r"(0x0f0f0f0f0f0f0f0f));	\
	__asm("vpbroadcastq %" _0f_x ", %" _0f);	\
	\
	/* upper bits */	\
	__asm("vbroadcasti128 %0, %%" _lt_mod		\
		: : "m" (sse_gf_mod_lt[2*(c)][0]));		\
	__asm("vbroadcasti128 %0, %%" _lt_clmul		\
		: : "m" (sse_clmul_mod_lt[2*(c)][0]));	\
	\
	__asm("vpsraw $0x4, %" VR0(r) ", %"_a_save);		\
	__asm("vpsraw $0x4, %" VR1(r) ", %"_b_save);		\
	__asm("vpand %" _0f ", %" VR0(r) ", %" VR0(r));		\
	__asm("vpand %" _0f ", %" VR1(r) ", %" VR1(r));		\
	__asm("vpand %" _0f ", %" _a_save ", %" _a_save);	\
	__asm("vpand %" _0f ", %" _b_save ", %" _b_save);	\
	\
	__asm("vpshufb %" _a_save ",%" _lt_mod ",%" _ta);		\
	__asm("vpshufb %" _b_save ",%" _lt_mod ",%" _tb);		\
	__asm("vpshufb %" _a_save ",%" _lt_clmul ",%" _a_save);	\
	__asm("vpshufb %" _b_save ",%" _lt_clmul ",%" _b_save);	\
	\
	/* low bits */\
	__asm("vbroadcasti128 %0, %%" _lt_mod		\
		: : "m" (sse_gf_mod_lt[2*(c)+1][0]));	\
	__asm("vbroadcasti128 %0, %%" _lt_clmul		\
		: : "m" (sse_clmul_mod_lt[2*(c)+1][0]));\
	\
	__asm("vpxor %" _ta ",%" _a_save ",%" _a_save);	\
	__asm("vpxor %" _tb ",%" _b_save ",%" _b_save);	\
	\
	__asm("vpshufb %" VR0(r) ",%" _lt_mod ",%" _ta);		\
	__asm("vpshufb %" VR1(r) ",%" _lt_mod ",%" _tb);		\
	__asm("vpshufb %" VR0(r) ",%" _lt_clmul ",%" VR0(r));	\
	__asm("vpshufb %" VR1(r) ",%" _lt_clmul ",%" VR1(r));	\
	\
	__asm("vpxor %" _ta ",%" VR0(r) ",%" VR0(r));		\
	__asm("vpxor %" _a_save ",%" VR0(r) ",%" VR0(r));	\
	__asm("vpxor %" _tb ",%" VR1(r) ",%" VR1(r));		\
	__asm("vpxor %" _b_save ",%" VR1(r) ",%" VR1(r));	\
}

#define	MUL(c, r...)	\
{	\
	switch (REG_CNT(r)) {	\
	case 4:	\
		MULx2(c, R_23(r));	\
	case 2:	\
		MULx2(c, R_01(r));	\
	}	\
}


#include "vdev_raidz_math_x86simd.h"
#include "vdev_raidz_math_impl.h"

RAIDZ_GEN_WRAP(p, avx2);
RAIDZ_GEN_WRAP(pq, avx2);
RAIDZ_GEN_WRAP(pqr, avx2);

RAIDZ_REC_WRAP(p, avx2);
RAIDZ_REC_WRAP(q, avx2);
RAIDZ_REC_WRAP(r, avx2);
RAIDZ_REC_WRAP(pq, avx2);
RAIDZ_REC_WRAP(pr, avx2);
RAIDZ_REC_WRAP(qr, avx2);
RAIDZ_REC_WRAP(pqr, avx2);

static boolean_t
raidz_math_will_avx2_work(void)
{
	return (zfs_avx_available() && zfs_avx2_available());
}

const raidz_math_ops_t vdev_raidz_avx2_impl = {
	.gen = { &avx2_gen_p, &avx2_gen_pq, &avx2_gen_pqr },
	.rec = {
		&avx2_rec_p, &avx2_rec_q, &avx2_rec_r,
		&avx2_rec_pq, &avx2_rec_pr, &avx2_rec_qr,
		&avx2_rec_pqr
	},
	.is_supported = &raidz_math_will_avx2_work,
	.name = "avx2"
};

#endif /* defined(RAIDZ_AVX2_x86_64) */
