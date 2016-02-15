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

static raidz_inline void raidz_math_begin(void);
static raidz_inline void raidz_math_end(void);

static raidz_inline void
raidz_generate_p_impl_abd(raidz_map_t * const rm)
{
	size_t c;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t psize = rm->rm_col[CODE_P].rc_size;
	abd_t *pabd = rm->rm_col[CODE_P].rc_data;
	size_t dsize;
	abd_t *dabd;

	ASSERT3U(psize, ==, rm->rm_col[firstdc].rc_size);

	/* start with first data column */
	abd_copy(pabd, rm->rm_col[firstdc].rc_data, psize);

	raidz_math_begin();

	for (c = firstdc+1; c < ncols; c++) {
		dabd = rm->rm_col[c].rc_data;
		dsize = rm->rm_col[c].rc_size;

		/* add data column */
		abd_iterate_func2(pabd, dabd, dsize, dsize,
			raidz_add_abd, NULL);
	}

	raidz_math_end();
}

static raidz_inline void
raidz_generate_pq_impl_abd(raidz_map_t * const rm)
{
	size_t c;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t csize = rm->rm_col[CODE_P].rc_size;
	size_t dsize;
	abd_t *dabd;
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_data,
		rm->rm_col[CODE_Q].rc_data
	};

	abd_copy(cabds[CODE_P], rm->rm_col[firstdc].rc_data, csize);
	abd_copy(cabds[CODE_Q], rm->rm_col[firstdc].rc_data, csize);

	raidz_math_begin();

	for (c = firstdc+1; c < ncols; c++) {
		dabd = rm->rm_col[c].rc_data;
		dsize = rm->rm_col[c].rc_size;

		abd_raidz_gen_iterate(cabds, dabd, csize, dsize, 2,
			raidz_gen_pq_add);
	}

	raidz_math_end();
}

static raidz_inline void
raidz_generate_pqr_impl_abd(raidz_map_t * const rm)
{
	size_t c;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t csize = rm->rm_col[CODE_P].rc_size;
	size_t dsize;
	abd_t *dabd;
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_data,
		rm->rm_col[CODE_Q].rc_data,
		rm->rm_col[CODE_R].rc_data
	};

	abd_copy(cabds[CODE_P], rm->rm_col[firstdc].rc_data, csize);
	abd_copy(cabds[CODE_Q], rm->rm_col[firstdc].rc_data, csize);
	abd_copy(cabds[CODE_R], rm->rm_col[firstdc].rc_data, csize);

	raidz_math_begin();

	for (c = firstdc+1; c < ncols; c++) {
		dabd = rm->rm_col[c].rc_data;
		dsize = rm->rm_col[c].rc_size;

		abd_raidz_gen_iterate(cabds, dabd, csize, dsize, 3,
			raidz_gen_pqr_add);
	}

	raidz_math_end();
}


static raidz_inline int
raidz_reconstruct_p_impl_abd(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t xsize = rm->rm_col[x].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	size_t size;
	abd_t *dabd;

	/* copy P into target */
	abd_copy(xabd, rm->rm_col[CODE_P].rc_data, xsize);

	raidz_math_begin();

	/* generate p_syndrome */
	for (c = firstdc; c < ncols; c++) {
		if (c == x)
			continue;

		dabd = rm->rm_col[c].rc_data;
		size = MIN(rm->rm_col[c].rc_size, xsize);

		abd_iterate_func2(xabd, dabd, size, size, raidz_add_abd, NULL);
	}
	raidz_math_end();

	return (1 << 0);
}


static raidz_inline int
raidz_reconstruct_q_impl_abd(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	abd_t *xabd = rm->rm_col[x].rc_data;
	const size_t xsize = rm->rm_col[x].rc_size;
	abd_t *tabds[] = { xabd };
	const unsigned mul[] = {
	[MUL_Q_X] = fix_mul_exp(255 - (ncols - x - 1))
	};

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 1,
			raidz_syndrome_q_abd);
	}

	/* add Q to the syndrome */
	abd_iterate_func2(xabd, rm->rm_col[CODE_Q].rc_data, xsize, xsize,
		raidz_add_abd, NULL);

	/* transform the syndrome */
	abd_iterate_wfunc(xabd, xsize, raidz_mul_abd, (void*)mul);

	raidz_math_end();

	return (1 << 1);
}


