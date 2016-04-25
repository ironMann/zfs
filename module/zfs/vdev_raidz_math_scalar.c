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

#include <sys/vdev_raidz_impl.h>

/*
 * Provide native CPU scalar routines.
 * Support 32bit and 64bit CPUs.
 */
#if ((~(0x0ULL)) >> 24) == 0xffULL
#define	ELEM_SIZE	4
typedef uint32_t iv_t;
#elif ((~(0x0ULL)) >> 56) == 0xffULL
#define	ELEM_SIZE	8
typedef uint64_t iv_t;
#endif

/*
 * Vector type used in scalar implementation
 *
 * The union is expected to be of native CPU register size. Since addition
 * uses XOR operation, it can be performed an all byte elements at once.
 * Multiplication requires per byte access.
 */
typedef union {
	iv_t e;
	uint8_t b[ELEM_SIZE];
} v_t;

/*
 * Precomputed lookup tables for multiplication by a constant
 *
 * Reconstruction path requires multiplication by a constant factors. Instead of
 * performing two step lookup (log & exp tables), a direct lookup can be used
 * instead. Multiplication of element 'a' by a constant 'c' is obtained as:
 *
 * 	r = vdev_raidz_mul_lt[c_log][a];
 *
 * where c_log = vdev_raidz_log2[c]. Log of coefficient factors is used because
 * they are faster to obtain while solving the syndrome equations.
 *
 * PERFORMANCE NOTE:
 * Even though the complete lookup table uses 64kiB, only relatively small
 * portion of it is used at the same time. Following shows number of accessed
 * bytes for different cases:
 * 	- 1 failed disk: 256B (1 mul. coefficient)
 * 	- 2 failed disks: 512B (2 mul. coefficients)
 * 	- 3 failed disks: 1536B (6 mul. coefficients)
 *
 * Size of actually accessed lookup table regions is only larger for
 * reconstruction of 3 failed disks, when compared to traditional log/exp
 * method. But since the result is obtained in one lookup step performance is
 * doubled.
 */
static uint8_t vdev_raidz_mul_lt[256][256] __attribute__((aligned(256)));

static void
raidz_init_scalar(void)
{
	int c, i;
	for (c = 0; c < 256; c++)
		for (i = 0; i < 256; i++)
			vdev_raidz_mul_lt[c][i] = gf_mul(c, i);

}

#define	PREFETCHNTA(ptr, offset)	{}
#define	PREFETCH(ptr, offset) 		{}

#define	XOR_ACC(src, acc)	acc.e ^= ((v_t *)src)[0].e
#define	XOR(src, acc)		acc.e ^= src.e
#define	COPY(src, dst)		dst = src
#define	LOAD(src, val) 		val = ((v_t *)src)[0]
#define	STORE(dst, val)		((v_t *)dst)[0] = val

/*
 * Constants used for optimized multiplication by 2.
 */
static const struct {
	iv_t mod;
	iv_t mask;
	iv_t msb;
} scalar_mul2_consts = {
#if ELEM_SIZE == 8
	.mod	= 0x1d1d1d1d1d1d1d1dULL,
	.mask	= 0xfefefefefefefefeULL,
	.msb	= 0x8080808080808080ULL,
#else
	.mod	= 0x1d1d1d1dULL,
	.mask	= 0xfefefefeULL,
	.msb	= 0x80808080ULL,
#endif
};

#define	MUL2_SETUP() {}

#define	MUL2(a)								\
{									\
	iv_t _mask;							\
									\
	_mask = (a).e & scalar_mul2_consts.msb;				\
	_mask = (_mask << 1) - (_mask >> 7);				\
	(a).e = ((a).e << 1) & scalar_mul2_consts.mask;			\
	(a).e = (a).e ^ (_mask & scalar_mul2_consts.mod);		\
}

#define	MUL4(a) 							\
{									\
	MUL2(a);							\
	MUL2(a);							\
}

#define	MUL(c, a)							\
{									\
	const uint8_t *mul_lt = vdev_raidz_mul_lt[c];			\
	switch (ELEM_SIZE) {						\
	case 8:								\
		a.b[7] = mul_lt[a.b[7]];				\
		a.b[6] = mul_lt[a.b[6]];				\
		a.b[5] = mul_lt[a.b[5]];				\
		a.b[4] = mul_lt[a.b[4]];				\
	case 4:								\
		a.b[3] = mul_lt[a.b[3]];				\
		a.b[2] = mul_lt[a.b[2]];				\
		a.b[1] = mul_lt[a.b[1]];				\
		a.b[0] = mul_lt[a.b[0]];				\
		break;							\
	}								\
}

#define	raidz_math_begin()	{}
#define	raidz_math_end()	{}



#if !defined(RAIDZ_CONFIG_HIGHMEM)

#define	GEN_P_DEFINE() v_t p0
#define	GEN_P_STRIDE	1
#define	GEN_P_P		p0

