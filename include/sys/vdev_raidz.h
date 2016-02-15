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
 * Copyright (C) 2016 Gvozden Neskovic <neskovic@compeng.uni-frankfurt.de>.
 */

#ifndef _SYS_VDEV_RAIDZ_H
#define	_SYS_VDEV_RAIDZ_H

#include <sys/isa_defs.h>

#if defined(HAVE_SSE)
#define	RAIDZ_SSE			1
#if defined(__i386)
#define	RAIDZ_SSE_x86		1
#elif defined(__x86_64)
#define	RAIDZ_SSE_x86_64	1
#else
#error "Unknown RAIDZ SSE implementation"
#endif
#endif /* defined(HAVE_SSE) */

#if defined(HAVE_AVX2)
#define	RAIDZ_AVX2			1
#if defined(__i386)
#define	RAIDZ_AVX2_x86		1
#elif defined(__x86_64)
#define	RAIDZ_AVX2_x86_64	1
#else
#error "Unknown RAIDZ AVX2 implementation"
#endif
#endif /* defined(HAVE_AVX2) */

#ifdef	__cplusplus
extern "C" {
#endif





#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_RAIDZ_H */
