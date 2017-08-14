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

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dnode.h>
#include <sys/zio.h>
#include <sys/flat_range_tree.h>

typedef struct frm_stats {
	kstat_named_t frmstat_total_num;
	kstat_named_t frmstat_total_mem;
	kstat_named_t frmstat_max_mem;
	kstat_named_t frmstat_max_space;
	kstat_named_t frmstat_max_frag;
} frm_stats_t;

static frm_stats_t frm_stats = {
	/* number of flat_rang_map objects */
	{ "total_num",				KSTAT_DATA_UINT64 },
	{ "total_mem",				KSTAT_DATA_UINT64 },
	{ "max_mem",				KSTAT_DATA_UINT64 },
	{ "max_space",				KSTAT_DATA_UINT64 },
	{ "max_frag",				KSTAT_DATA_UINT64 },
};

static kstat_t *frm_ksp;

#define	FRMSTAT(stat)		(frm_stats.stat.value.ui64)
#define	FRMSTAT_INCR(stat, val) \
	atomic_add_64(&frm_stats.stat.value.ui64, (val))
#define	FRMSTAT_DECR(stat, val) \
	atomic_sub_64(&frm_stats.stat.value.ui64, (val))
#define	FRMSTAT_BUMP(stat)	FRMSTAT_INCR(stat, 1)
#define	FRMSTAT_BUMPDOWN(stat)	FRMSTAT_INCR(stat, -1)

void
flat_range_tree_init(void)
{
	frm_ksp = kstat_create("zfs", 0, "frmstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (frm_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (frm_ksp != NULL) {
		frm_ksp->ks_data = &frm_stats;
		kstat_install(frm_ksp);
	}
}

void
flat_range_tree_fini(void)
{
	if (frm_ksp != NULL) {
		kstat_delete(frm_ksp);
		frm_ksp = NULL;
	}
}

static inline void
flat_range_tree_stat_incr(flat_range_tree_t *frt, flat_range_seg_t *frs)
{
	uint64_t size = frs->frs_end - frs->frs_start;
	unsigned idx = highbit64(size) - 1;

	ASSERT3U(frs->frs_start, <, frs->frs_end);
	ASSERT3U(idx, <, FLAT_RANGE_TREE_HIST_SIZE);

	frt->frt_histogram[idx]++;
	ASSERT3U(frt->frt_histogram[idx], !=, 0);
}

static inline void
flat_range_tree_stat_decr(flat_range_tree_t *frt, flat_range_seg_t *frs)
{
	uint64_t size = frs->frs_end - frs->frs_start;
	unsigned idx = highbit64(size) - 1;

	ASSERT3U(frs->frs_start, <, frs->frs_end);
	ASSERT3U(idx, <, FLAT_RANGE_TREE_HIST_SIZE);
	ASSERT3U(frt->frt_histogram[idx], !=, 0);

	frt->frt_histogram[idx]--;
}

static void
flat_range_tree_check_resize(flat_range_tree_t *frt)
{
	ASSERT3U(frt->frt_seg_cnt, <=, frt->frt_seg_cap);
	/* increase capacity 1.5x, align to page size */
	if (frt->frt_seg_cnt == frt->frt_seg_cap) {
		flat_range_seg_t *nmap = NULL;
		ssize_t ncap = MAX(frt->frt_seg_cap * 3 / 2, FRT_MAP_INIT_CAP);
		ncap = P2ROUNDUP(ncap * sizeof (flat_range_seg_t),
		    (size_t)PAGESIZE);
		ncap /= sizeof (flat_range_seg_t);

		ASSERT3U(ncap, >, frt->frt_seg_cap);

		nmap = vmem_alloc(ncap * sizeof (flat_range_seg_t), KM_SLEEP);

		if(FRMSTAT(frmstat_max_mem) < (ncap * sizeof (flat_range_seg_t)))
			FRMSTAT(frmstat_max_mem) = (ncap * sizeof (flat_range_seg_t));

		FRMSTAT_INCR(frmstat_total_mem, ncap * sizeof (flat_range_seg_t));

		if (frt->frt_seg_map != NULL) {
			memmove(nmap, frt->frt_seg_map, frt->frt_seg_cnt *
			    sizeof (flat_range_seg_t));

			vmem_free(frt->frt_seg_map, frt->frt_seg_cap * sizeof (flat_range_seg_t));

			FRMSTAT_DECR(frmstat_total_mem, frt->frt_seg_cap * sizeof (flat_range_seg_t));
		}
		frt->frt_seg_cap = ncap;
		frt->frt_seg_map = nmap;
	}
}

