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

#include <sys/isa_defs.h>

#if defined(__x86_64) && defined(HAVE_AVX2)

#include <sys/types.h>
#include <linux/simd_x86.h>

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
#define	_R_23(_0, _1, REG2, REG3, ...) REG2, REG3
#define	R_23(REG...) _R_23(REG, 1, 2, 3)

extern const uint8_t sse_gf_mod_lt[2*256][16];
extern const uint8_t sse_clmul_mod_lt[2*256][16];

#define	ELEM_SIZE 32

typedef struct v {
	uint8_t b[ELEM_SIZE] __attribute__((aligned(ELEM_SIZE)));
} v_t;

#define	PREFETCH(ptr, offset, stride)					\
{									\
	switch (stride) {						\
	case 4:								\
		__asm(							\
		    "prefetchnta 0x20*" #offset "+0x00(%[MEM])\n"	\
		    "prefetchnta 0x20*" #offset "+0x20(%[MEM])\n"	\
		    "prefetchnta 0x20*" #offset "+0x40(%[MEM])\n"	\
		    "prefetchnta 0x20*" #offset "+0x60(%[MEM])\n"	\
		    : : [MEM] "r" (ptr));				\
		break;							\
	default:							\
		__asm(							\
		    "prefetchnta 0x20*" #offset "+0x00(%[MEM])\n"	\
		    "prefetchnta 0x20*" #offset "+0x20(%[MEM])\n"	\
		    : : [MEM] "r" (ptr));				\
		break;							\
	}								\
}

#define	XOR_ACC(src, r...)						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "vpxor 0x00(%[SRC]), %%" VR0(r)", %%" VR0(r) "\n"	\
		    "vpxor 0x20(%[SRC]), %%" VR1(r)", %%" VR1(r) "\n"	\
		    "vpxor 0x40(%[SRC]), %%" VR2(r)", %%" VR2(r) "\n"	\
		    "vpxor 0x60(%[SRC]), %%" VR3(r)", %%" VR3(r) "\n"	\
		    : : [SRC] "r" (src));				\
		break;							\
	case 2:								\
		__asm(							\
		    "vpxor 0x00(%[SRC]), %%" VR0(r)", %%" VR0(r) "\n"	\
		    "vpxor 0x20(%[SRC]), %%" VR1(r)", %%" VR1(r) "\n"	\
		    : : [SRC] "r" (src));				\
		break;							\
	case 1:								\
		__asm(							\
		    "vpxor 0x00(%[SRC]), %%" VR0(r)", %%" VR0(r) "\n"	\
		    : : [SRC] "r" (src));				\
		break;							\
	}								\
}

#define	XOR(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		    "vpxor %" VR0(r) ", %" VR4(r)", %" VR4(r) "\n"	\
		    "vpxor %" VR1(r) ", %" VR5(r)", %" VR5(r) "\n"	\
		    "vpxor %" VR2(r) ", %" VR6(r)", %" VR6(r) "\n"	\
		    "vpxor %" VR3(r) ", %" VR7(r)", %" VR7(r));		\
		break;							\
	case 4:								\
		__asm(							\
		    "vpxor %" VR0(r) ", %" VR2(r)", %" VR2(r) "\n"	\
		    "vpxor %" VR1(r) ", %" VR3(r)", %" VR3(r));		\
		break;							\
	case 2:								\
		__asm("vpxor %" VR0(r) ", %" VR1(r)", %" VR1(r));	\
		break;							\
	}								\
}

#define	COPY(r...) 							\
{									\
	switch (REG_CNT(r)) {						\
	case 8:								\
		__asm(							\
		    "vmovdqa %" VR0(r) ", %" VR4(r) "\n"		\
		    "vmovdqa %" VR1(r) ", %" VR5(r) "\n"		\
		    "vmovdqa %" VR2(r) ", %" VR6(r) "\n"		\
		    "vmovdqa %" VR3(r) ", %" VR7(r));			\
		break;							\
	case 4:								\
		__asm(							\
		    "vmovdqa %" VR0(r) ", %" VR2(r) "\n"		\
		    "vmovdqa %" VR1(r) ", %" VR3(r));			\
		break;							\
	case 2:								\
		__asm("vmovdqa %" VR0(r) ", %" VR1(r));			\
		break;							\
	}								\
}