static raidz_inline int
raidz_reconstruct_r_impl_abd(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t xsize = rm->rm_col[x].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	abd_t *tabds[] = { xabd };
	const unsigned mul[] = {
	[MUL_R_X] = fix_mul_exp(255 - 2 * (ncols - x - 1))
	};

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}
		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 1,
			raidz_syndrome_r_abd);
	}

	/* add R to the syndrome */
	abd_iterate_func2(xabd, rm->rm_col[CODE_R].rc_data, xsize, xsize,
		raidz_add_abd, NULL);

	/* transform the syndrome */
	abd_iterate_wfunc(xabd, xsize, raidz_mul_abd, (void *)mul);

	raidz_math_end();

	return (1 << 2);
}

static raidz_inline int
raidz_reconstruct_pq_impl_abd(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t y = tgtidx[1];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	abd_t *yabd = rm->rm_col[y].rc_data;
	abd_t *tabds[2] = { xabd, yabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_data,
		rm->rm_col[CODE_Q].rc_data
	};
	const unsigned a = vdev_raidz_pow2[255 + x - y];
	const unsigned b = vdev_raidz_pow2[255 - (ncols - 1 - x)];
	const unsigned e = 255 - vdev_raidz_log2[a ^ 0x01];
	const unsigned mul[] = {
	[MUL_PQ_X] = fix_mul_exp(vdev_raidz_log2[vdev_raidz_exp2(a, e)]),
	[MUL_PQ_Y] = fix_mul_exp(vdev_raidz_log2[vdev_raidz_exp2(b, e)])
	};

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc_scatter(xsize);
		VERIFY(yabd);
		tabds[1] = yabd;
	}

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
		abd_copy(yabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
		abd_zero(yabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 2,
			raidz_syndrome_pq_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 2, raidz_rec_pq_abd, mul);

	raidz_math_end();

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize) {
		abd_copy(rm->rm_col[y].rc_data, yabd, ysize);
		abd_free(yabd, xsize);
	}

	return ((1 << 0) | (1 << 1));
}

static raidz_inline int
raidz_reconstruct_pr_impl_abd(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t y = tgtidx[1];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	abd_t *yabd = rm->rm_col[y].rc_data;
	abd_t *tabds[2] = { xabd, yabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_data,
		rm->rm_col[CODE_R].rc_data
	};
	const unsigned a = vdev_raidz_pow2[255 + 2 * x - 2 * y];
	const unsigned b = vdev_raidz_pow2[255 - 2 * (ncols - 1 - x)];
	const unsigned e = 255 - vdev_raidz_log2[a ^ 0x01];
	const unsigned mul[] = {
	[MUL_PR_X] = fix_mul_exp(vdev_raidz_log2[vdev_raidz_exp2(a, e)]),
	[MUL_PR_Y] = fix_mul_exp(vdev_raidz_log2[vdev_raidz_exp2(b, e)])
	};

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc_scatter(xsize);
		VERIFY(yabd);
		tabds[1] = yabd;
	}

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
		abd_copy(yabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
		abd_zero(yabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 2,
			raidz_syndrome_pr_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 2, raidz_rec_pr_abd, mul);

	raidz_math_end();

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize) {
		abd_copy(rm->rm_col[y].rc_data, yabd, ysize);
		abd_free(yabd, xsize);
	}

	return ((1 << 0) | (1 << 2));
}



