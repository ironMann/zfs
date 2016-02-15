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

#ifndef	_VDEV_RAIDZ_MATH_X86SIMD_H
#define	_VDEV_RAIDZ_MATH_X86SIMD_H

#include <sys/types.h>

/* Convert from GF log to perform multiplications using SIMD */
#define	fix_mul_exp(e)	(vdev_raidz_pow2[e])

static raidz_inline void
raidz_math_begin(void)
{
	kfpu_begin();
}
static raidz_inline void
raidz_math_end(void)
{
	FLUSH();
	kfpu_end();
}

#define	P_D_SYNDROME(D, T, t)		\
{					\
	LOAD((t), T);			\
	XOR(D, T);			\
	STORE((t), T);			\
}

#define	Q_D_SYNDROME(D, T, t)		\
{					\
	LOAD((t), T);			\
	MUL2(T);			\
	XOR(D, T);			\
	STORE((t), T);			\
}

#define	Q_SYNDROME(T, t)		\
{					\
	LOAD((t), T);			\
	MUL2(T);			\
	STORE((t), T);			\
}

#define	R_D_SYNDROME(D, T, t)		\
{					\
	LOAD((t), T);			\
	MUL4(T);			\
	XOR(D, T);			\
	STORE((t), T);			\
}

#define	R_SYNDROME(T, t)		\
{					\
	LOAD((t), T);			\
	MUL4(T);			\
	STORE((t), T);			\
}

static int
raidz_add_abd(void *dc, void *sc, uint64_t dsize, uint64_t ssize, void *private)
{
#define	ADD_D 0, 1, 2, 3

	v_t *dst = (v_t *) dc;
	const v_t *src = (v_t *) sc;
	const v_t * const src_end = src + (ssize / sizeof (v_t));

	for (; src < src_end; src += 4, dst += 4) {
		PREFETCH(src, 2, 4);

		LOAD(dst, ADD_D);
		XOR_ACC(src, ADD_D);
		STORE(dst, ADD_D);
	}
	return (0);

#undef	ADD_D
}

static int
raidz_mul_abd(void *dc, uint64_t size, void *private)
{
#define	MUL_D 0, 1, 2, 3

	const unsigned mul = *((unsigned *) private);
	v_t *d = (v_t *) dc;
	v_t * const dend = d + (size / sizeof (v_t));

	for (; d < dend; d += 4) {
		LOAD(d, MUL_D);
		MUL(mul, MUL_D);
		STORE(d, MUL_D);
	}
	return (0);

#undef	MUL_D
}

static void
raidz_gen_pq_add(void **c, const void *dc, const size_t csize,
	const size_t dsize)
{
#define	GEN_PQ_D 0, 1, 2, 3
#define	GEN_PQ_C 4, 5, 6, 7

	v_t *p = (v_t *) c[CODE_P];
	v_t *q = (v_t *) c[CODE_Q];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const qend = q + (csize / sizeof (v_t));

	MUL2_SETUP();

	for (; d < dend; d += 4, p += 4, q += 4) {
		PREFETCH(d, 2, 4);

		LOAD(d, GEN_PQ_D);
		P_D_SYNDROME(GEN_PQ_D, GEN_PQ_C, p);
		Q_D_SYNDROME(GEN_PQ_D, GEN_PQ_C, q);
	}
	for (; q < qend; q += 4) {
		Q_SYNDROME(GEN_PQ_C, q);
	}
#undef	GEN_PQ_D
#undef	GEN_PQ_C
}

static void
raidz_gen_pqr_add(void **c, const void *dc, const size_t csize,
	const size_t dsize)
{
#define	GEN_PQR_C 0, 1, 2, 3
#define	GEN_PQR_D 4, 5, 6, 7

	v_t *p = (v_t *) c[CODE_P];
	v_t *q = (v_t *) c[CODE_Q];
	v_t *r = (v_t *) c[CODE_R];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const qend = q + (csize / sizeof (v_t));

	MUL2_SETUP();

	for (; d < dend; d += 4, p += 4, q += 4, r += 4) {
		PREFETCH(d, 2, 4);

		LOAD(d, GEN_PQR_D);
		P_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, p);
		Q_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, q);
		R_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, r);
	}
	for (; q < qend; q += 4, r += 4) {
		Q_SYNDROME(GEN_PQR_C, q);
		R_SYNDROME(GEN_PQR_C, r);
	}

