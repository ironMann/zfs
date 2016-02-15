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

#define	D(idx) ((elem_t *)(((uint8_t *)(cols[idx].rc_data)) + off))

static raidz_inline void
raidz_flush_data(void)
{
	FLUSH();
}

static raidz_inline size_t
gen_p_block(raidz_col_t *cols, const size_t nd, const size_t off)
{
	size_t c;

#define	G_P_P 0, 1, 2, 3

	PREFETCHNTA(D(1), 4, 4);

	LOAD(D(CODE_P+1), G_P_P);
	for (c = CODE_P+2; c < nd; c++) {
		PREFETCHNTA(D(c), 4, 4);

		XOR_ACC(D(c), G_P_P);
	}

	STREAM_STORE(D(CODE_P), G_P_P);
	return (4 * sizeof (elem_t));
}

static raidz_inline size_t
gen_pq_block(raidz_col_t *cols, const size_t nd, const size_t nld,
	const size_t off)
{
	size_t c;

#define	G_PQ_P 0, 1, 2, 3
#define	G_PQ_Q 4, 5, 6, 7
#define	G_PQ_LOAD 8, 9, 10, 11

	PREFETCHNTA(D(2), 4, 4);

	MUL2_SETUP();

	LOAD(D(CODE_Q+1), G_PQ_P);
	COPY(G_PQ_P, G_PQ_Q);

	for (c = CODE_Q+2; c < nld; c++) {
		PREFETCHNTA(D(c), 4, 4);
		MUL2(G_PQ_Q);

		LOAD(D(c), G_PQ_LOAD);
		XOR(G_PQ_LOAD, G_PQ_P);
		XOR(G_PQ_LOAD, G_PQ_Q);
	}
	STREAM_STORE(D(CODE_P), G_PQ_P);

	for (; c < nd; c++) {
		MUL2(G_PQ_Q);
	}
	STREAM_STORE(D(CODE_Q), G_PQ_Q);

	return (4 * sizeof (elem_t));
}

static raidz_inline size_t
gen_pqr_block(raidz_col_t *cols, const size_t nd, const size_t nld,
	const size_t off)
{
	size_t c;

#define	G_PQR_P 0, 1, 2, 3
#define	G_PQR_Q 4, 5, 6, 7
#define	G_PQR_R 8, 9, 10, 11
#define	G_PQR_LOAD 12, 13

#define	G_PQR_P_1 0, 1
#define	G_PQR_P_2 2, 3
#define	G_PQR_Q_1 4, 5
#define	G_PQR_Q_2 6, 7
#define	G_PQR_R_1 8, 9
#define	G_PQR_R_2 10, 11

	PREFETCHNTA(D(3), 4, 4);

	MUL2_SETUP();

	LOAD(D(CODE_R+1), G_PQR_P);
	COPY(G_PQR_P,  G_PQR_Q);
	COPY(G_PQR_P,  G_PQR_R);

	for (c = CODE_R+2; c < nld; c++) {
		PREFETCHNTA(D(c), 4, 4);

		MUL2(G_PQR_Q);
		MUL4(G_PQR_R);

		LOAD(D(c), G_PQR_LOAD);
		XOR(G_PQR_LOAD,  G_PQR_P_1);
		XOR(G_PQR_LOAD,  G_PQR_Q_1);
		XOR(G_PQR_LOAD,  G_PQR_R_1);

		LOAD(D(c) + 2, G_PQR_LOAD);
		XOR(G_PQR_LOAD,  G_PQR_P_2);
		XOR(G_PQR_LOAD,  G_PQR_Q_2);
		XOR(G_PQR_LOAD,  G_PQR_R_2);
	}
	STREAM_STORE(D(CODE_P), G_PQR_P);

	for (; c < nd; c++) {
		MUL2(G_PQR_Q);
		MUL4(G_PQR_R);
	}
	STREAM_STORE(D(CODE_Q), G_PQR_Q);
	STREAM_STORE(D(CODE_R), G_PQR_R);

	return (4 * sizeof (elem_t));
}

static raidz_inline size_t
rec_p_block(raidz_col_t *cols, const size_t parity,
	const size_t xidx, const size_t nd, const size_t off)
{
	size_t c;

#define	R_P_P 0, 1, 2, 3

	PREFETCHNTA(D(CODE_P), 0, 4);

	ZERO(R_P_P);

	for (c = parity; c < nd; c++) {
		PREFETCHNTA(D(c), 4, 4);

		if (c != xidx) {
			XOR_ACC(D(c), R_P_P);
		}
	}
	XOR_ACC(D(CODE_P), R_P_P);
	/* Calc X (X==P) */
	STREAM_STORE(D(xidx), R_P_P);

	return (4 * sizeof (elem_t));
}

