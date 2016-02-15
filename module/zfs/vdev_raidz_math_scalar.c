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
typedef uint32_t ielem_t;
#elif ((~(0x0ULL)) >> 56) == 0xffULL
#define	ELEM_SIZE	8
typedef uint64_t ielem_t;
#endif

typedef union elem {
	ielem_t e;
	uint8_t b[sizeof (ielem_t)];
} elem_t;

static const struct raidz_scalar_constants {
	elem_t mod;
	elem_t mask;
	elem_t msb;
} raidz_scalar_constants = {
#if ELEM_SIZE == 8
	.mod.e	= 0x1d1d1d1d1d1d1d1dULL,
	.mask.e	= 0xfefefefefefefefeULL,
	.msb.e	= 0x8080808080808080ULL,
#else
	.mod.e	= 0x1d1d1d1dULL,
	.mask.e	= 0xfefefefeULL,
	.msb.e	= 0x80808080ULL,
#endif
};

/* Keep multiplication in log form for scalar */
static raidz_inline int
fix_mul_exp(int e) {
	while (e > 255)
		e -= 255;
	return (e);
}

static raidz_inline void
raidz_math_begin(void) {
	/* nop */
}

static raidz_inline void
raidz_math_end(void) {
	/* nop */
}

static raidz_inline void
xor_acc_x2(const elem_t *data, elem_t *a0, elem_t *a1)
{
	a0->e ^= data[0].e;
	a1->e ^= data[1].e;
}

static raidz_inline void
load_x2(const elem_t *src, elem_t *a0, elem_t *a1)
{
	a0->e = src[0].e;
	a1->e = src[1].e;
}

static raidz_inline void
store_x2(elem_t *dst, elem_t a0, elem_t a1)
{
	dst[0].e = a0.e;
	dst[1].e = a1.e;
}

static raidz_inline void
xor_x2(const elem_t a0, const elem_t a1, elem_t *r0, elem_t *r1)
{
	r0->e ^= a0.e;
	r1->e ^= a1.e;
}

static raidz_inline void
copy_x2(const elem_t a0, const elem_t a1, elem_t *c0, elem_t *c1)
{
	c0->e = a0.e;
	c1->e = a1.e;
}

static raidz_inline void
mul2_x2(elem_t *a0, elem_t *a1)
{
	elem_t cmp, dbl, mask;

	cmp.e = (*a0).e & raidz_scalar_constants.msb.e;
	mask.e = (cmp.e << 1) - (cmp.e >> 7);
	dbl.e = ((*a0).e << 1) & raidz_scalar_constants.mask.e;
	(*a0).e = dbl.e ^ (mask.e & raidz_scalar_constants.mod.e);

	cmp.e = (*a1).e & raidz_scalar_constants.msb.e;
	mask.e = (cmp.e << 1) - (cmp.e >> 7);
	dbl.e = ((*a1).e << 1) & raidz_scalar_constants.mask.e;
	(*a1).e = dbl.e ^ (mask.e & raidz_scalar_constants.mod.e);
}

static raidz_inline void
mul4_x2(elem_t *a0, elem_t *a1)
{
	mul2_x2(a0, a1);
	mul2_x2(a0, a1);
}

static raidz_inline elem_t
exp2_x1(const elem_t a, const int mul)
{
	elem_t r;

#if ELEM_SIZE == 8
	r.b[7] = vdev_raidz_exp2(a.b[7], mul);
	r.b[6] = vdev_raidz_exp2(a.b[6], mul);
	r.b[5] = vdev_raidz_exp2(a.b[5], mul);
	r.b[4] = vdev_raidz_exp2(a.b[4], mul);
#endif
	r.b[3] = vdev_raidz_exp2(a.b[3], mul);
	r.b[2] = vdev_raidz_exp2(a.b[2], mul);
	r.b[1] = vdev_raidz_exp2(a.b[1], mul);
	r.b[0] = vdev_raidz_exp2(a.b[0], mul);

	return (r);
}

static raidz_inline void
exp2_x2(elem_t *a0, elem_t *a1, const int mul)
{
	*a0 = exp2_x1(*a0, mul);
	*a1 = exp2_x1(*a1, mul);
}


