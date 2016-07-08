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
 * Copyright (C) 2016 Gvozden Neskovic. All rights reserved.
 */

#include <sys/dsl_scan.h>
#include <sys/dsl_seqscrub.h>
#include <sys/dsl_pool.h>
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>

extern int zfs_top_maxinflight;	/* maximum I/Os per top-level */
extern int zfs_resilver_delay;	/* number of ticks to delay resilver */
extern int zfs_scrub_delay;	/* number of ticks to delay scrub */
extern int zfs_scan_idle;	/* idle window in clock ticks */

extern int zfs_scan_min_time_ms; 	/* min millisecs to scrub per txg */
extern int zfs_free_min_time_ms; 	/* min millisecs to free per txg */
extern int zfs_resilver_min_time_ms; 	/* min millisecs to resilver per txg */


extern int zfs_no_scrub_io; /* set to disable scrub i/o */

/* Compate first DVA offsets */
int
dsl_seqscrub_compare(const void *arg1, const void *arg2)
{
	const dsl_seqscrub_entry_t *dse1 = arg1;
	const dsl_seqscrub_entry_t *dse2 = arg2;

	uint64_t offset1, offset2;

	// ASSERT3U(1, >=, BP_GET_NDVAS(&dse1->dse_bp)); // this triggers?!
	// ASSERT3U(1, >=, BP_GET_NDVAS(&dse2->dse_bp));

	offset1 = DVA_GET_OFFSET(&dse1->dse_bp.blk_dva[0]);
	offset2 = DVA_GET_OFFSET(&dse2->dse_bp.blk_dva[0]);

	if (offset1 < offset2)
		return (-1);
	else if (offset2 < offset1)
		return (+1);
	else
		return (0);
}



static void
dsl_scan_scrub_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;

	zio_data_buf_free(zio->io_data, zio->io_size);

	mutex_enter(&spa->spa_scrub_lock);
	spa->spa_scrub_inflight--;
	cv_broadcast(&spa->spa_scrub_io_cv);

	if (zio->io_error && (zio->io_error != ECKSUM ||
	    !(zio->io_flags & ZIO_FLAG_SPECULATIVE))) {
		spa->spa_dsl_pool->dp_scan->scn_phys.scn_errors++;
	}
	mutex_exit(&spa->spa_scrub_lock);
}

int
dsl_seqscan_scrub(dsl_pool_t *dp, const dsl_seqscrub_entry_t *dse)
{
	const blkptr_t *bp = &dse->dse_bp;
	const zbookmark_phys_t *zb = &dse->dse_zb;
	const size_t size = BP_GET_PSIZE(bp);
	const uint64_t phys_birth = BP_PHYSICAL_BIRTH(bp);

	dsl_scan_t *scn = dp->dp_scan;
	spa_t *spa = dp->dp_spa;
	boolean_t needs_io = B_FALSE;
	int zio_flags = ZIO_FLAG_SCAN_THREAD | ZIO_FLAG_RAW | ZIO_FLAG_CANFAIL;
	int scan_delay = 0;
	int d;

	ASSERT(DSL_SCAN_IS_SCRUB_RESILVER(scn));
	if (scn->scn_phys.scn_func == POOL_SCAN_SCRUB) {
		zio_flags |= ZIO_FLAG_SCRUB;
		needs_io = B_TRUE;
		scan_delay = zfs_scrub_delay;
	} else {
		ASSERT3U(scn->scn_phys.scn_func, ==, POOL_SCAN_RESILVER);
		zio_flags |= ZIO_FLAG_RESILVER;
		needs_io = B_FALSE;
		scan_delay = zfs_resilver_delay;
	}

	/* If it's an intent log block, failure is expected. */
	if (zb->zb_level == ZB_ZIL_LEVEL)
		zio_flags |= ZIO_FLAG_SPECULATIVE;

	for (d = 0; d < BP_GET_NDVAS(bp); d++) {
		vdev_t *vd = vdev_lookup_top(spa,
		    DVA_GET_VDEV(&bp->blk_dva[d]));

		/*
		 * Keep track of how much data we've examined so that
		 * zpool(1M) status can make useful progress reports.
		 */
		scn->scn_phys.scn_examined += DVA_GET_ASIZE(&bp->blk_dva[d]);
		spa->spa_scan_pass_exam += DVA_GET_ASIZE(&bp->blk_dva[d]);

		/* if it's a resilver, this may not be in the target range */
		if (!needs_io) {
			if (DVA_GET_GANG(&bp->blk_dva[d])) {
				/*
				 * Gang members may be spread across multiple
				 * vdevs, so the best estimate we have is the
				 * scrub range, which has already been checked.
				 * XXX -- it would be better to change our
				 * allocation policy to ensure that all
				 * gang members reside on the same vdev.
				 */
				needs_io = B_TRUE;
			} else {
				needs_io = vdev_dtl_contains(vd, DTL_PARTIAL,
				    phys_birth, 1);
			}
		}
	}

	if (needs_io && !zfs_no_scrub_io) {
		vdev_t *rvd = spa->spa_root_vdev;
		uint64_t maxinflight = rvd->vdev_children * zfs_top_maxinflight;
		void *data = zio_data_buf_alloc(size);

		mutex_enter(&spa->spa_scrub_lock);
		while (spa->spa_scrub_inflight >= maxinflight)
			cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
		spa->spa_scrub_inflight++;
		mutex_exit(&spa->spa_scrub_lock);

		/*
		 * If we're seeing recent (zfs_scan_idle) "important" I/Os
		 * then throttle our workload to limit the impact of a scan.
		 */
		if (ddi_get_lbolt64() - spa->spa_last_io <= zfs_scan_idle)
			delay(scan_delay);

		zio_nowait(zio_read(NULL, spa, bp, data, size,
		    dsl_scan_scrub_done, NULL, ZIO_PRIORITY_SCRUB,
		    zio_flags, zb));
	}

	/* do not relocate this block */
	return (0);
}