#undef	GEN_PQR_C
#undef	GEN_PQR_D
}

static void
raidz_syndrome_q_abd(void **xc, const void *dc, const size_t xsize,
	const size_t dsize)
{
#define	SYN_Q_D	0, 1, 2, 3
#define	SYN_Q_X	4, 5, 6, 7

	v_t *x = (v_t *) xc[0];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const xend = x + (xsize / sizeof (v_t));

	MUL2_SETUP();

	for (; d < dend; d += 4, x += 4) {
		PREFETCH(d, 2, 4);

		LOAD(d, SYN_Q_D);
		Q_D_SYNDROME(SYN_Q_D, SYN_Q_X, x);
	}
	for (; x < xend; x += 4) {
		Q_SYNDROME(SYN_Q_X, x);
	}

#undef	SYN_Q_D
#undef	SYN_Q_X
}

static void
raidz_syndrome_r_abd(void **xc, const void *dc, const size_t xsize,
	const size_t dsize)
{
#define	SYN_R_D	0, 1, 2, 3
#define	SYN_R_X	4, 5, 6, 7

	v_t *x = (v_t *) xc[0];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const xend = x + (xsize / sizeof (v_t));

	MUL2_SETUP();

	for (; d < dend; d += 4, x += 4) {
		PREFETCH(d, 2, 4);

		LOAD(d, SYN_R_D);
		R_D_SYNDROME(SYN_R_D, SYN_R_X, x);
	}
	for (; x < xend; x += 4) {
		R_SYNDROME(SYN_R_X, x);
	}

#undef	SYN_R_D
#undef	SYN_R_X
}

static void
raidz_syndrome_pq_abd(void **tc, const void *dc, const size_t tsize,
	const size_t dsize)
{
#define	SYN_PQ_D	0, 1, 2, 3
#define	SYN_PQ_T	4, 5, 6, 7

	v_t *x = (v_t *) tc[0];
	v_t *y = (v_t *) tc[1];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const yend = y + (tsize / sizeof (v_t));

	MUL2_SETUP();

	for (; d < dend; d += 4, x += 4, y += 4) {
		PREFETCH(d, 2, 4);

		LOAD(d, SYN_PQ_D);
		P_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, x);
		Q_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, y);
	}
	for (; y < yend; y += 4) {
		Q_SYNDROME(SYN_PQ_T, y);
	}

#undef	SYN_PQ_D
#undef	SYN_PQ_T
}


static void
raidz_rec_pq_abd(void **tc, const size_t tsize, void **c,
	const unsigned *mul)
{
#define	REC_PQ_X	0, 1
#define	REC_PQ_Y	2, 3
#define	REC_PQ_T	4, 5

	v_t *x = (v_t *) tc[0];
	v_t *y = (v_t *) tc[1];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *) c[CODE_P];
	const v_t *q = (v_t *) c[CODE_Q];

	for (; x < xend; x += 2, y += 2, p += 2, q += 2) {
		PREFETCH(p, 2, 2);
		PREFETCH(q, 2, 2);

		LOAD(x, REC_PQ_X);
		LOAD(y, REC_PQ_Y);

		XOR_ACC(p, REC_PQ_X);
		XOR_ACC(q, REC_PQ_Y);

		/* Save Pxy */
		COPY(REC_PQ_X,  REC_PQ_T);

		/* Calc X */
		MUL(mul[MUL_PQ_X], REC_PQ_X);
		MUL(mul[MUL_PQ_Y], REC_PQ_Y);
		XOR(REC_PQ_Y,  REC_PQ_X);
		STORE(x, REC_PQ_X);

		/* Calc Y */
		XOR(REC_PQ_T,  REC_PQ_X);
		STORE(y, REC_PQ_X);
	}

#undef	REC_PQ_X
#undef	REC_PQ_Y
#undef	REC_PQ_T
}

static void
raidz_syndrome_pr_abd(void **c, const void *dc, const size_t tsize,
	const size_t dsize)
{
#define	SYN_PQ_D	0, 1, 2, 3
#define	SYN_PQ_T	4, 5, 6, 7

	v_t *x = (v_t *) c[0];
	v_t *y = (v_t *) c[1];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const yend = y + (tsize / sizeof (v_t));

	MUL2_SETUP();

	for (; d < dend; d += 4, x += 4, y += 4) {
		PREFETCH(d, 2, 4);

		LOAD(d, SYN_PQ_D);
		P_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, x);
		R_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, y);
	}
	for (; y < yend; y += 4) {
		R_SYNDROME(SYN_PQ_T, y);
	}
