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

#ifndef _VDEV_RAIDZ_MATH_IMPL_H
#define	_VDEV_RAIDZ_MATH_IMPL_H

#include <sys/types.h>


struct elem_t;
struct elem_mul_pad_t;

static raidz_inline void raidz_flush_data(void);

static raidz_inline size_t
gen_p_block(raidz_col_t *cols, const size_t nd, const size_t off);
static raidz_inline size_t
gen_pq_block(raidz_col_t *cols, const size_t nd, const size_t nld,
	const size_t off);
static raidz_inline size_t
gen_pqr_block(raidz_col_t *cols, const size_t nd, const size_t nld,
	const size_t off);

static raidz_inline size_t
rec_p_block(raidz_col_t *cols, const size_t parity, const size_t xidx,
	const size_t nd, const size_t off);
static raidz_inline size_t
rec_q_block(raidz_col_t *cols, const size_t parity,
	const size_t xidx, const int x_mul,
	const size_t nd, const size_t nld, const size_t off);
static raidz_inline size_t
rec_r_block(raidz_col_t *cols, const size_t parity,
	const size_t xidx, const int x_mul,
	const size_t nd, const size_t nld, const size_t off);

static raidz_inline size_t
rec_pq_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y,
	const size_t xsize, const size_t ysize,
	const int a_mul, const int b_mul,
	const size_t nd, const size_t nld, const size_t off);
static raidz_inline size_t
rec_pr_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y,
	const size_t xsize, const size_t ysize,
	const int a_mul, const int b_mul,
	const size_t nd, const size_t nld, const size_t off);
static raidz_inline size_t
rec_qr_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y,
	const size_t xsize, const size_t ysize,
	const int xq_mul, const int x_mul, const int yq_mul, const int y_mul,
	const size_t nd, const size_t nld, const size_t off);

static raidz_inline size_t
rec_pqr_block(raidz_col_t *cols, const size_t parity,
	const size_t x, const size_t y, const size_t z,
	const size_t xsize, const size_t ysize, const size_t zsize,
	const int xp_mul, const int xq_mul, const int xr_mul,
	const int yp_mul, const int yq_mul, const int upd_q_mul,
	const size_t nd, const size_t nld, const size_t off);

static raidz_inline void
raidz_flush_data(void);

static raidz_inline size_t
raidz_col_size(const raidz_map_t *rm, const size_t cidx)
{
	return (rm->rm_col[cidx].rc_size);
}

static raidz_inline void
raidz_generate_p_impl(raidz_map_t *rm)
{
	size_t off;
	const size_t ncols = raidz_ncols(rm);
	const size_t nbigcols = raidz_nbigcols(rm);
	const size_t ldsize = raidz_ldsize(rm);
	const size_t csize = raidz_csize(rm);

	for (off = 0; off < ldsize; ) {
		off += gen_p_block(rm->rm_col, ncols, off);
	}
	for (; off < csize; ) {
		off += gen_p_block(rm->rm_col, nbigcols, off);
	}

	raidz_flush_data();
}

static raidz_inline void
raidz_generate_pq_impl(raidz_map_t *rm)
{
	size_t off;
	const size_t ncols = raidz_ncols(rm);
	const size_t nbigcols = raidz_nbigcols(rm);
	const size_t ldsize = raidz_ldsize(rm);
	const size_t csize = raidz_csize(rm);

	for (off = 0; off < ldsize; ) {
		off += gen_pq_block(rm->rm_col, ncols, ncols, off);
	}
	for (; off < csize; ) {
		off += gen_pq_block(rm->rm_col, ncols, nbigcols, off);
	}

	raidz_flush_data();
}

static raidz_inline void
raidz_generate_pqr_impl(raidz_map_t *rm)
{
	size_t off;
	const size_t ncols = raidz_ncols(rm);
	const size_t nbigcols = raidz_nbigcols(rm);
	const size_t ldsize = raidz_ldsize(rm);
	const size_t csize = raidz_csize(rm);

	for (off = 0; off < ldsize; ) {
		off += gen_pqr_block(rm->rm_col, ncols, ncols, off);
	}
	for (; off < csize; ) {
		off += gen_pqr_block(rm->rm_col, ncols, nbigcols, off);
	}

	raidz_flush_data();
}

static raidz_inline int
raidz_reconstruct_p_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t off;
	const size_t parity = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t nbigcols = raidz_nbigcols(rm);
	const size_t ldsize = raidz_ldsize(rm);
	const size_t xidx = tgtidx[0];
	const size_t xsize = raidz_col_size(rm, xidx);

	for (off = 0; off < ldsize; ) {
		off += rec_p_block(rm->rm_col, parity, xidx, ncols, off);
	}
	for (; off < xsize; ) {
		off += rec_p_block(rm->rm_col, parity, xidx, nbigcols, off);
	}

	raidz_flush_data();

	return (1 << 0);
}

