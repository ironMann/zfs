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

#ifndef _VDEV_RAIDZ_H
#define	_VDEV_RAIDZ_H

#define	raidz_inline inline __attribute__((always_inline))

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/kstat.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define	CODE_P	(0U)
#define	CODE_Q	(1U)
#define	CODE_R	(2U)

#define	CODE_PQ		(CODE_P | CODE_Q)
#define	CODE_PQR	(CODE_P | CODE_Q | CODE_R)

enum raidz_math_gen_op {
	RAIDZ_GEN_P = 0,
	RAIDZ_GEN_PQ,
	RAIDZ_GEN_PQR,
	RAIDZ_GEN_NUM = 3
};
enum raidz_rec_op {
	RAIDZ_REC_P = 0,
	RAIDZ_REC_Q,
	RAIDZ_REC_R,
	RAIDZ_REC_PQ,
	RAIDZ_REC_PR,
	RAIDZ_REC_QR,
	RAIDZ_REC_PQR,
	RAIDZ_REC_NUM = 7
};

typedef struct raidz_math_ops raidz_math_ops_t_;

typedef struct raidz_col {
	size_t rc_devidx;	/* child device index for I/O */
	size_t rc_offset;	/* device offset */
	size_t rc_size;	/* I/O size */
	void *rc_data;			/* I/O data */
	void *rc_gdata;			/* used to store the "good" version */
	int rc_error;			/* I/O error for this device */
	unsigned int rc_tried;	/* Did we attempt this I/O column? */
	unsigned int rc_skipped;	/* Did we skip this I/O column? */
} raidz_col_t;

typedef struct raidz_map {
	size_t rm_cols;		/* Regular column count */
	size_t rm_scols;		/* Count including skipped columns */
	size_t rm_bigcols;	/* Number of oversized columns */
	size_t rm_asize;		/* Actual total I/O size */
	size_t rm_missingdata;	/* Count of missing data devices */
	size_t rm_missingparity;	/* Count of missing parity devices */
	size_t rm_firstdatacol;	/* First data column/parity count */
	size_t rm_nskip;		/* Skipped sectors for padding */
	size_t rm_skipstart;	/* Column index of padding start */
	void *rm_datacopy;		/* rm_asize-buffer of copied data */
	size_t rm_reports;	/* # of referencing checksum reports */
	unsigned int rm_freed;		/* map no longer has referencing ZIO */
	unsigned int rm_ecksuminjected;	/* checksum error was injected */
	raidz_math_ops_t_ *rm_ops;	/* RAIDZ math operations */
	raidz_col_t rm_col[1];		/* Flexible array of I/O columns */
} raidz_map_t;


typedef struct zio zio_t_;

raidz_map_t * vdev_raidz_map_alloc(zio_t_ *zio, uint64_t unit_shift,
	uint64_t dcols, uint64_t nparity);
void vdev_raidz_map_free(raidz_map_t *rm);
void vdev_raidz_generate_parity(raidz_map_t *rm);
int vdev_raidz_reconstruct(raidz_map_t *rm, int *t, int nt);

/* parity of the raidz block */
static raidz_inline size_t raidz_parity(const raidz_map_t *rm)
{
	return (rm->rm_firstdatacol);
}

/* total number of columns */
static raidz_inline size_t raidz_ncols(const raidz_map_t *rm)
{
	return (rm->rm_cols);
}

/* size of code columns */
static raidz_inline size_t raidz_csize(const raidz_map_t *rm)
{
	ASSERT(rm->rm_col[0].rc_size ==
		rm->rm_col[rm->rm_firstdatacol].rc_size);
	return (rm->rm_col[0].rc_size);
}

/* size of the first data column */
static raidz_inline size_t raidz_dsize(const raidz_map_t *rm)
{
	ASSERT(raidz_csize(rm) ==
		rm->rm_col[rm->rm_firstdatacol].rc_size);
	return (raidz_csize(rm));
}

/* size of the last data dolumn */
static raidz_inline size_t raidz_ldsize(const raidz_map_t *rm)
{
	return (rm->rm_col[rm->rm_cols - 1].rc_size);
}