static raidz_inline int
raidz_add_abd(void *d, void *s, uint64_t dsize, uint64_t ssize,
	void *private)
{
	elem_t *dst = (elem_t *)d;
	const elem_t *src = (elem_t *)s;
	const elem_t * const srcend = src + (ssize / sizeof (elem_t));
	elem_t d0, d1;

	for (; src < srcend; src += 2, dst += 2) {
		load_x2(src, &d0, &d1);
		xor_x2(d0, d1, dst, dst+1);
	}
	return (0);
}

static raidz_inline int
raidz_mul_abd(void *d, uint64_t size, void *private)
{
	elem_t *dst = (elem_t *)d;
	elem_t * const dstend = dst + (size / sizeof (elem_t));
	const unsigned *mul = (unsigned *)private;

	for (; dst < dstend; dst++) {
		*dst = exp2_x1(*dst, mul[MUL_Q_X]);
	}
	return (0);
}

static raidz_inline void
raidz_gen_pq_add(void **c, const void *dc, const size_t csize,
	const size_t dsize)
{
	elem_t d0, d1, q0, q1;
	elem_t *p = (elem_t *) c[CODE_P];
	elem_t *q = (elem_t *) c[CODE_Q];
	const elem_t *d = (elem_t *) dc;
	const elem_t * const dend = d + (dsize / sizeof (elem_t));
	const elem_t * const qend = q + (csize / sizeof (elem_t));

	for (; d < dend; d += 2, p += 2, q += 2) {
		load_x2(d, &d0, &d1);
		load_x2(q, &q0, &q1);

		xor_x2(d0, d1, &p[0], &p[1]);

		mul2_x2(&q0, &q1);
		xor_x2(d0, d1, &q0, &q1);
		store_x2(q, q0, q1);
	}
	for (; q < qend; q += 2) {
		load_x2(q, &q0, &q1);
		mul2_x2(&q0, &q1);
		store_x2(q, q0, q1);
	}
}

static raidz_inline void
raidz_gen_pqr_add(void **c, const void *dc, const size_t csize,
	const size_t dsize)
{
	elem_t d0, d1, q0, q1, r0, r1;
	elem_t *p = (elem_t *) c[CODE_P];
	elem_t *q = (elem_t *) c[CODE_Q];
	elem_t *r = (elem_t *) c[CODE_R];
	const elem_t *d = (elem_t *) dc;
	const elem_t * const dend = d + (dsize / sizeof (elem_t));
	const elem_t * const qend = q + (csize / sizeof (elem_t));

	for (; d < dend; d += 2, p += 2, q += 2, r += 2) {
		load_x2(d, &d0, &d1);
		load_x2(q, &q0, &q1);
		load_x2(r, &r0, &r1);

		xor_x2(d0, d1, p, p+1);

		mul2_x2(&q0, &q1);
		xor_x2(d0, d1, &q0, &q1);
		store_x2(q, q0, q1);

		mul4_x2(&r0, &r1);
		xor_x2(d0, d1, &r0, &r1);
		store_x2(r, r0, r1);
	}
	for (; q < qend; q += 2, r += 2) {
		load_x2(q, &q0, &q1);
		load_x2(r, &r0, &r1);

		mul2_x2(&q0, &q1);
		store_x2(q, q0, q1);

		mul4_x2(&r0, &r1);
		store_x2(r, r0, r1);
	}
}

static raidz_inline void
raidz_syndrome_q_abd(void **c, const void *dc, const size_t xsize,
	const size_t dsize)
{
	elem_t d0, d1, x0, x1;
	elem_t *x = (elem_t *) c[0];
	const elem_t *d = (elem_t *) dc;
	const elem_t * const dend = d + (dsize / sizeof (elem_t));
	const elem_t * const xend = x + (xsize / sizeof (elem_t));

	for (; d < dend; d += 2, x += 2) {
		load_x2(d, &d0, &d1);
		load_x2(x, &x0, &x1);

		mul2_x2(&x0, &x1);
		xor_x2(d0, d1, &x0, &x1);
		store_x2(x, x0, x1);
	}
	for (; x < xend; x += 2) {
		load_x2(x, &x0, &x1);
		mul2_x2(&x0, &x1);
		store_x2(x, x0, x1);
	}
}