static raidz_inline size_t
rec_q_block(raidz_col_t *cols, const size_t parity,
	const size_t xidx, const int x_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;

#define	R_Q_Q 0, 1

	PREFETCHNTA(D(CODE_Q), 0, 2);

	MUL2_SETUP();
	ZERO(R_Q_Q);

	for (c = parity; c < nld; c++) {
		PREFETCHNTA(D(c), 2, 2);

		MUL2(R_Q_Q);

		if (c != xidx) {
			XOR_ACC(D(c), R_Q_Q);
		}
	}

	for (; c < nd; c++) {
		MUL2(R_Q_Q);
	}
	XOR_ACC(D(CODE_Q), R_Q_Q);

	/* Calc X */
	MULx2(x_mul, R_Q_Q);
	STREAM_STORE(D(xidx), R_Q_Q);

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_r_block(raidz_col_t *cols, const size_t parity,
	const size_t xidx, const int x_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;

#define	R_R_R 0, 1

	PREFETCHNTA(D(CODE_R), 0, 2);

	MUL2_SETUP();
	ZERO(R_R_R);

	for (c = parity; c < nld; c++) {
		PREFETCHNTA(D(c), 2, 2);

		MUL4(R_R_R);

		if (c != xidx) {
			XOR_ACC(D(c), R_R_R);
		}
	}

	for (; c < nd; c++) {
		MUL4(R_R_R);
	}
	XOR_ACC(D(CODE_R), R_R_R);

	/* Calc X */
	MULx2(x_mul, R_R_R);
	STREAM_STORE(D(xidx), R_R_R);

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_pq_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y,
	const size_t xsize, const size_t ysize,
	const int a_mul, const int b_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;

	PREFETCHNTA(D(CODE_P), 0, 2);
	PREFETCHNTA(D(CODE_Q), 0, 2);

#define	R_PQ_P 0, 1
#define	R_PQ_Q 2, 3
#define	R_PQ_LOAD 	4, 5
#define	R_PQ_P_SAVE 4, 5

	MUL2_SETUP();
	ZERO(R_PQ_P);
	ZERO(R_PQ_Q);

	for (c = parity; c < nld; c++) {
		PREFETCHNTA(D(c), 2, 2);

		MUL2(R_PQ_Q);

		if (c != x && c != y) {
			LOAD(D(c), R_PQ_LOAD);
			XOR(R_PQ_LOAD, R_PQ_P);
			XOR(R_PQ_LOAD, R_PQ_Q);
		}
	}

	for (; c < nd; c++) {
		MUL2(R_PQ_Q);
	}
	XOR_ACC(D(CODE_P), R_PQ_P);
	XOR_ACC(D(CODE_Q), R_PQ_Q);

	/* Save Pxy */
	COPY(R_PQ_P,  R_PQ_P_SAVE);

	/* Calc X */
	MULx2(a_mul, R_PQ_P);
	MULx2(b_mul, R_PQ_Q);

	XOR(R_PQ_Q,  R_PQ_P);
	STREAM_STORE(D(x), R_PQ_P);

	if (xsize == ysize) {
		/* Calc Y */
		XOR(R_PQ_P_SAVE,  R_PQ_P);
		STREAM_STORE(D(y), R_PQ_P);
	}

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_pr_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y,
	const size_t xsize, const size_t ysize,
	const int a_mul, const int b_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;

#define	R_PR_P 0, 1
#define	R_PR_R 2, 3
#define	R_PR_LOAD	4, 5
#define	R_PR_P_SAVE 4, 5

	PREFETCHNTA(D(CODE_P), 0, 2);
	PREFETCHNTA(D(CODE_R), 0, 2);

	MUL2_SETUP();
	ZERO(R_PR_P);
	ZERO(R_PR_R);

	for (c = parity; c < nld; c++) {
		PREFETCHNTA(D(c), 2, 2);
		MUL4(R_PR_R);
		if (c != x && c != y) {
			LOAD(D(c), R_PR_LOAD);
			XOR(R_PR_LOAD, R_PR_P);
			XOR(R_PR_LOAD, R_PR_R);
		}
	}

	for (; c < nd; c++) {
		MUL4(R_PR_R);
	}
	XOR_ACC(D(CODE_P), R_PR_P);
	XOR_ACC(D(CODE_R), R_PR_R);

	/* Save Pxy */
	COPY(R_PR_P, R_PR_P_SAVE);

	/* Calc X */
	MULx2(a_mul, R_PR_P);
	MULx2(b_mul, R_PR_R);

	XOR(R_PR_R,  R_PR_P);
	STREAM_STORE(D(x), R_PR_P);

	if (xsize == ysize) {
		/* Calc Y */
		XOR(R_PR_P_SAVE, R_PR_P);
		STREAM_STORE(D(y), R_PR_P);
	}

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_qr_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y,
	const size_t xsize, const size_t ysize,
	const int xq_mul, const int x_mul,
	const int yq_mul, const int y_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;

#define	R_QR_P		0, 1
#define	R_QR_R		2, 3
#define	R_QR_LOAD	4, 5
#define	R_QR_P_SAVE 4, 5

	PREFETCHNTA(D(CODE_Q), 0, 2);
	PREFETCHNTA(D(CODE_R), 0, 2);

	MUL2_SETUP();
	ZERO(R_QR_P);
	ZERO(R_QR_R);

	for (c = parity; c < nld; c++) {
		PREFETCHNTA(D(c), 2, 2);

		MUL2(R_QR_P);
		MUL4(R_QR_R);

		if (c != x && c != y) {
			LOAD(D(c), R_QR_LOAD);
			XOR(R_QR_LOAD, R_QR_P);
			XOR(R_QR_LOAD, R_QR_R);
		}
	}

	for (; c < nd; c++) {
		MUL2(R_QR_P);
		MUL4(R_QR_R);
	}
	XOR_ACC(D(CODE_Q), R_QR_P);
	XOR_ACC(D(CODE_R), R_QR_R);

	/* Save Pxy */
	COPY(R_QR_P, R_QR_P_SAVE);

	/* Calc X */
	MULx2(xq_mul, R_QR_P); /* X = Q * xqm */
	XOR(R_QR_R, R_QR_P);   /* X = R ^ X   */
	MULx2(x_mul, R_QR_P);  /* X = X * xm */
	STREAM_STORE(D(x), R_QR_P);

	if (xsize == ysize) {
		/* Calc Y */
		MULx2(yq_mul, R_QR_P_SAVE);  /* X = Q * xqm */

		XOR(R_QR_R, R_QR_P_SAVE); /* X = R ^ X   */

		MULx2(y_mul, R_QR_P_SAVE);  /* X = X * xm */
		STREAM_STORE(D(y), R_QR_P_SAVE);
	}

	return (2 * sizeof (elem_t));
}

static raidz_inline size_t
rec_pqr_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y, const size_t z,
	const size_t xsize, const size_t ysize, const size_t zsize,
	const int xp_mul, const int xq_mul, const int xr_mul,
	const int yp_mul, const int yq_mul, const int upd_q_mul,
	const size_t nd, const size_t nld, const size_t off)
{
	size_t c;

#define	R_PQR_P 0, 1
#define	R_PQR_Q 2, 3
#define	R_PQR_R 4, 5
#define	R_PQR_LOAD 6, 7
#define	R_PQR_P_SAVE 6, 7
#define	R_PQR_Q_SAVE 8, 9

	PREFETCHNTA(D(CODE_P), 0, 2);
	PREFETCHNTA(D(CODE_Q), 0, 2);
	PREFETCHNTA(D(CODE_R), 0, 2);

	MUL2_SETUP();
	ZERO(R_PQR_P);
	ZERO(R_PQR_Q);
	ZERO(R_PQR_R);

	for (c = parity; c < nld; c++) {
		PREFETCHNTA(D(c), 2, 2);

		MUL2(R_PQR_Q);
		MUL4(R_PQR_R);

		if (c != x && c != y && c != z) {
			LOAD(D(c), R_PQR_LOAD);
			XOR(R_PQR_LOAD, R_PQR_P);
			XOR(R_PQR_LOAD, R_PQR_Q);
			XOR(R_PQR_LOAD, R_PQR_R);
		}
	}

	for (; c < nd; c++) {
		MUL2(R_PQR_Q);
		MUL4(R_PQR_R);
	}

	XOR_ACC(D(CODE_P), R_PQR_P);
	XOR_ACC(D(CODE_Q), R_PQR_Q);
	XOR_ACC(D(CODE_R), R_PQR_R);

	/* Save Pxyz and Qxyz */
	COPY(R_PQR_P, R_PQR_P_SAVE);
	COPY(R_PQR_Q, R_PQR_Q_SAVE);

	/* Calc X */
	MULx2(xp_mul, R_PQR_P); /* Xp = Pxyz * xp */
	MULx2(xq_mul, R_PQR_Q); /* Xq = Qxyz * xq */
	XOR(R_PQR_Q, R_PQR_P);
	MULx2(xr_mul, R_PQR_R); /* Xr = Rxyz * xr */
	XOR(R_PQR_R, R_PQR_P);  /* X = Xp + Xq + Xr */

	STREAM_STORE(D(x), R_PQR_P);

	if (ysize == xsize) {

		/* Calc Y */
		XOR(R_PQR_P, R_PQR_P_SAVE); /* Pyz = Pxyz + X */

		MULx2(upd_q_mul, R_PQR_P);  /* Xq = X * upd_q */
		XOR(R_PQR_P, R_PQR_Q_SAVE); /* Qyz = Qxyz + Xq */

		COPY(R_PQR_P_SAVE, R_PQR_P);	/* restore Pyz */
		MULx2(yp_mul, R_PQR_P);			/* Yp = Pyz * yp */

		MULx2(yq_mul, R_PQR_Q_SAVE);	/* Yq = Qyz * yq */
		XOR(R_PQR_P, R_PQR_Q_SAVE); 	/* Y = Yp + Yq */

		STREAM_STORE(D(y), R_PQR_Q_SAVE);

		if (zsize == xsize) {
			/* Calc Z */
			XOR(R_PQR_P_SAVE, R_PQR_Q_SAVE); /* Z = Pz = Pyz + Y */
			STREAM_STORE(D(z), R_PQR_Q_SAVE);
		}
	}

	return (2 * sizeof (elem_t));
}

#endif  /* _VDEV_RAIDZ_MATH_X86SIMD_H */
