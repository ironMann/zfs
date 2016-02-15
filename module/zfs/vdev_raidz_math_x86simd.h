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
static raidz_inline int
fix_mul_exp(int e) {
	return ((int)vdev_raidz_pow2[e]);
}

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

#define	P_D_SYNDROME(D, T, t)\
{\
	LOAD((t), T);\
	XOR(D, T);\
	STORE((t), T);\
}

#define	Q_D_SYNDROME(D, T, t)\
{\
	LOAD((t), T);\
	MUL2(T);\
	XOR(D, T);\
	STORE((t), T);\
}

#define	Q_SYNDROME(T, t)\
{\
	LOAD((t), T);\
	MUL2(T);\
	STORE((t), T);\
}

#define	R_D_SYNDROME(D, T, t)\
{\
	LOAD((t), T);\
	MUL4(T);\
	XOR(D, T);\
	STORE((t), T);\
}

#define	R_SYNDROME(T, t)\
{\
	LOAD((t), T);\
	MUL4(T);\
	STORE((t), T);\
}


static raidz_inline void
gen_p_add(void **c, const void *dc, const size_t csize, const size_t dsize)
{
#define	GEN_P_C 0, 1, 2, 3
#define	GEN_P_D 4, 5, 6, 7

	size_t i;
	elem_t *p = (elem_t *) c[CODE_P];
	const elem_t *d = (elem_t *) dc;

	for (i = 0; i < dsize / (sizeof (elem_t)); i += 4,
		d += 4, p += 4) {

		PREFETCHNTA(d, 4, 4);
		LOAD(d, GEN_P_D);
		P_D_SYNDROME(GEN_P_D, GEN_P_C, p);
	}
#undef	GEN_P_C
#undef	GEN_P_D
}

static raidz_inline void
gen_pq_add(void **c, const void *dc, const size_t csize, const size_t dsize)
{
#define	GEN_PQ_D 0, 1, 2, 3
#define	GEN_PQ_C 4, 5, 6, 7

	size_t i;
	elem_t *p = (elem_t *) c[CODE_P];
	elem_t *q = (elem_t *) c[CODE_Q];
	const elem_t *d = (elem_t *) dc;

	MUL2_SETUP();

	for (i = 0; i < dsize / sizeof (elem_t); i += 4,
		d += 4, p += 4, q += 4) {
		PREFETCHNTA(d, 4, 4);

		LOAD(d, GEN_PQ_D);
		P_D_SYNDROME(GEN_PQ_D, GEN_PQ_C, p);
		Q_D_SYNDROME(GEN_PQ_D, GEN_PQ_C, q);
	}
	for (; i < csize / sizeof (elem_t); i += 4,
		q += 4) {
		Q_SYNDROME(GEN_PQ_C, q);
	}
#undef	GEN_PQ_D
#undef	GEN_PQ_C
}

static raidz_inline void
gen_pqr_add(void **c, const void *dc, const size_t csize, const size_t dsize)
{
#define	GEN_PQR_C 0, 1, 2, 3
#define	GEN_PQR_D 4, 5, 6, 7

	size_t i;
	elem_t *p = (elem_t *) c[CODE_P];
	elem_t *q = (elem_t *) c[CODE_Q];
	elem_t *r = (elem_t *) c[CODE_R];
	const elem_t *d = (elem_t *) dc;

	MUL2_SETUP();

	for (i = 0; i < dsize / sizeof (elem_t); i += 4,
		d += 4, p += 4, q += 4, r += 4) {
		PREFETCHNTA(d, 4, 4);

		LOAD(d, GEN_PQR_D);
		P_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, p);
		Q_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, q);
		R_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, r);
	}
	for (; i < csize / sizeof (elem_t); i += 4,
		q += 4, r += 4) {
		Q_SYNDROME(GEN_PQR_C, q);
		R_SYNDROME(GEN_PQR_C, r);
	}

#undef	GEN_PQR_C
#undef	GEN_PQR_D
}