static raidz_inline void
raidz_syndrome_r_abd(void **c, const void *dc, const size_t xsize,
	const size_t dsize)
{
	elem_t d0, d1, x0, x1;
	elem_t *x = (elem_t *) c[0];
	const elem_t *d = (elem_t *) dc;
	const elem_t * const dend = d + (dsize / sizeof (elem_t));
	const elem_t * const xend = x + (xsize / sizeof (elem_t));

	for (; d < dend; d += 2, x += 2) {
		load_x2(d, &d0, &d1);
		load_x2(x, &x0, &x1);

		mul4_x2(&x0, &x1);
		xor_x2(d0, d1, &x0, &x1);
		store_x2(x, x0, x1);
	}
	for (; x < xend; x += 2) {
		load_x2(x, &x0, &x1);
		mul4_x2(&x0, &x1);
		store_x2(x, x0, x1);
	}
}


static raidz_inline void
raidz_syndrome_pq_abd(void **c, const void *dc, const size_t xsize,
	const size_t dsize)
{
	elem_t d0, d1, x0, x1, y0, y1;
	elem_t *x = (elem_t *) c[0];
	elem_t *y = (elem_t *) c[1];
	const elem_t *d = (elem_t *) dc;
	const elem_t * const dend = d + (dsize / sizeof (elem_t));
	const elem_t * const yend = y + (xsize / sizeof (elem_t));

	for (; d < dend; d += 2, x += 2, y += 2) {
		load_x2(d, &d0, &d1);
		load_x2(x, &x0, &x1);
		load_x2(y, &y0, &y1);

		mul2_x2(&y0, &y1);
		xor_x2(d0, d1, &x0, &x1);
		xor_x2(d0, d1, &y0, &y1);
		store_x2(x, x0, x1);
		store_x2(y, y0, y1);
	}
	for (; y < yend; y += 2) {
		load_x2(y, &y0, &y1);
		mul2_x2(&y0, &y1);
		store_x2(y, y0, y1);
	}
}

static void
raidz_rec_pq_abd(void **t, const size_t tsize, void **c, const unsigned *mul)
{
	elem_t x0, x1, y0, y1, t0, t1;
	const elem_t *p = (elem_t *) c[CODE_P];
	const elem_t *q = (elem_t *) c[CODE_Q];
	elem_t *x = (elem_t *) t[0];
	elem_t *y = (elem_t *) t[1];
	const elem_t * const xend = x + (tsize / sizeof (elem_t));

	for (; x < xend; x += 2, y += 2, p += 2, q += 2) {
		load_x2(x, &x0, &x1);
		load_x2(y, &y0, &y1);
		xor_acc_x2(p, &x0, &x1);
		xor_acc_x2(q, &y0, &y1);

		/* Save Pxy */
		copy_x2(x0, x1, &t0, &t1);
		/* Calc X */
		exp2_x2(&x0, &x1, mul[MUL_PQ_X]);
		exp2_x2(&y0, &y1, mul[MUL_PQ_Y]);
		xor_x2(y0, y1, &x0, &x1);
		store_x2(x, x0, x1);
		/* Calc Y */
		xor_x2(t0, t1, &x0, &x1);
		store_x2(y, x0, x1);
	}
}


static raidz_inline void
raidz_syndrome_pr_abd(void **c, const void *dc, const size_t xsize,
	const size_t dsize)
{
	elem_t d0, d1, x0, x1, y0, y1;
	elem_t *x = (elem_t *) c[0];
	elem_t *y = (elem_t *) c[1];
	const elem_t *d = (elem_t *) dc;
	const elem_t * const dend = d + (dsize / sizeof (elem_t));
	const elem_t * const yend = y + (xsize / sizeof (elem_t));

	for (; d < dend; d += 2, x += 2, y += 2) {
		load_x2(d, &d0, &d1);
		load_x2(x, &x0, &x1);
		load_x2(y, &y0, &y1);

		mul4_x2(&y0, &y1);
		xor_x2(d0, d1, &x0, &x1);
		xor_x2(d0, d1, &y0, &y1);
		store_x2(x, x0, x1);
		store_x2(y, y0, y1);
	}
	for (; y < yend; y += 2) {
		load_x2(y, &y0, &y1);
		mul4_x2(&y0, &y1);
		store_x2(y, y0, y1);
	}
}