#define	LOAD(src, r...) 						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "vmovdqa 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    "vmovdqa 0x20(%[SRC]), %%" VR1(r) "\n"		\
		    "vmovdqa 0x40(%[SRC]), %%" VR2(r) "\n"		\
		    "vmovdqa 0x60(%[SRC]), %%" VR3(r) "\n"		\
		    : : [SRC] "r" (src));				\
		break;							\
	case 2:								\
		__asm(							\
		    "vmovdqa 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    "vmovdqa 0x20(%[SRC]), %%" VR1(r) "\n"		\
		    : : [SRC] "r" (src));				\
		break;							\
	case 1:								\
		__asm(							\
		    "vmovdqa 0x00(%[SRC]), %%" VR0(r) "\n"		\
		    : : [SRC] "r" (src));				\
		break;							\
	}								\
}

#define	STORE(dst, r...)   						\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
		__asm(							\
		    "vmovdqa %%" VR0(r) ", 0x00(%[DST])\n"		\
		    "vmovdqa %%" VR1(r) ", 0x20(%[DST])\n"		\
		    "vmovdqa %%" VR2(r) ", 0x40(%[DST])\n"		\
		    "vmovdqa %%" VR3(r) ", 0x60(%[DST])\n"		\
		    : : [DST] "r" (dst));				\
		break;							\
	case 2:								\
		__asm(							\
		    "vmovdqa %%" VR0(r) ", 0x00(%[DST])\n"		\
		    "vmovdqa %%" VR1(r) ", 0x20(%[DST])\n"		\
		    : : [DST] "r" (dst));				\
		break;							\
	case 1:								\
		__asm(							\
		    "vmovdqa %%" VR0(r) ", 0x00(%[DST])\n"		\
		    : : [DST] "r" (dst));				\
		break;							\
	}								\
}

#define	FLUSH()								\
{									\
	__asm("vzeroupper");						\
}

#define	MUL2_SETUP() 							\
{   									\
	__asm("vmovq %0,   %%xmm14" :: "r"(0x1d1d1d1d1d1d1d1d));	\
	__asm("vpbroadcastq %xmm14, %ymm14");				\
	__asm("vpxor        %ymm15, %ymm15 ,%ymm15");			\
}

#define	_MUL2(r...) 							\
{									\
	switch	(REG_CNT(r)) {						\
	case 2:								\
		__asm(							\
		    "vpcmpgtb %" VR0(r)", %ymm15,     %ymm12\n"		\
		    "vpcmpgtb %" VR1(r)", %ymm15,     %ymm13\n"		\
		    "vpaddb   %" VR0(r)", %" VR0(r)", %" VR0(r) "\n"	\
		    "vpaddb   %" VR1(r)", %" VR1(r)", %" VR1(r) "\n"	\
		    "vpand    %ymm14,     %ymm12,     %ymm12\n"		\
		    "vpand    %ymm14,     %ymm13,     %ymm13\n"		\
		    "vpxor    %ymm12,     %" VR0(r)", %" VR0(r) "\n"	\
		    "vpxor    %ymm13,     %" VR1(r)", %" VR1(r));	\
		break;							\
	}								\
}

#define	MUL2(r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
	    _MUL2(R_01(r));						\
	    _MUL2(R_23(r));						\
	    break;							\
	case 2:								\
	    _MUL2(r);							\
	    break;							\
	}								\
}

#define	MUL4(r...)							\
{									\
	MUL2(r);							\
	MUL2(r);							\
}

#define	_0f		"ymm15"
#define	_a_save		"ymm14"
#define	_b_save		"ymm13"
#define	_lt_mod		"ymm12"
#define	_lt_clmul	"ymm11"
#define	_ta		"ymm10"
#define	_tb		"ymm15"

static const uint8_t __attribute__((aligned(32))) _mul_mask = 0x0F;