#undef	SYN_PQ_D
#undef	SYN_PQ_T
}

static void
raidz_rec_pr_abd(void **t, const size_t tsize, void **c,
	const unsigned *mul)
{
#define	REC_PQ_X	0, 1
#define	REC_PQ_Y	2, 3
#define	REC_PQ_T	4, 5

	v_t *x = (v_t *) t[0];
	v_t *y = (v_t *) t[1];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *) c[CODE_P];
	const v_t *q = (v_t *) c[CODE_Q];

	for (; x < xend; x += 2, y += 2, p += 2, q += 2) {
		PREFETCH(p, 2, 2);
		PREFETCH(q, 2, 2);

		LOAD(x, REC_PQ_X);
		LOAD(y, REC_PQ_Y);
		XOR_ACC(p, REC_PQ_X);
		XOR_ACC(q, REC_PQ_Y);

		/* Save Pxy */
		COPY(REC_PQ_X,  REC_PQ_T);

		/* Calc X */
		MUL(mul[MUL_PR_X], REC_PQ_X);
		MUL(mul[MUL_PR_Y], REC_PQ_Y);
		XOR(REC_PQ_Y,  REC_PQ_X);
		STORE(x, REC_PQ_X);

		/* Calc Y */
		XOR(REC_PQ_T,  REC_PQ_X);
		STORE(y, REC_PQ_X);
	}

#undef	REC_PQ_X
#undef	REC_PQ_Y
#undef	REC_PQ_T
}

static void
raidz_syndrome_qr_abd(void **c, const void *dc, const size_t tsize,
	const size_t dsize)
{
#define	SYN_PQ_D	0, 1, 2, 3
#define	SYN_PQ_T	4, 5, 6, 7

	v_t *x = (v_t *) c[0];
	v_t *y = (v_t *) c[1];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));

	MUL2_SETUP();

	for (; d < dend; d += 4, x += 4, y += 4) {
		PREFETCH(d, 2, 4);

		LOAD(d, SYN_PQ_D);
		Q_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, x);
		R_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, y);
	}
	for (; x < xend; x += 4, y += 4) {
		Q_SYNDROME(SYN_PQ_T, x);
		R_SYNDROME(SYN_PQ_T, y);
	}

#undef	SYN_PQ_D
#undef	SYN_PQ_T
}

static void
raidz_rec_qr_abd(void **t, const size_t tsize, void **c,
	const unsigned *mul)
{
#define	REC_QR_X	0, 1
#define	REC_QR_Y	2, 3
#define	REC_QR_T	4, 5

	v_t *x = (v_t *) t[0];
	v_t *y = (v_t *) t[1];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *) c[CODE_P];
	const v_t *q = (v_t *) c[CODE_Q];

	for (; x < xend; x += 2, y += 2, p += 2, q += 2) {
		PREFETCH(p, 2, 2);
		PREFETCH(q, 2, 2);

		LOAD(x, REC_QR_X);
		LOAD(y, REC_QR_Y);

		XOR_ACC(p, REC_QR_X);
		XOR_ACC(q, REC_QR_Y);

		/* Save Pxy */
		COPY(REC_QR_X,  REC_QR_T);

		/* Calc X */
		MUL(mul[MUL_QR_XQ], REC_QR_X);	/* X = Q * xqm */
		XOR(REC_QR_Y, REC_QR_X);	/* X = R ^ X   */
		MUL(mul[MUL_QR_X], REC_QR_X);	/* X = X * xm  */
		STORE(x, REC_QR_X);

		/* Calc Y */
		MUL(mul[MUL_QR_YQ], REC_QR_T);	/* X = Q * xqm */
		XOR(REC_QR_Y, REC_QR_T);	/* X = R ^ X   */
		MUL(mul[MUL_QR_Y], REC_QR_T);	/* X = X * xm  */
		STORE(y, REC_QR_T);
	}

#undef	REC_QR_X
#undef	REC_QR_Y
#undef	REC_QR_T
}