flat_range_tree_t *
flat_range_tree_create(flat_range_tree_ops_t *ops, void *arg, kmutex_t *lp)
{
	flat_range_tree_t *frt;

	frt = kmem_zalloc(sizeof (flat_range_tree_t), KM_SLEEP);

	flat_range_tree_check_resize(frt);

	frt->frt_lock = lp;
	frt->frt_ops = ops;
	frt->frt_arg = arg;

	if (frt->frt_ops != NULL)
		frt->frt_ops->frtop_create(frt, frt->frt_arg);

	FRMSTAT_BUMP(frmstat_total_num);

	return (frt);
}

void
flat_range_tree_destroy(flat_range_tree_t *frt)
{
	VERIFY0(frt->frt_space);

	FRMSTAT_DECR(frmstat_total_mem, frt->frt_seg_cap * sizeof (flat_range_seg_t));

	if (frt->frt_ops != NULL)
		frt->frt_ops->frtop_destroy(frt, frt->frt_arg);

	kmem_free(frt->frt_seg_map,
	    frt->frt_seg_cap * sizeof (flat_range_seg_t));
	kmem_free(frt, sizeof (flat_range_tree_t));

	FRMSTAT_BUMPDOWN(frmstat_total_num);

}

#define	SEG_OVERLAP(a, b)	(((a)->frs_end > (b)->frs_start) && ((a)->frs_start < (b)->frs_end))
#define	SEG_CONT_LEFT(a, b)	((a)->frs_end == (b)->frs_start)
#define	SEG_CONT_RIGHT(a, b)	SEG_CONT_LEFT(b, a)