#define	GEN_PQ_DEFINE() v_t d0, p0, q0
#define	GEN_PQ_STRIDE	1
#define	GEN_PQ_D	d0
#define	GEN_PQ_P	p0
#define	GEN_PQ_Q	q0

#define	GEN_PQR_DEFINE() v_t d0, p0, q0, r0
#define	GEN_PQR_STRIDE	1
#define	GEN_PQR_D	d0
#define	GEN_PQR_P	p0
#define	GEN_PQR_Q	q0
#define	GEN_PQR_R	r0

#define	REC_P_DEFINE() 	v_t x0
#define	REC_P_STRIDE	1
#define	REC_P_X		x0

#define	REC_Q_DEFINE() 	v_t x0
#define	REC_Q_STRIDE	1
#define	REC_Q_X		x0

#define	REC_R_DEFINE() 	v_t x0
#define	REC_R_STRIDE	1
#define	REC_R_X		x0

#define	REC_PQ_DEFINE() v_t x0, y0, d0
#define	REC_PQ_STRIDE	1
#define	REC_PQ_X	x0
#define	REC_PQ_Y	y0
#define	REC_PQ_D	d0

#define	REC_PR_DEFINE() v_t x0, y0, d0
#define	REC_PR_STRIDE	1
#define	REC_PR_X	x0
#define	REC_PR_Y	y0
#define	REC_PR_D	d0

#define	REC_QR_DEFINE() v_t x0, y0, d0
#define	REC_QR_STRIDE	1
#define	REC_QR_X	x0
#define	REC_QR_Y	y0
#define	REC_QR_D	d0

#define	REC_PQR_DEFINE() v_t x0, y0, z0, d0, t0
#define	REC_PQR_STRIDE	1
#define	REC_PQR_X	x0
#define	REC_PQR_Y	y0
#define	REC_PQR_Z	z0
#define	REC_PQR_D	d0
#define	REC_PQR_XS	d0
#define	REC_PQR_YS	t0




#else





#define	SYN_STRIDE	1


#define	ADD_DEFINE() 	v_t d0
#define	ADD_STRIDE	1
#define	ADD_D 		d0

#define	MUL_DEFINE()	v_t d0
#define	MUL_STRIDE	1
#define	MUL_D		d0

#define	GEN_P_STRIDE	1
#define	GEN_P_DEFINE()	v_t p0
#define	GEN_P_P		p0

#define	GEN_PQ_STRIDE	1
#define	GEN_PQ_DEFINE() v_t d0, c0
#define	GEN_PQ_D	d0
#define	GEN_PQ_C	c0

#define	GEN_PQR_STRIDE		1
#define	GEN_PQR_DEFINE()	v_t d0, c0
#define	GEN_PQR_D		d0
#define	GEN_PQR_C		c0

#define	SYN_Q_DEFINE()	v_t d0, x0
#define	SYN_Q_D		d0
#define	SYN_Q_X		x0


#define	SYN_R_DEFINE()	v_t d0, x0
#define	SYN_R_D		d0
#define	SYN_R_X		x0


#define	SYN_PQ_DEFINE() v_t d0, x0
#define	SYN_PQ_D	d0
#define	SYN_PQ_X	x0


#define	REC_PQ_STRIDE	1
#define	REC_PQ_DEFINE() v_t x0, y0, t0
#define	REC_PQ_X	x0
#define	REC_PQ_Y	y0
#define	REC_PQ_T	t0


#define	SYN_PR_DEFINE() v_t d0, x0
#define	SYN_PR_D	d0
#define	SYN_PR_X	x0

#define	REC_PR_STRIDE	1
#define	REC_PR_DEFINE() v_t x0, y0, t0
#define	REC_PR_X	x0
#define	REC_PR_Y	y0
#define	REC_PR_T	t0


#define	SYN_QR_DEFINE() v_t d0, x0
#define	SYN_QR_D	d0
#define	SYN_QR_X	x0


#define	REC_QR_STRIDE	1
#define	REC_QR_DEFINE() v_t x0, y0, t0
#define	REC_QR_X	x0
#define	REC_QR_Y	y0
#define	REC_QR_T	t0


#define	SYN_PQR_DEFINE() v_t d0, x0
#define	SYN_PQR_D	d0
#define	SYN_PQR_X	x0

#define	REC_PQR_STRIDE	1
#define	REC_PQR_DEFINE() v_t x0, y0, z0, xs0, ys0
#define	REC_PQR_X	x0
#define	REC_PQR_Y	y0
#define	REC_PQR_Z	z0
#define	REC_PQR_XS	xs0
#define	REC_PQR_YS	ys0

#endif

#include "vdev_raidz_math_impl.h"

DEFINE_GEN_METHODS(scalar);
DEFINE_REC_METHODS(scalar);

static boolean_t
raidz_will_scalar_work(void)
{
	return (B_TRUE); /* always */
}