static void
raidz_syndrome_pqr_abd(void **c, const void *dc, const size_t tsize,
	const size_t dsize)
{
#define	SYN_PQR_D	0, 1, 2, 3
#define	SYN_PQR_T	4, 5, 6, 7

	v_t *x = (v_t *) c[0];
	v_t *y = (v_t *) c[1];
	v_t *z = (v_t *) c[2];
	const v_t * const yend = y + (tsize / sizeof (v_t));
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));

	MUL2_SETUP();

	for (; d < dend;  d += 4, x += 4, y += 4, z += 4) {
		PREFETCH(d, 2, 4);

		LOAD(d, SYN_PQR_D);
		P_D_SYNDROME(SYN_PQR_D, SYN_PQR_T, x)
		Q_D_SYNDROME(SYN_PQR_D, SYN_PQR_T, y);
		R_D_SYNDROME(SYN_PQR_D, SYN_PQR_T, z);
	}
	for (; y < yend; y += 4, z += 4) {
		Q_SYNDROME(SYN_PQR_T, y);
		R_SYNDROME(SYN_PQR_T, z);
	}

#undef	SYN_PQR_D
#undef	SYN_PQR_T
}

static void
raidz_rec_pqr_abd(void **t, const size_t tsize, void **c,
	const unsigned * const mul)
{
#define	REC_PQR_X	0, 1
#define	REC_PQR_Y	2, 3
#define	REC_PQR_Z	4, 5
#define	REC_PQR_XS	6, 7
#define	REC_PQR_YS	8, 9

	v_t *x = (v_t *) t[0];
	v_t *y = (v_t *) t[1];
	v_t *z = (v_t *) t[2];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *) c[CODE_P];
	const v_t *q = (v_t *) c[CODE_Q];
	const v_t *r = (v_t *) c[CODE_R];

	for (; x < xend; x += 2, y += 2, z += 2, p += 2, q += 2, r += 2) {
		PREFETCH(p, 2, 2);
		PREFETCH(q, 2, 2);
		PREFETCH(r, 2, 2);

		LOAD(x, REC_PQR_X);
		LOAD(y, REC_PQR_Y);
		LOAD(z, REC_PQR_Z);

		XOR_ACC(p, REC_PQR_X);
		XOR_ACC(q, REC_PQR_Y);
		XOR_ACC(r, REC_PQR_Z);

		/* Save Pxyz and Qxyz */
		COPY(REC_PQR_X, REC_PQR_XS);
		COPY(REC_PQR_Y, REC_PQR_YS);

		/* Calc X */
		MUL(mul[MUL_PQR_XP], REC_PQR_X);	/* Xp = Pxyz * xp   */
		MUL(mul[MUL_PQR_XQ], REC_PQR_Y);	/* Xq = Qxyz * xq   */
		XOR(REC_PQR_Y, REC_PQR_X);
		MUL(mul[MUL_PQR_XR], REC_PQR_Z);	/* Xr = Rxyz * xr   */
		XOR(REC_PQR_Z, REC_PQR_X);		/* X = Xp + Xq + Xr */
		STORE(x, REC_PQR_X);

		/* Calc Y */
		XOR(REC_PQR_X, REC_PQR_XS); 		/* Pyz = Pxyz + X */
		MUL(mul[MUL_PQR_YU], REC_PQR_X);  	/* Xq = X * upd_q */
		XOR(REC_PQR_X, REC_PQR_YS); 		/* Qyz = Qxyz + Xq */
		COPY(REC_PQR_XS, REC_PQR_X);		/* restore Pyz */
		MUL(mul[MUL_PQR_YP], REC_PQR_X);	/* Yp = Pyz * yp */
		MUL(mul[MUL_PQR_YQ], REC_PQR_YS);	/* Yq = Qyz * yq */
		XOR(REC_PQR_X, REC_PQR_YS); 		/* Y = Yp + Yq */
		STORE(y, REC_PQR_YS);

		/* Calc Z */
		XOR(REC_PQR_XS, REC_PQR_YS);		/* Z = Pz = Pyz + Y */
		STORE(z, REC_PQR_YS);
	}

#undef	REC_PQR_X
#undef	REC_PQR_Y
#undef	REC_PQR_Z
#undef	REC_PQR_XS
#undef	REC_PQR_YS
}

#endif  /* _VDEV_RAIDZ_MATH_X86SIMD_H */