void
flat_range_tree_add(void *arg, uint64_t start, uint64_t size)
{
	flat_range_tree_t *frt = arg;
	boolean_t insert = B_TRUE;
	boolean_t update_current = B_FALSE;
	boolean_t delete_next = B_FALSE;
	flat_range_seg_t *frs = NULL, *frs_next = NULL;
	size_t i = 0;
	flat_range_seg_t nfrs = { .frs_start = start, .frs_end = start + size };

	// ASSERT(MUTEX_HELD(frt->frt_lock));
	VERIFY3U(size, >, 0);

	/* resize if needed to have consistent pointers later */
	flat_range_tree_check_resize(frt);

	/* find the place to insert/modify */
	for (i = 0; i < frt->frt_seg_cnt; i++) {
		frs = &frt->frt_seg_map[i];

		/* check for overlap */
		if (SEG_OVERLAP(&nfrs, frs)) {
			zfs_panic_recover("zfs: allocating allocated segment"
			    "(offset=%llu size=%llu)\n",
			    (longlong_t)start, (longlong_t)size);
			return;
		}

		if (frs->frs_start > nfrs.frs_end)
			break;
	}

	/* find the place to insert/modify */
	for (i = 0, frs = NULL; i < frt->frt_seg_cnt; i++) {
		frs = &frt->frt_seg_map[i];

		if (SEG_CONT_LEFT(&nfrs, frs)) {
			update_current = B_TRUE;
			insert = B_FALSE;
			nfrs.frs_end = frs->frs_end;
			break;
		}

		if (SEG_CONT_RIGHT(&nfrs, frs)) {
			update_current = B_TRUE;
			insert = B_FALSE;
			nfrs.frs_start = frs->frs_start;
			/* merge next ? */
			if ((i+1) < frt->frt_seg_cnt) {
				frs_next = frs + 1;
				if (SEG_CONT_LEFT(&nfrs, frs_next)) {
					delete_next = B_TRUE;
					nfrs.frs_end = frs_next->frs_end;
				}
			}
			break;
		}

		if (nfrs.frs_start < frs->frs_start) {
			insert = B_TRUE;
			break;
		}

		frs = NULL;
	}

	ASSERT3U(nfrs.frs_start, <, nfrs.frs_end);

	if (insert) {
		frs = frt->frt_seg_map + i;
		frs_next = frs + 1;
		memmove(frs_next, frs, sizeof(flat_range_seg_t) * (frt->frt_seg_cnt - i));
		*frs = nfrs;
		frt->frt_seg_cnt += 1;
	} else if (update_current && delete_next) {
		if (frt->frt_ops != NULL) {
			frt->frt_ops->frtop_remove(frt, frs, frt->frt_arg);
			frt->frt_ops->frtop_remove(frt, frs_next, frt->frt_arg);
		}

		flat_range_tree_stat_decr(frt, frs);
		flat_range_tree_stat_decr(frt, frs_next);

		memmove(frs_next, frs_next + 1, sizeof(flat_range_seg_t) * (frt->frt_seg_cnt - i - 2));
		*frs = nfrs;
		frt->frt_seg_cnt -= 1;
	} else if (update_current) {
		if (frt->frt_ops != NULL)
			frt->frt_ops->frtop_remove(frt, frs, frt->frt_arg);

		flat_range_tree_stat_decr(frt, frs);

		*frs = nfrs;
	}

	if (frt->frt_ops != NULL)
		frt->frt_ops->frtop_add(frt, frs, frt->frt_arg);

	flat_range_tree_stat_incr(frt, frs);
	frt->frt_space += size;


	if (frt->frt_space > FRMSTAT(frmstat_max_space))
		FRMSTAT(frmstat_max_space) = frt->frt_space;


	if ((frt->frt_seg_cnt > 0) && ((frt->frt_space / frt->frt_seg_cnt) > FRMSTAT(frmstat_max_frag)))
		FRMSTAT(frmstat_max_frag) = frt->frt_space / frt->frt_seg_cnt;

#ifdef _KERNEL
	if (frt->frt_space == (1ULL << 50))
		dump_stack();
#endif
}

#define	SEG_EQUAL(a, b)		(((a)->frs_start == (b)->frs_start) && ((a)->frs_end == (b)->frs_end))
#define	SEG_CONTAIN(a, b)	(((a)->frs_start >= (b)->frs_start) && ((a)->frs_end <= (b)->frs_end))
#define	SEG_CONTAIN_LEFT(a, b)	(((a)->frs_start == (b)->frs_start) && ((a)->frs_end <= (b)->frs_end))
#define	SEG_CONTAIN_RIGHT(a, b)	(((a)->frs_start >= (b)->frs_start) && ((a)->frs_end == (b)->frs_end))
#define	SEG_CONTAIN_MIDDLE(a, b)	(((a)->frs_start > (b)->frs_start) && ((a)->frs_end < (b)->frs_end))