const raidz_impl_ops_t vdev_raidz_scalar_impl = {
	.init = raidz_init_scalar,
	.fini = NULL,
	.gen = RAIDZ_GEN_METHODS(scalar),
	.rec = RAIDZ_REC_METHODS(scalar),
	.is_supported = &raidz_will_scalar_work,
	.name = "scalar"
};

/* Powers of 2 in the RAID-Z Galois field. */
const uint8_t vdev_raidz_pow2[256] __attribute__((aligned(256))) = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
	0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26,
	0x4c, 0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9,
	0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0,
	0x9d, 0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35,
	0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23,
	0x46, 0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0,
	0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1,
	0x5f, 0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc,
	0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0,
	0xfd, 0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f,
	0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2,
	0xd9, 0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88,
	0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce,
	0x81, 0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93,
	0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc,
	0x85, 0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9,
	0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54,
	0xa8, 0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa,
	0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73,
	0xe6, 0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e,
	0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff,
	0xe3, 0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4,
	0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41,
	0x82, 0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e,
	0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6,
	0x51, 0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef,
	0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09,
	0x12, 0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5,
	0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16,
	0x2c, 0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83,
	0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01
};

/* Logs of 2 in the RAID-Z Galois field. */
const uint8_t vdev_raidz_log2[256] __attribute__((aligned(256))) = {
	0x00, 0x00, 0x01, 0x19, 0x02, 0x32, 0x1a, 0xc6,
	0x03, 0xdf, 0x33, 0xee, 0x1b, 0x68, 0xc7, 0x4b,
	0x04, 0x64, 0xe0, 0x0e, 0x34, 0x8d, 0xef, 0x81,
	0x1c, 0xc1, 0x69, 0xf8, 0xc8, 0x08, 0x4c, 0x71,
	0x05, 0x8a, 0x65, 0x2f, 0xe1, 0x24, 0x0f, 0x21,
	0x35, 0x93, 0x8e, 0xda, 0xf0, 0x12, 0x82, 0x45,
	0x1d, 0xb5, 0xc2, 0x7d, 0x6a, 0x27, 0xf9, 0xb9,
	0xc9, 0x9a, 0x09, 0x78, 0x4d, 0xe4, 0x72, 0xa6,
	0x06, 0xbf, 0x8b, 0x62, 0x66, 0xdd, 0x30, 0xfd,
	0xe2, 0x98, 0x25, 0xb3, 0x10, 0x91, 0x22, 0x88,
	0x36, 0xd0, 0x94, 0xce, 0x8f, 0x96, 0xdb, 0xbd,
	0xf1, 0xd2, 0x13, 0x5c, 0x83, 0x38, 0x46, 0x40,
	0x1e, 0x42, 0xb6, 0xa3, 0xc3, 0x48, 0x7e, 0x6e,
	0x6b, 0x3a, 0x28, 0x54, 0xfa, 0x85, 0xba, 0x3d,
	0xca, 0x5e, 0x9b, 0x9f, 0x0a, 0x15, 0x79, 0x2b,
	0x4e, 0xd4, 0xe5, 0xac, 0x73, 0xf3, 0xa7, 0x57,
	0x07, 0x70, 0xc0, 0xf7, 0x8c, 0x80, 0x63, 0x0d,
	0x67, 0x4a, 0xde, 0xed, 0x31, 0xc5, 0xfe, 0x18,
	0xe3, 0xa5, 0x99, 0x77, 0x26, 0xb8, 0xb4, 0x7c,
	0x11, 0x44, 0x92, 0xd9, 0x23, 0x20, 0x89, 0x2e,
	0x37, 0x3f, 0xd1, 0x5b, 0x95, 0xbc, 0xcf, 0xcd,
	0x90, 0x87, 0x97, 0xb2, 0xdc, 0xfc, 0xbe, 0x61,
	0xf2, 0x56, 0xd3, 0xab, 0x14, 0x2a, 0x5d, 0x9e,
	0x84, 0x3c, 0x39, 0x53, 0x47, 0x6d, 0x41, 0xa2,
	0x1f, 0x2d, 0x43, 0xd8, 0xb7, 0x7b, 0xa4, 0x76,
	0xc4, 0x17, 0x49, 0xec, 0x7f, 0x0c, 0x6f, 0xf6,
	0x6c, 0xa1, 0x3b, 0x52, 0x29, 0x9d, 0x55, 0xaa,
	0xfb, 0x60, 0x86, 0xb1, 0xbb, 0xcc, 0x3e, 0x5a,
	0xcb, 0x59, 0x5f, 0xb0, 0x9c, 0xa9, 0xa0, 0x51,
	0x0b, 0xf5, 0x16, 0xeb, 0x7a, 0x75, 0x2c, 0xd7,
	0x4f, 0xae, 0xd5, 0xe9, 0xe6, 0xe7, 0xad, 0xe8,
	0x74, 0xd6, 0xf4, 0xea, 0xa8, 0x50, 0x58, 0xaf,
};