static raidz_inline int
raidz_reconstruct_qr_impl_abd(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t y = tgtidx[1];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	abd_t *yabd = rm->rm_col[y].rc_data;
	abd_t *tabds[2] = { xabd, yabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_Q].rc_data,
		rm->rm_col[CODE_R].rc_data
	};
	const unsigned denom = 255 - vdev_raidz_log2[
		vdev_raidz_pow2[3 * ncols - 3 - x - 2 * y] ^
		vdev_raidz_pow2[3 * ncols - 3 - 2 * x - y]
	];
	const unsigned mul[] = {
		[MUL_QR_XQ]	= fix_mul_exp(ncols - 1 - y),
		[MUL_QR_X]	= fix_mul_exp(ncols - 1 - y + denom),
		[MUL_QR_YQ]	= fix_mul_exp(ncols - 1 - x),
		[MUL_QR_Y]	= fix_mul_exp(ncols - 1 - x + denom)
	};

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc_scatter(xsize);
		VERIFY(yabd);
		tabds[1] = yabd;
	}

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
		abd_copy(yabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
		abd_zero(yabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 2,
			raidz_syndrome_qr_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 2, raidz_rec_qr_abd, mul);

	raidz_math_end();

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize) {
		abd_copy(rm->rm_col[y].rc_data, yabd, ysize);
		abd_free(yabd, xsize);
	}

	return ((1 << 1) | (1 << 2));
}




static raidz_inline int
raidz_reconstruct_pqr_impl_abd(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t y = tgtidx[1];
	const size_t z = tgtidx[2];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	const size_t zsize = rm->rm_col[z].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	abd_t *yabd = rm->rm_col[y].rc_data;
	abd_t *zabd = rm->rm_col[z].rc_data;
	abd_t *tabds[] = { xabd, yabd, zabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_data,
		rm->rm_col[CODE_Q].rc_data,
		rm->rm_col[CODE_R].rc_data
	};
	const unsigned x_d = 255 - vdev_raidz_log2[
		vdev_raidz_pow2[3 * ncols - 3 - 2 * x - y] ^
		vdev_raidz_pow2[3 * ncols - 3 - x - 2 * y] ^
		vdev_raidz_pow2[3 * ncols - 3 - 2 * x - z] ^
		vdev_raidz_pow2[3 * ncols - 3 - x - 2 * z] ^
		vdev_raidz_pow2[3 * ncols - 3 - 2 * y - z] ^
		vdev_raidz_pow2[3 * ncols - 3 - y - 2 * z]
	];
	const unsigned y_d = 255 - vdev_raidz_log2[
		vdev_raidz_pow2[ncols - 1 - y] ^
		vdev_raidz_pow2[ncols - 1 - z]
	];
	const unsigned mul[] = {
	[MUL_PQR_XP] = fix_mul_exp(vdev_raidz_log2[
			vdev_raidz_pow2[3 * ncols - 3 - 2 * y - z] ^
			vdev_raidz_pow2[3 * ncols - 3 - y - 2 * z]
		] + x_d),
	[MUL_PQR_XQ] = fix_mul_exp(vdev_raidz_log2[
			vdev_raidz_pow2[2 * ncols - 2 - 2 * y] ^
			vdev_raidz_pow2[2 * ncols - 2 - 2 * z]
		] + x_d),
	[MUL_PQR_XR] = fix_mul_exp(vdev_raidz_log2[
			vdev_raidz_pow2[ncols - 1 - y] ^
			vdev_raidz_pow2[ncols - 1 - z]
		] + x_d),
	[MUL_PQR_YU] = fix_mul_exp(ncols - 1 - x),
	[MUL_PQR_YP] = fix_mul_exp(ncols - 1 - z + y_d),
	[MUL_PQR_YQ] = fix_mul_exp(y_d)
	};

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc_scatter(xsize);
		VERIFY(yabd);
		tabds[1] = yabd;
	}
	if (zsize < xsize) {
		zabd = abd_alloc_scatter(xsize);
		VERIFY(zabd);
		tabds[2] = zabd;
	}

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
		abd_copy(yabd, rm->rm_col[firstdc].rc_data, xsize);
		abd_copy(zabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
		abd_zero(yabd, xsize);
		abd_zero(zabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y || c == z) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 3,
			raidz_syndrome_pqr_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 3, raidz_rec_pqr_abd, mul);

	raidz_math_end();

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize) {
		abd_copy(rm->rm_col[y].rc_data, yabd, ysize);
		abd_free(yabd, xsize);
	}
	if (zsize < xsize) {
		abd_copy(rm->rm_col[z].rc_data, zabd, zsize);
		abd_free(zabd, xsize);
	}

	return ((1 << 0) | (1 << 1) | (1 << 2));
}

#endif /* _VDEV_RAIDZ_MATH_IMPL_H */