/* number of bigger data columns */
static raidz_inline size_t raidz_nbigcols(const raidz_map_t *rm)
{
	size_t nbigdcols = 0;

	if (rm->rm_bigcols == 0) {
		nbigdcols = 0; /* all same size */
	} else if (rm->rm_bigcols == rm->rm_cols) {
		nbigdcols = 0; /* data doesn't span all columns */
	} else if (rm->rm_bigcols < rm->rm_cols) {
		nbigdcols = rm->rm_bigcols - raidz_parity(rm);

		ASSERT(rm->rm_col[rm->rm_bigcols - 1].rc_size ==
			raidz_csize(rm));
		ASSERT(rm->rm_col[rm->rm_firstdatacol].rc_size ==
			raidz_csize(rm));
	} else {
		nbigdcols = 0;
	}

	ASSERT((nbigdcols + raidz_parity(rm)) < raidz_ncols(rm));
	return (nbigdcols + raidz_parity(rm));
}

#if !defined(_KERNEL)
struct kernel_param {};
#endif

void vdev_raidz_math_init(void);
void vdev_raidz_math_fini(void);
int zfs_raidz_math_impl_set(const char *math, struct kernel_param *kp);

/* testing interface for userspace */
#if !defined(_KERNEL)
void vdev_raidz_cycle_impl(unsigned int v);
#endif

void raidz_math_ops_init(raidz_map_t *rm);
void raidz_math_generate(raidz_map_t *rm);
int raidz_math_reconstruct(raidz_map_t *rm, const int *parity_valid,
	const int *dt, const int nbaddata);

extern const char *raidz_gen_name[RAIDZ_GEN_NUM];
extern const char *raidz_rec_name[RAIDZ_REC_NUM];

typedef struct raidz_math_ops_kstat {
	kstat_named_t gen_kstat[RAIDZ_GEN_NUM];
	kstat_named_t rec_kstat[RAIDZ_REC_NUM];
} raidz_math_ops_kstat_t;

typedef void (*raidz_gen_f)(raidz_map_t *rm);
typedef int (*raidz_rec_f)(raidz_map_t *rm, const int *);
typedef boolean_t (*will_work_f)(void);

typedef struct raidz_math_ops {
	raidz_gen_f gen[RAIDZ_GEN_NUM];
	raidz_rec_f rec[RAIDZ_REC_NUM];
	will_work_f is_supported;
	char *name;
} raidz_math_ops_t;

extern size_t raidz_supp_maths_cnt;
extern raidz_math_ops_t *raidz_supp_maths[];

#define	RAIDZ_SCALAR_GEN_WRAP(code, impl) \
static void	\
impl ## _gen_ ## code(raidz_map_t *rm)\
{\
	raidz_generate_## code ## _impl(rm); \
}

#define	RAIDZ_SCALAR_REC_WRAP(code, impl) \
static int \
impl ## _rec_ ## code(raidz_map_t *rm, const int *tgtidx)	\
{	\
	return (raidz_reconstruct_## code ## _impl(rm, tgtidx));	\
}


#define	RAIDZ_x86SIMD_GEN_WRAP(code, impl) \
static void	\
impl ## _gen_ ## code(raidz_map_t *rm)\
{\
	kfpu_begin();\
	raidz_generate_## code ## _impl(rm); \
	kfpu_end();\
}

#define	RAIDZ_x86SIMD_REC_WRAP(code, impl) \
static int \
impl ## _rec_ ## code(raidz_map_t *rm, const int *tgtidx)\
{	\
	int code;\
	kfpu_begin();\
	code = raidz_reconstruct_## code ## _impl(rm, tgtidx);	\
	kfpu_end();\
	return (code);\
}

/*
 * Powers of 2 in the Galois field defined above.
 * Elements are repeated to seepd up vdev_raidz_exp2 function,
 * (used in scalar reconstruction).
 */
extern const unsigned char vdev_raidz_pow2[511] __attribute__((aligned(256)));
/* Logs of 2 in the Galois field defined above. */
extern const unsigned char vdev_raidz_log2[256] __attribute__((aligned(256)));

/*
 * Multiply a given number by 2 raised to the given power.
 */
static inline unsigned char
vdev_raidz_exp2(const unsigned char a, const unsigned char exp)
{
	if (a == 0) {
		return (0);
	} else {
		return (vdev_raidz_pow2[exp + vdev_raidz_log2[a]]);
	}
}

#ifdef  __cplusplus
}
#endif

#endif /* _VDEV_RAIDZ_H */