static raidz_inline int
raidz_add_abd(void *dc, void *sc, uint64_t dsize, uint64_t ssize, void *private)
{
#define	ADD_D 0, 1, 2, 3

	size_t i;
	elem_t *dst = (elem_t *) dc;
	const elem_t *src = (elem_t *) sc;

	for (i = 0; i < dsize / sizeof (elem_t); i += 4,
		src += 4, dst += 4) {
		LOAD(dst, ADD_D);
		XOR_ACC(src, ADD_D);
		STORE(dst, ADD_D);
	}
	return (0);

#undef	ADD_D
}

static raidz_inline int
raidz_mul_abd(void *dc, uint64_t size, void *private)
{
#define	MUL_D 0, 1, 2, 3

	size_t i;
	elem_t *d = (elem_t *) dc;
	const unsigned *mul = (unsigned *) private;

	for (i = 0; i < size / sizeof (elem_t); i += 4, d += 4) {
		LOAD(d, MUL_D);
		MUL(mul[MUL_Q_X], MUL_D);
		STORE(d, MUL_D);
	}
	return (0);

#undef	MUL_D
}

static raidz_inline void
syndrome_q_abd(void **c, const void *dc, const size_t csize,
	const size_t dsize)
{
#define	SYN_Q_D	0, 1, 2, 3
#define	SYN_Q_X	4, 5, 6, 7

	size_t i;
	elem_t *x = (elem_t *) c[0];
	const elem_t *d = (elem_t *) dc;

	MUL2_SETUP();

	for (i = 0; i < dsize / sizeof (elem_t); i += 4,
		d += 4, x += 4) {
		PREFETCHNTA(d, 4, 4);

		LOAD(d, SYN_Q_D);
		Q_D_SYNDROME(SYN_Q_D, SYN_Q_X, x);
	}
	for (; i < csize / sizeof (elem_t); i += 4,
		x += 4) {
		PREFETCHNTA(x, 4, 4);
		Q_SYNDROME(SYN_Q_X, x);
	}

#undef	SYN_Q_D
#undef	SYN_Q_X
}

static raidz_inline void
syndrome_r_abd(void **c, const void *dc, const size_t csize,
	const size_t dsize)
{
#define	SYN_R_D	0, 1, 2, 3
#define	SYN_R_X	4, 5, 6, 7

	size_t i;
	elem_t *x = (elem_t *) c[0];
	elem_t *d = (elem_t *) dc;

	MUL2_SETUP();

	for (i = 0; i < dsize / sizeof (elem_t); i += 4,
		d += 4, x += 4) {

		PREFETCHNTA(d, 4, 4);

		LOAD(d, SYN_R_D);
		R_D_SYNDROME(SYN_R_D, SYN_R_X, x);
	}
	for (; i < csize / sizeof (elem_t); i += 4,
		x += 4) {
		PREFETCHNTA(x, 4, 4);
		R_SYNDROME(SYN_R_X, x);
	}

#undef	SYN_R_D
#undef	SYN_R_X
}

static raidz_inline void
syndrome_pq_abd(void **c, const void *dc, const size_t tsize,
	const size_t dsize)
{
#define	SYN_PQ_D	0, 1, 2, 3
#define	SYN_PQ_T	4, 5, 6, 7

	size_t i;
	elem_t *x = (elem_t *) c[0];
	elem_t *y = (elem_t *) c[1];
	const elem_t *d = (elem_t *) dc;

	MUL2_SETUP();

	for (i = 0; i < dsize / sizeof (elem_t); i += 4,
		x += 4, y += 4, d += 4) {

		PREFETCHNTA(d, 4, 4);

		LOAD(d, SYN_PQ_D);
		P_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, x);
		Q_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, y);
	}
	for (; i < tsize / sizeof (elem_t); i += 4,
		y += 4) {

		PREFETCHNTA(y, 4, 4);
		Q_SYNDROME(SYN_PQ_T, y);
	}

#undef	SYN_PQ_D
#undef	SYN_PQ_T
}