// #if 0
static boolean_t
dsl_seqscrub_check_pause(dsl_scan_t *scn)
{
	uint64_t elapsed_nanosecs;
	int mintime;

	mintime = (scn->scn_phys.scn_func == POOL_SCAN_RESILVER) ?
	    zfs_resilver_min_time_ms : zfs_scan_min_time_ms;
	elapsed_nanosecs = gethrtime() - scn->scn_sync_start_time;

	if (elapsed_nanosecs / NANOSEC >= zfs_txg_timeout ||
	    NSEC2MSEC(elapsed_nanosecs) > mintime) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

// #endif

ulong
dsl_seqscrub_process(dsl_scan_t *scn)
{
	ulong processed = 0;
	while (!dsl_seqscrub_check_pause(scn) &&
		!DSL_SEQSCRUB_ELEVATOR_EMPTY(scn)) {

		dsl_seqscrub_entry_t *dse;
		dse = avl_first(&scn->scn_scrub_elevator);

		dsl_seqscan_scrub(scn->scn_dp, dse);

		zfs_dbgmsg("scrubing block with DVA_OFFSET [%llu]\n",
			(longlong_t) DVA_GET_OFFSET(&dse->dse_bp.blk_dva[0]));

		avl_remove(&scn->scn_scrub_elevator, dse);
		kmem_free(dse, sizeof (dsl_seqscrub_entry_t));
		processed++;
	}

	return (processed);
}


void
dsl_seqscrub_done(dsl_scan_t *scn, dmu_tx_t *tx)
{
	dsl_pool_t *dp = scn->scn_dp;
	spa_t *spa = dp->dp_spa;

	mutex_enter(&spa->spa_scrub_lock);
	while (spa->spa_scrub_inflight > 0)
		cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
	mutex_exit(&spa->spa_scrub_lock);

	spa->spa_scrub_started = B_FALSE;
	spa->spa_scrub_active = B_FALSE;

	/*
	 * If the scrub/resilver completed, update all DTLs to
	 * reflect this.  Whether it succeeded or not, vacate
	 * all temporary scrub DTLs.
	 */
	vdev_dtl_reassess(spa->spa_root_vdev, tx->tx_txg,
	    scn->scn_phys.scn_max_txg, B_TRUE);

	spa_event_notify(spa, NULL, scn->scn_phys.scn_min_txg ?
	    FM_EREPORT_ZFS_RESILVER_FINISH :
	    FM_EREPORT_ZFS_SCRUB_FINISH);

	spa_errlog_rotate(spa);

	/*
	 * We may have finished replacing a device.
	 * Let the async thread assess this and handle the detach.
	 */
	spa_async_request(spa, SPA_ASYNC_RESILVER_DONE);
}