void
flat_range_tree_remove(void *arg, uint64_t start, uint64_t size)
{
	flat_range_tree_t *frt = arg;
	boolean_t punch_left = B_FALSE;
	boolean_t punch_right = B_FALSE;
	boolean_t punch_middle = B_FALSE;
	flat_range_seg_t *frs = NULL, *frs_next;
	size_t i;

	flat_range_seg_t nfrs = { .frs_start = start, .frs_end = (start + size), };

	// ASSERT(MUTEX_HELD(frt->frt_lock));
	VERIFY3U(size, >, 0);

	/* remove operation can add segments */
	flat_range_tree_check_resize(frt);

	/* find segment to punch hole in */
	for (i = 0; i < frt->frt_seg_cnt; i++) {
		frs = &frt->frt_seg_map[i];

		if (SEG_CONTAIN_LEFT(&nfrs, frs))
			punch_left = B_TRUE;

		if (SEG_CONTAIN_RIGHT(&nfrs, frs))
			punch_right = B_TRUE;

		if (!punch_left && !punch_left && SEG_CONTAIN_MIDDLE(&nfrs, frs))
			punch_middle = B_TRUE;

		if (punch_left || punch_right || punch_middle)
			break;

		frs = NULL;
	}

	if (frs == NULL) {
		zfs_panic_recover("zfs: freeing non-tracked (free) segment "
		    "(offset=%llu size=%llu)",
		    (longlong_t)start, (longlong_t)size);
		return;
	}

	ASSERT(punch_left || punch_right || punch_middle);

	flat_range_tree_stat_decr(frt, frs);

	if (frt->frt_ops != NULL)
		frt->frt_ops->frtop_remove(frt, frs, frt->frt_arg);

	if (punch_left && punch_right) {
		ASSERT(!punch_middle);
		/* remove the whole segment */
		memmove(frs, frs + 1, sizeof(flat_range_seg_t) * (frt->frt_seg_cnt - i - 1));
		frt->frt_seg_cnt -= 1;
		frs = NULL;
	} else if (punch_left) {
		frs->frs_start = nfrs.frs_end;
		ASSERT3U(frs->frs_start, <, frs->frs_end);
	} else if (punch_right) {
		frs->frs_end = nfrs.frs_start;
	} else if (punch_middle) {
		frs_next = frs + 1;
		memmove(frs_next + 1, frs_next, sizeof(flat_range_seg_t) * (frt->frt_seg_cnt - i - 1));

		frs_next->frs_start = nfrs.frs_end;
		frs_next->frs_end = frs->frs_end;

		frs->frs_end = nfrs.frs_start;

		flat_range_tree_stat_incr(frt, frs_next);
		if (frt->frt_ops != NULL)
			frt->frt_ops->frtop_add(frt, frs_next, frt->frt_arg);

		frt->frt_seg_cnt += 1;
	}

	if (frs != NULL) {
 		flat_range_tree_stat_incr(frt, frs);

		if (frt->frt_ops != NULL)
			frt->frt_ops->frtop_add(frt, frs, frt->frt_arg);
	}

	frt->frt_space -= size;



	if ((frt->frt_seg_cnt > 0) && ((frt->frt_space / frt->frt_seg_cnt) > FRMSTAT(frmstat_max_frag)))
		FRMSTAT(frmstat_max_frag) = frt->frt_space / frt->frt_seg_cnt;
}

/*
 * Ensure that this range is not in the tree, regardless of whether
 * it is currently in the tree.
 */
void
flat_range_tree_clear(flat_range_tree_t *frt, uint64_t start, uint64_t size)
{
	flat_range_seg_t *frs;
	flat_range_seg_t cfrs = { .frs_start = start, .frs_end = start + size };
	ssize_t i;

	for (i = 0; i < flat_range_tree_nodes(frt); ) {
		frs = &frt->frt_seg_map[i];

		if (frs->frs_start > cfrs.frs_end)
			break;

		if (SEG_OVERLAP(&cfrs, frs)) {
			uint64_t e_start = MAX(frs->frs_start, cfrs.frs_start);
			uint64_t e_end = MIN(frs->frs_end, cfrs.frs_end);
			flat_range_tree_remove(frt, e_start, e_end - e_start);
			continue;
		}

		i++;
	}
}

void
flat_range_tree_walk(flat_range_tree_t *frt, flat_range_tree_func_t *func,
    void *arg)
{
	flat_range_seg_t *frs;
	size_t i;

	ASSERT(MUTEX_HELD(frt->frt_lock));

	if (func != 0) {
		for (i = 0; i < frt->frt_seg_cnt; i++) {
			frs = &frt->frt_seg_map[i];
			func(arg, frs->frs_start, frs->frs_end - frs->frs_start);
		}
	}
}

void
flat_range_tree_vacate(flat_range_tree_t *frt, flat_range_tree_func_t *func,
    void *arg)
{
	ASSERT(MUTEX_HELD(frt->frt_lock));

	if (frt->frt_ops != NULL)
		frt->frt_ops->frtop_vacate(frt, frt->frt_arg);

	flat_range_tree_walk(frt, func, arg);

	bzero(frt->frt_histogram, sizeof (frt->frt_histogram));
	frt->frt_seg_cnt = 0;
	frt->frt_space = 0;
}


