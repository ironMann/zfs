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

#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu_tx.h>

typedef struct dsl_seqscrub_entry {
	avl_node_t dse_node;

	blkptr_t dse_bp;
	zbookmark_phys_t dse_zb; /* ? */
} dsl_seqscrub_entry_t;

typedef struct dsl_pool dsl_pool_t;

#define	DSL_SEQSCRUB_ELEVATOR_EMPTY(snc)	\
	(avl_is_empty(&scn->scn_scrub_elevator))

int dsl_seqscrub_compare(const void *arg1, const void *arg2);

int dsl_seqscan_scrub(dsl_pool_t *dp, const dsl_seqscrub_entry_t *dse);
ulong dsl_seqscrub_process(dsl_scan_t *scn);
void dsl_seqscrub_done(dsl_scan_t *scn, dmu_tx_t *tx);