static void
raidz_rec_pq_abd(void **t, const size_t tsize, void **c,
	const unsigned *mul)
{
#define	REC_PQ_X	0, 1
#define	REC_PQ_Y	2, 3
#define	REC_PQ_T	4, 5

	size_t i;
	elem_t *x = (elem_t *) t[0];
	elem_t *y = (elem_t *) t[1];
	const elem_t *p = (elem_t *) c[CODE_P];
	const elem_t *q = (elem_t *) c[CODE_Q];

	for (i = 0; i < tsize / (sizeof (elem_t)); i += 2,
		x += 2, y += 2, p += 2, q += 2) {
		PREFETCHNTA(p, 4, 4);
		PREFETCHNTA(q, 4, 4);

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

static raidz_inline void
syndrome_pr_abd(void **c, const void *dc, const size_t tsize,
	const size_t dsize)
{
#define	SYN_PQ_D	0, 1, 2, 3
#define	SYN_PQ_T	4, 5, 6, 7

	size_t i;
	elem_t *x = (elem_t *) c[0];
	elem_t *y = (elem_t *) c[1];
	const elem_t *d = (elem_t *) dc;

	MUL2_SETUP();

	for (i = 0; i < dsize / sizeof (elem_t); i += 4,
		x += 4, y += 4, d += 4) {
		PREFETCHNTA(d, 4, 4);

		LOAD(d, SYN_PQ_D);
		P_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, x);
		R_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, y);
	}
	for (; i < tsize / sizeof (elem_t); i += 4,
		y += 4) {
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

	size_t i;
	elem_t *x = (elem_t *) t[0];
	elem_t *y = (elem_t *) t[1];
	const elem_t *p = (elem_t *) c[CODE_P];
	const elem_t *q = (elem_t *) c[CODE_Q];

	for (i = 0; i < tsize / (sizeof (elem_t)); i += 2,
		x += 2, y += 2, p += 2, q += 2) {
		PREFETCHNTA(p, 4, 4);
		PREFETCHNTA(q, 4, 4);

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

static raidz_inline void
syndrome_qr_abd(void **c, const void *dc, const size_t tsize,
	const size_t dsize)
{
#define	SYN_PQ_D	0, 1, 2, 3
#define	SYN_PQ_T	4, 5, 6, 7

	size_t i;
	elem_t *x = (elem_t *) c[0];
	elem_t *y = (elem_t *) c[1];
	const elem_t *d = (elem_t *) dc;

	MUL2_SETUP();

	for (i = 0; i < dsize / sizeof (elem_t); i += 4,
		x += 4, y += 4, d += 4) {
		PREFETCHNTA(d, 4, 4);

		LOAD(d, SYN_PQ_D);
		Q_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, x);
		R_D_SYNDROME(SYN_PQ_D, SYN_PQ_T, y);
	}
	for (; i < tsize / sizeof (elem_t); i += 4,
		x += 4, y += 4) {
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

	size_t i;
	elem_t *x = (elem_t *) t[0];
	elem_t *y = (elem_t *) t[1];
	const elem_t *p = (elem_t *) c[CODE_P];
	const elem_t *q = (elem_t *) c[CODE_Q];

	for (i = 0; i < tsize / (sizeof (elem_t)); i += 2,
		x += 2, y += 2, p += 2, q += 2) {
		PREFETCHNTA(p, 4, 4);
		PREFETCHNTA(q, 4, 4);

		LOAD(x, REC_QR_X);
		LOAD(y, REC_QR_Y);

		XOR_ACC(p, REC_QR_X);
		XOR_ACC(q, REC_QR_Y);

		/* Save Pxy */
		COPY(REC_QR_X,  REC_QR_T);

		/* Calc X */
		MULx2(mul[MUL_QR_XQ], REC_QR_X); /* X = Q * xqm */
		XOR(REC_QR_Y, REC_QR_X);		 /* X = R ^ X   */
		MULx2(mul[MUL_QR_X], REC_QR_X);	 /* X = X * xm  */
		STORE(x, REC_QR_X);

		/* Calc Y */
		MULx2(mul[MUL_QR_YQ], REC_QR_T); /* X = Q * xqm */
		XOR(REC_QR_Y, REC_QR_T);		 /* X = R ^ X   */
		MULx2(mul[MUL_QR_Y], REC_QR_T);	 /* X = X * xm  */
		STORE(y, REC_QR_T);
	}

#undef	REC_QR_X
#undef	REC_QR_Y
#undef	REC_QR_T
}


static void
syndrome_pqr_abd(void **c, const void *dc, const size_t tsize,
	const size_t dsize)
{
#define	SYN_PQR_D	0, 1, 2, 3
#define	SYN_PQR_T	4, 5, 6, 7

	size_t i;
	elem_t *x = (elem_t *) c[0];
	elem_t *y = (elem_t *) c[1];
	elem_t *z = (elem_t *) c[2];
	const elem_t *d = (elem_t *) dc;

	MUL2_SETUP();

	for (i = 0; i < dsize / sizeof (elem_t); i += 4,
		x += 4, y += 4, z += 4, d += 4) {
		PREFETCHNTA(d, 4, 4);

		LOAD(d, SYN_PQR_D);
		P_D_SYNDROME(SYN_PQR_D, SYN_PQR_T, x)
		Q_D_SYNDROME(SYN_PQR_D, SYN_PQR_T, y);
		R_D_SYNDROME(SYN_PQR_D, SYN_PQR_T, z);
	}
	for (; i < tsize / sizeof (elem_t); i += 4,
		y += 4, z += 4) {
		Q_SYNDROME(SYN_PQR_T, y);
		R_SYNDROME(SYN_PQR_T, z);
	}

#undef	SYN_PQR_D
#undef	SYN_PQR_T
}

static void
raidz_rec_pqr_abd(void **t, const size_t tsize, void **c,
	const unsigned *mul)
{
#define	REC_PQR_X	0, 1
#define	REC_PQR_Y	2, 3
#define	REC_PQR_Z	4, 5
#define	REC_PQR_XS	6, 7
#define	REC_PQR_YS	8, 9

	size_t i;
	elem_t *x = (elem_t *) t[0];
	elem_t *y = (elem_t *) t[1];
	elem_t *z = (elem_t *) t[2];
	const elem_t *p = (elem_t *) c[CODE_P];
	const elem_t *q = (elem_t *) c[CODE_Q];
	const elem_t *r = (elem_t *) c[CODE_R];

	for (i = 0; i < tsize / (sizeof (elem_t)); i += 2,
		x += 2, y += 2, z += 2, p += 2, q += 2, r += 2) {
		PREFETCHNTA(p, 4, 4);
		PREFETCHNTA(q, 4, 4);
		PREFETCHNTA(r, 4, 4);

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
		MULx2(mul[MUL_PQR_XP], REC_PQR_X);	/* Xp = Pxyz * xp   */
		MULx2(mul[MUL_PQR_XQ], REC_PQR_Y);	/* Xq = Qxyz * xq   */
		XOR(REC_PQR_Y, REC_PQR_X);
		MULx2(mul[MUL_PQR_XR], REC_PQR_Z);	/* Xr = Rxyz * xr   */
		XOR(REC_PQR_Z, REC_PQR_X);		/* X = Xp + Xq + Xr */
		STORE(x, REC_PQR_X);

		/* Calc Y */
		XOR(REC_PQR_X, REC_PQR_XS); 		/* Pyz = Pxyz + X */
		MULx2(mul[MUL_PQR_YU], REC_PQR_X);  /* Xq = X * upd_q */
		XOR(REC_PQR_X, REC_PQR_YS); 		/* Qyz = Qxyz + Xq */
		COPY(REC_PQR_XS, REC_PQR_X);		/* restore Pyz */
		MULx2(mul[MUL_PQR_YP], REC_PQR_X);	/* Yp = Pyz * yp */
		MULx2(mul[MUL_PQR_YQ], REC_PQR_YS);	/* Yq = Qyz * yq */
		XOR(REC_PQR_X, REC_PQR_YS); 	/* Y = Yp + Yq */
		STORE(y, REC_PQR_YS);

		/* Calc Z */
		XOR(REC_PQR_XS, REC_PQR_YS); /* Z = Pz = Pyz + Y */
		STORE(z, REC_PQR_YS);
	}

#undef	REC_PQR_X
#undef	REC_PQR_Y
#undef	REC_PQR_Z
#undef	REC_PQR_XS
#undef	REC_PQR_YS
}

#endif  /* _VDEV_RAIDZ_MATH_X86SIMD_H */