#define	_MULx2(c, r...)							\
{									\
	__asm(								\
	    "vpbroadcastb (%[mask]), %%" _0f "\n"			\
	    /* upper bits */						\
	    "vbroadcasti128 0x00(%[mod]), %%" _lt_mod "\n"		\
	    "vbroadcasti128 0x00(%[clmul]), %%" _lt_clmul "\n"		\
									\
	    "vpsraw $0x4, %%" VR0(r) ", %%"_a_save "\n"			\
	    "vpsraw $0x4, %%" VR1(r) ", %%"_b_save "\n"			\
	    "vpand %%" _0f ", %%" VR0(r) ", %%" VR0(r) "\n"		\
	    "vpand %%" _0f ", %%" VR1(r) ", %%" VR1(r) "\n"		\
	    "vpand %%" _0f ", %%" _a_save ", %%" _a_save "\n"		\
	    "vpand %%" _0f ", %%" _b_save ", %%" _b_save "\n"		\
									\
	    "vpshufb %%" _a_save ", %%" _lt_mod ", %%" _ta "\n"		\
	    "vpshufb %%" _b_save ", %%" _lt_mod ", %%" _tb "\n"		\
	    "vpshufb %%" _a_save ", %%" _lt_clmul ", %%" _a_save "\n"	\
	    "vpshufb %%" _b_save ", %%" _lt_clmul ", %%" _b_save "\n"	\
	    /* low bits */						\
	    "vbroadcasti128 0x10(%[mod]), %%" _lt_mod "\n"		\
	    "vbroadcasti128 0x10(%[clmul]), %%" _lt_clmul "\n"		\
									\
	    "vpxor %%" _ta ", %%" _a_save ", %%" _a_save "\n"		\
	    "vpxor %%" _tb ", %%" _b_save ", %%" _b_save "\n"		\
									\
	    "vpshufb %%" VR0(r) ", %%" _lt_mod ", %%" _ta "\n"		\
	    "vpshufb %%" VR1(r) ", %%" _lt_mod ", %%" _tb "\n"		\
	    "vpshufb %%" VR0(r) ", %%" _lt_clmul ", %%" VR0(r) "\n"	\
	    "vpshufb %%" VR1(r) ", %%" _lt_clmul ", %%" VR1(r) "\n"	\
									\
	    "vpxor %%" _ta ", %%" VR0(r) ", %%" VR0(r) "\n"		\
	    "vpxor %%" _a_save ", %%" VR0(r) ", %%" VR0(r) "\n"		\
	    "vpxor %%" _tb ", %%" VR1(r) ", %%" VR1(r) "\n"		\
	    "vpxor %%" _b_save ", %%" VR1(r) ", %%" VR1(r) "\n"		\
	    : : [mask] "r" (&_mul_mask),				\
	    [mod] "r" (sse_gf_mod_lt[2*(c)]),				\
	    [clmul] "r" (sse_clmul_mod_lt[2*(c)]));			\
}

#define	MUL(c, r...)							\
{									\
	switch (REG_CNT(r)) {						\
	case 4:								\
	    _MULx2(c, R_01(r));						\
	    _MULx2(c, R_23(r));						\
	    break;							\
	case 2:								\
	    _MULx2(c, R_01(r));						\
	    break;							\
	}								\
}


#include <sys/vdev_raidz_impl.h>
#include "vdev_raidz_math_x86simd.h"
#include "vdev_raidz_math_impl.h"

DEFINE_GEN_METHODS(avx2);
DEFINE_REC_METHODS(avx2);

static boolean_t
raidz_math_will_avx2_work(void)
{
	return (zfs_avx_available() && zfs_avx2_available());
}

const raidz_math_ops_t vdev_raidz_avx2_impl = {
	.gen = RAIDZ_GEN_METHODS(avx2),
	.rec = RAIDZ_REC_METHODS(avx2),
	.is_supported = &raidz_math_will_avx2_work,
	.name = "avx2"
};

#endif /* defined(__x86_64) && defined(HAVE_AVX2) */
