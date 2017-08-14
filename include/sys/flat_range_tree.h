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
 * Copyright (c) 2017 Gvozden Nešković <neskovic@gmail.com> All rights reserved.
 */

#ifndef _SYS_FLAT_RANGE_TREE_H
#define	_SYS_FLAT_RANGE_TREE_H

#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif


typedef struct flat_range_tree_ops flat_range_tree_ops_t;

#define	FLAT_RANGE_TREE_HIST_SIZE	64


typedef struct flat_range_seg {
	uint64_t	frs_start;	/* starting offset of this segment */
	uint64_t	frs_end;	/* ending offset (non-inclusive) */
} flat_range_seg_t;

#define FRT_MAP_INIT_CAP	((PAGESIZE) / sizeof(flat_range_seg_t))

typedef struct flat_range_tree {
	/* segments map */
	uint64_t	frt_space;	/* sum of all segments in the map */
	size_t		frt_seg_cap;	/* map capacity */
	size_t		frt_seg_cnt;	/* number of segments in map */
	flat_range_seg_t	*frt_seg_map;

	/* pointer to lock that protects map */
	kmutex_t	*frt_lock;

	/* flat map operations */
	flat_range_tree_ops_t	*frt_ops;
	void			*frt_arg;

	/* The frt_histogram maintains a histogram of ranges. */
	uint64_t	frt_histogram[FLAT_RANGE_TREE_HIST_SIZE];
} flat_range_tree_t;

struct flat_range_tree_ops {
	void    (*frtop_create)(flat_range_tree_t *frt, void *arg);
	void    (*frtop_destroy)(flat_range_tree_t *frt, void *arg);
	void	(*frtop_add)(flat_range_tree_t *frt, flat_range_seg_t *frs, void *arg);
	void    (*frtop_remove)(flat_range_tree_t *frt, flat_range_seg_t *frs, void *arg);
	void	(*frtop_vacate)(flat_range_tree_t *frt, void *arg);
};

typedef void flat_range_tree_func_t(void *arg, uint64_t start, uint64_t size);

void flat_range_tree_init(void);
void flat_range_tree_fini(void);

flat_range_tree_t *flat_range_tree_create(flat_range_tree_ops_t *ops, void *arg, kmutex_t *lp);
void flat_range_tree_destroy(flat_range_tree_t *rt);
boolean_t flat_range_tree_contains(flat_range_tree_t *rt, uint64_t start, uint64_t size);
uint64_t flat_range_tree_space(flat_range_tree_t *rt);
uint64_t flat_range_tree_nodes(flat_range_tree_t *rt);
void flat_range_tree_verify(flat_range_tree_t *rt, uint64_t start, uint64_t size);
void flat_range_tree_swap(flat_range_tree_t **rtsrc, flat_range_tree_t **rtdst);
void flat_range_tree_stat_verify(flat_range_tree_t *rt);

void flat_range_tree_add(void *arg, uint64_t start, uint64_t size);
void flat_range_tree_remove(void *arg, uint64_t start, uint64_t size);
void flat_range_tree_clear(flat_range_tree_t *rt, uint64_t start, uint64_t size);

void flat_range_tree_vacate(flat_range_tree_t *rt, flat_range_tree_func_t *func, void *arg);
void flat_range_tree_walk(flat_range_tree_t *rt, flat_range_tree_func_t *func, void *arg);

flat_range_seg_t* flat_range_tree_lower_bound(flat_range_tree_t *frt, uint64_t start);
flat_range_seg_t* flat_range_tree_seg_first(flat_range_tree_t *frt);
flat_range_seg_t* flat_range_tree_seg_next(flat_range_tree_t *frt, flat_range_seg_t* frs);
flat_range_seg_t* flat_range_tree_seg_last(flat_range_tree_t *frt);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FLAT_RANGE_TREE_H */