static void
raidz_rec_pr_abd(void **t, const size_t tsize, void **c, const unsigned *mul)
{
	elem_t x0, x1, y0, y1, t0, t1;
	const elem_t *p = (elem_t *) c[0];
	const elem_t *r = (elem_t *) c[1];
	elem_t *x = (elem_t *) t[0];
	elem_t *y = (elem_t *) t[1];
	const elem_t * const xend = x + (tsize / sizeof (elem_t));

	for (; x < xend; x += 2, y += 2, p += 2, r += 2) {
		load_x2(x, &x0, &x1);
		load_x2(y, &y0, &y1);
		xor_acc_x2(p, &x0, &x1);
		xor_acc_x2(r, &y0, &y1);

		/* Save Pxy */
		copy_x2(x0, x1, &t0, &t1);
		/* Calc X */
		exp2_x2(&x0, &x1, mul[MUL_PQ_X]);
		exp2_x2(&y0, &y1, mul[MUL_PQ_Y]);
		xor_x2(y0, y1, &x0, &x1);
		store_x2(x, x0, x1);
		/* Calc Y */
		xor_x2(t0, t1, &x0, &x1);
		store_x2(y, x0, x1);
	}
}


static void
raidz_syndrome_qr_abd(void **c, const void *dc, const size_t xsize,
	const size_t dsize)
{
	elem_t d0, d1, x0, x1, y0, y1;
	elem_t *x = (elem_t *) c[0];
	elem_t *y = (elem_t *) c[1];
	const elem_t *d = (elem_t *) dc;
	const elem_t * const dend = d + (dsize / sizeof (elem_t));
	const elem_t * const xend = x + (xsize / sizeof (elem_t));

	for (; d < dend; d += 2, x += 2, y += 2) {
		load_x2(d, &d0, &d1);

		load_x2(x, &x0, &x1);
		mul2_x2(&x0, &x1);
		xor_x2(d0, d1, &x0, &x1);
		store_x2(x, x0, x1);

		load_x2(y, &y0, &y1);
		mul4_x2(&y0, &y1);
		xor_x2(d0, d1, &y0, &y1);
		store_x2(y, y0, y1);
	}
	for (; x < xend; x += 2, y += 2) {
		load_x2(x, &x0, &x1);
		mul2_x2(&x0, &x1);
		store_x2(x, x0, x1);

		load_x2(y, &y0, &y1);
		mul4_x2(&y0, &y1);
		store_x2(y, y0, y1);
	}
}

static void
raidz_rec_qr_abd(void **t, const size_t tsize, void **c, const unsigned *mul)
{
	elem_t x0, x1, y0, y1, t0, t1;
	const elem_t *p = (elem_t *) c[0];
	const elem_t *q = (elem_t *) c[1];
	elem_t *x = (elem_t *) t[0];
	elem_t *y = (elem_t *) t[1];
	const elem_t * const xend = x + (tsize / sizeof (elem_t));

	for (; x < xend; x += 2, y += 2, p += 2, q += 2) {
		load_x2(x, &x0, &x1);
		load_x2(y, &y0, &y1);
		xor_acc_x2(p, &x0, &x1);
		xor_acc_x2(q, &y0, &y1);

		/* Calc X */
		copy_x2(x0, x1, &t0, &t1);
		exp2_x2(&t0, &t1, mul[MUL_QR_XQ]); /* X = Q * xqm */
		xor_x2(y0, y1, &t0, &t1);		   /* X = R ^ X   */
		exp2_x2(&t0, &t1, mul[MUL_QR_X]);  /* X = X * xm  */
		store_x2(x, t0, t1);

		/* Calc Y */
		exp2_x2(&x0, &x1, mul[MUL_QR_YQ]);
		xor_x2(y0, y1, &x0, &x1);
		exp2_x2(&x0, &x1, mul[MUL_QR_Y]);
		store_x2(y, x0, x1);
	}
}