static raidz_inline int
raidz_reconstruct_q_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t off;
	const size_t parity = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t nbigcols = raidz_nbigcols(rm);
	const size_t ldsize = raidz_ldsize(rm);
	const size_t xidx = tgtidx[0];
	const size_t xsize = raidz_col_size(rm, xidx);

	const int tgtmul = fix_mul_exponent(255 - (ncols - xidx - 1));

	for (off = 0; off < ldsize; ) {
		off += rec_q_block(rm->rm_col, parity, xidx, tgtmul,
			ncols, ncols, off);
	}
	for (; off < xsize; ) {
		off += rec_q_block(rm->rm_col, parity, xidx, tgtmul,
			ncols, nbigcols, off);
	}

	raidz_flush_data();

	return (1 << 1);
}

static raidz_inline int
raidz_reconstruct_r_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t off;
	const size_t parity = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t nbigcols = raidz_nbigcols(rm);
	const size_t ldsize = raidz_ldsize(rm);
	const size_t xidx = tgtidx[0];
	const size_t xsize = raidz_col_size(rm, xidx);

	const int tgtmul = fix_mul_exponent(255 - 2 * (ncols - xidx - 1));

	for (off = 0; off < ldsize; ) {
		off += rec_r_block(rm->rm_col, parity, xidx,
			tgtmul, ncols, ncols, off);
	}
	for (; off < xsize; ) {
		off += rec_r_block(rm->rm_col, parity, xidx,
			tgtmul, ncols, nbigcols, off);
	}

	raidz_flush_data();

	return (1 << 2);
}

static raidz_inline int
raidz_reconstruct_pq_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t off;
	int tmp, aexp, bexp, a, b;

	const size_t parity = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t nbigcols = raidz_nbigcols(rm);
	const size_t ldsize = raidz_ldsize(rm);

	const size_t xidx = tgtidx[0];
	const size_t yidx = tgtidx[1];
	const size_t xsize = raidz_col_size(rm, xidx);
	const size_t ysize = raidz_col_size(rm, yidx);

	a = vdev_raidz_pow2[255 + xidx - yidx];
	b = vdev_raidz_pow2[255 - (ncols - 1 - xidx)];
	tmp = 255 - vdev_raidz_log2[a ^ 0x01];

	aexp = fix_mul_exponent(vdev_raidz_log2[vdev_raidz_exp2(a, tmp)]);
	bexp = fix_mul_exponent(vdev_raidz_log2[vdev_raidz_exp2(b, tmp)]);

	for (off = 0; off < ldsize; ) {
		off += rec_pq_block(rm->rm_col, parity, xidx, yidx,
			xsize, xsize, aexp, bexp, ncols, ncols, off);
	}
	for (; off < xsize; ) {
		off += rec_pq_block(rm->rm_col, parity, xidx, yidx,
			xsize, ysize, aexp, bexp, ncols, nbigcols, off);
	}

	raidz_flush_data();

	return ((1 << 0) | (1 << 1));
}

static raidz_inline int
raidz_reconstruct_pr_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t off;
	int tmp, aexp, bexp, a, b;

	const size_t parity = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t nbigcols = raidz_nbigcols(rm);
	const size_t ldsize = raidz_ldsize(rm);

	const size_t xidx = tgtidx[0];
	const size_t yidx = tgtidx[1];
	const size_t xsize = raidz_col_size(rm, xidx);
	const size_t ysize = raidz_col_size(rm, yidx);

	a = vdev_raidz_pow2[255 + 2 * xidx - 2 * yidx];
	b = vdev_raidz_pow2[255 - 2 * (ncols - 1 - xidx)];
	tmp = 255 - vdev_raidz_log2[a ^ 0x01];

	aexp = fix_mul_exponent(vdev_raidz_log2[vdev_raidz_exp2(a, tmp)]);
	bexp = fix_mul_exponent(vdev_raidz_log2[vdev_raidz_exp2(b, tmp)]);

	for (off = 0; off < ldsize; ) {
		off += rec_pr_block(rm->rm_col, parity, xidx, yidx,
			xsize, xsize, aexp, bexp, ncols, ncols, off);
	}
	for (; off < xsize; ) {
		off += rec_pr_block(rm->rm_col, parity, xidx, yidx,
			xsize, ysize, aexp, bexp, ncols, nbigcols, off);
	}

	raidz_flush_data();

	return ((1 << 0) | (1 << 2));
}