boolean_t
flat_range_tree_contains(flat_range_tree_t *frt, uint64_t start, uint64_t size)
{
	flat_range_seg_t *frs;
	flat_range_seg_t ffrs = { .frs_start = start, .frs_end = start + size };
	size_t i;

	for (i = 0; i < frt->frt_seg_cnt; i++) {
		frs = &frt->frt_seg_map[i];

		if (SEG_OVERLAP(&ffrs, frs))
			return (B_TRUE);

		if (frs->frs_start > ffrs.frs_end)
			return (B_FALSE);
	}

	return (B_FALSE);
}

uint64_t
flat_range_tree_space(flat_range_tree_t *frt)
{
	return (frt->frt_space);
}

uint64_t
flat_range_tree_nodes(flat_range_tree_t *frt)
{
	return (frt->frt_seg_cnt);
}

void
flat_range_tree_verify(flat_range_tree_t *frt, uint64_t start, uint64_t size)
{
	ASSERT(MUTEX_NOT_HELD(frt->frt_lock));

	mutex_enter(frt->frt_lock);
	if (flat_range_tree_contains(frt, start, size))
		panic("freeing free block; "
		    "(offset=%llu size=%llu)",
		    (longlong_t)start, (longlong_t)size);
	mutex_exit(frt->frt_lock);
}

void
flat_range_tree_swap(flat_range_tree_t **frt_src, flat_range_tree_t **frt_dst)
{
	flat_range_tree_t *frt;

	ASSERT(MUTEX_HELD((*frt_src)->frt_lock));
	ASSERT0(flat_range_tree_space(*frt_dst));
	ASSERT0((*frt_dst)->frt_seg_cnt);

	frt = *frt_src;
	*frt_src = *frt_dst;
	*frt_dst = frt;
}

void
flat_range_tree_stat_verify(flat_range_tree_t *frt)
{
	flat_range_seg_t *frs;
	uint64_t hist[FLAT_RANGE_TREE_HIST_SIZE] = { 0 };
	size_t i;

	for (i = 0; i < frt->frt_seg_cnt; i++) {
		frs = &frt->frt_seg_map[i];
		uint64_t size = frs->frs_end - frs->frs_start;
		unsigned idx = highbit64(size) - 1;

		hist[idx]++;
	}

	for (i = 0; i < FLAT_RANGE_TREE_HIST_SIZE; i++) {
		if (hist[i] != frt->frt_histogram[i]) {
			zfs_dbgmsg("i=%d, hist=%p, hist=%llu, rt_hist=%llu",
			    i, hist, hist[i], frt->frt_histogram[i]);
		}
		VERIFY3U(hist[i], ==, frt->frt_histogram[i]);
	}

}

flat_range_seg_t*
flat_range_tree_lower_bound(flat_range_tree_t *frt, uint64_t start)
{
	flat_range_seg_t *frs;
	size_t i;

	for (i = 0; i < frt->frt_seg_cnt; i++) {
		frs = &frt->frt_seg_map[i];

		if (frs->frs_start >= start)
			return (frs);
	}

	return (NULL);
}

flat_range_seg_t*
flat_range_tree_seg_first(flat_range_tree_t *frt)
{
	if (frt->frt_seg_cnt == 0)
		return (NULL);

	return (frt->frt_seg_map);
}

flat_range_seg_t*
flat_range_tree_seg_last(flat_range_tree_t *frt)
{
	if (frt->frt_seg_cnt == 0)
		return (NULL);

	return (frt->frt_seg_map + (frt->frt_seg_cnt - 1));
}

flat_range_seg_t*
flat_range_tree_seg_next(flat_range_tree_t *frt, flat_range_seg_t* frs)
{
	size_t idx = (frs - frt->frt_seg_map) + 1;

	VERIFY3U(idx, <=, frt->frt_seg_cnt);

	if (idx == frt->frt_seg_cnt)
		return (NULL);

	return (frt->frt_seg_map + idx);
}