static void
raidz_syndrome_pqr_abd(void **c, const void *dc, const size_t xsize,
	const size_t dsize)
{
	elem_t d0, d1, t0, t1;
	elem_t *x = (elem_t *) c[0];
	elem_t *y = (elem_t *) c[1];
	elem_t *z = (elem_t *) c[2];
	const elem_t *d = (elem_t *) dc;
	const elem_t * const dend = d + (dsize / sizeof (elem_t));
	const elem_t * const yend = y + (xsize / sizeof (elem_t));

	for (; d < dend;
		d += 2, x += 2, y += 2, z += 2) {
		load_x2(d, &d0, &d1);

		load_x2(x, &t0, &t1);
		xor_x2(d0, d1, &t0, &t1);
		store_x2(x, t0, t1);

		load_x2(y, &t0, &t1);
		mul2_x2(&t0, &t1);
		xor_x2(d0, d1, &t0, &t1);
		store_x2(y, t0, t1);

		load_x2(z, &t0, &t1);
		mul4_x2(&t0, &t1);
		xor_x2(d0, d1, &t0, &t1);
		store_x2(z, t0, t1);
	}
	for (; y < yend; y += 2, z += 2) {
		load_x2(y, &t0, &t1);
		mul2_x2(&t0, &t1);
		store_x2(y, t0, t1);

		load_x2(z, &t0, &t1);
		mul4_x2(&t0, &t1);
		store_x2(z, t0, t1);
	}
}

static void
raidz_rec_pqr_abd(void **t, const size_t tsize, void **c,
	const unsigned *mul)
{
	elem_t x0, x1, y0, y1, z0, z1, t0, t1, t2, t3;
	const elem_t *p = (elem_t *) c[CODE_P];
	const elem_t *q = (elem_t *) c[CODE_Q];
	const elem_t *r = (elem_t *) c[CODE_R];
	elem_t *x = (elem_t *) t[0];
	elem_t *y = (elem_t *) t[1];
	elem_t *z = (elem_t *) t[2];
	const elem_t * const xend = x + (tsize / sizeof (elem_t));

	for (; x < xend; x += 2, y += 2, z += 2, p += 2, q += 2, r += 2) {
		load_x2(x, &x0, &x1);
		load_x2(y, &y0, &y1);
		load_x2(z, &z0, &z1);
		xor_acc_x2(p, &x0, &x1);
		xor_acc_x2(q, &y0, &y1);
		xor_acc_x2(r, &z0, &z1);

		/* Calc X */
		copy_x2(x0, x1, &t0, &t1);
		exp2_x2(&t0, &t1, mul[MUL_PQR_XP]);
		copy_x2(y0, y1, &t2, &t3);
		exp2_x2(&t2, &t3, mul[MUL_PQR_XQ]);
		xor_x2(t2, t3, &t0, &t1);
		exp2_x2(&z0, &z1, mul[MUL_PQR_XR]);
		xor_x2(z0, z1, &t0, &t1);
		store_x2(x, t0, t1);

		/* Calc Y */
		xor_x2(t0, t1, &x0, &x1);
		exp2_x2(&t0, &t1, mul[MUL_PQR_YU]);
		xor_x2(t0, t1, &y0, &y1);
		copy_x2(x0, x1, &t0, &t1);
		exp2_x2(&t0, &t1, mul[MUL_PQR_YP]);
		exp2_x2(&y0, &y1, mul[MUL_PQR_YQ]);
		xor_x2(y0, y1, &t0, &t1);
		store_x2(y, t0, t1);

		/* Calc Z */
		xor_x2(x0, x1, &t0, &t1);
		store_x2(z, t0, t1);
	}
}

#include "vdev_raidz_math_impl.h"

DEFINE_GEN_METHODS(scalar);
DEFINE_REC_METHODS(scalar);

static boolean_t
raidz_math_will_scalar_work(void)
{
	return (B_TRUE); /* always */
}

const raidz_math_ops_t vdev_raidz_scalar_impl = {
	.gen = RAIDZ_GEN_METHODS(scalar),
	.rec = RAIDZ_REC_METHODS(scalar),
	.is_supported = &raidz_math_will_scalar_work,
	.name = "scalar"
};

/* Powers of 2 in the Galois field defined above. */
/*
 * Concatenate pow2 lookup table on itself so that multiplication
 * by a constant can be performed without normalization of the
 * logarithm sum. The original code:
 * vdev_raidz_exp2(a, exp) {
 * 	...
 * 	exp += vdev_raidz_log2[a]; <--- exp < 511
 * 	if(exp > 255) exp -= 255;  <--- not needed
 * 	return (vdev_raidz_pow2[exp]);
 * }
 * NOTE: the algorithm requires that the first element is left out
 */
const uint8_t vdev_raidz_pow2[511] __attribute__((aligned(256))) = {
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
	0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01,
/* NOTE */    0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
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

/* Logs of 2 in the Galois field defined above. */
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