static raidz_inline int
raidz_reconstruct_qr_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t off;
	int xqmul_exp, xmul_exp, yqmul_exp, ymul_exp, denom;

	const size_t parity = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t nbigcols = raidz_nbigcols(rm);
	const size_t ldsize = raidz_ldsize(rm);

	const size_t xidx = tgtidx[0];
	const size_t yidx = tgtidx[1];
	const size_t xsize = raidz_col_size(rm, xidx);
	const size_t ysize = raidz_col_size(rm, yidx);

	denom = 255 - vdev_raidz_log2[
			vdev_raidz_pow2[3 * ncols - 3 - xidx - 2 * yidx] ^
			vdev_raidz_pow2[3 * ncols - 3 - 2 * xidx - yidx]
		];

	xqmul_exp = fix_mul_exponent(ncols - 1 - yidx);
	xmul_exp = fix_mul_exponent(ncols - 1 - yidx + denom);

	yqmul_exp = fix_mul_exponent(ncols - 1 - xidx);
	ymul_exp = fix_mul_exponent(ncols - 1 - xidx + denom);

	for (off = 0; off < ldsize; ) {
		off += rec_qr_block(rm->rm_col, parity, xidx, yidx,
			xsize, xsize, xqmul_exp, xmul_exp, yqmul_exp, ymul_exp,
			ncols, ncols, off);
	}
	for (; off < xsize; ) {
		off += rec_qr_block(rm->rm_col, parity, xidx, yidx,
			xsize, ysize, xqmul_exp, xmul_exp, yqmul_exp, ymul_exp,
			ncols, nbigcols, off);
	}

	raidz_flush_data();

	return ((1 << 1) | (1 << 2));
}

static raidz_inline int
raidz_reconstruct_pqr_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t off;
	int xp_exp, xq_exp, xr_exp, yp_exp, yq_exp, yu_exp, xdenom, ydenom;

	const size_t parity = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t nbigcols = raidz_nbigcols(rm);
	const size_t ldsize = raidz_ldsize(rm);

	const size_t xidx = tgtidx[0];
	const size_t yidx = tgtidx[1];
	const size_t zidx = tgtidx[2];
	const size_t xsize = raidz_col_size(rm, xidx);
	const size_t ysize = raidz_col_size(rm, yidx);
	const size_t zsize = raidz_col_size(rm, zidx);

	xdenom = 255 - vdev_raidz_log2[
			vdev_raidz_pow2[3 * ncols - 3 - 2 * xidx - yidx] ^
			vdev_raidz_pow2[3 * ncols - 3 - xidx - 2 * yidx] ^
			vdev_raidz_pow2[3 * ncols - 3 - 2 * xidx - zidx] ^
			vdev_raidz_pow2[3 * ncols - 3 - xidx - 2 * zidx] ^
			vdev_raidz_pow2[3 * ncols - 3 - 2 * yidx - zidx] ^
			vdev_raidz_pow2[3 * ncols - 3 - yidx - 2 * zidx]
		];

	xp_exp = fix_mul_exponent(vdev_raidz_log2[
			vdev_raidz_pow2[3 * ncols - 3 - 2 * yidx - zidx] ^
			vdev_raidz_pow2[3 * ncols - 3 - yidx - 2 * zidx]
		] + xdenom);
	xq_exp = fix_mul_exponent(vdev_raidz_log2[
			vdev_raidz_pow2[2 * ncols - 2 - 2 * yidx] ^
			vdev_raidz_pow2[2 * ncols - 2 - 2 * zidx]
		] + xdenom);
	xr_exp = fix_mul_exponent(vdev_raidz_log2[
			vdev_raidz_pow2[ncols - 1 - yidx] ^
			vdev_raidz_pow2[ncols - 1 - zidx]
		] + xdenom);

	ydenom = 255 - vdev_raidz_log2[
					vdev_raidz_pow2[ncols - 1 - yidx] ^
					vdev_raidz_pow2[ncols - 1 - zidx]
				];

	yp_exp = fix_mul_exponent(ncols - 1 - zidx + ydenom);
	yq_exp = fix_mul_exponent(ydenom);
	yu_exp = fix_mul_exponent(ncols - 1 - xidx);

	for (off = 0; off < ldsize; ) {
		off += rec_pqr_block(rm->rm_col, parity, xidx, yidx, zidx,
			xsize, xsize, xsize, xp_exp, xq_exp, xr_exp,
			yp_exp, yq_exp, yu_exp, ncols, ncols, off);
	}
	for (; off < xsize; ) {
		off += rec_pqr_block(rm->rm_col, parity, xidx, yidx, zidx,
			xsize, ysize, zsize, xp_exp, xq_exp, xr_exp,
			yp_exp, yq_exp, yu_exp, ncols, nbigcols, off);
	}

	raidz_flush_data();

	return ((1 << 0) | (1 << 1) | (1 << 2));
}

#endif /* _VDEV_RAIDZ_MATH_IMPL_H */
