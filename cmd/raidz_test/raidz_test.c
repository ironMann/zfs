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

#include <sys/zfs_context.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/zio.h>
#include <umem.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>
#include <stdio.h>
#include "raidz_test.h"

raidz_test_opts_t rto_opts;

static char gdb[256];
static const char gdb_tmpl[] = "gdb -ex \"set pagination 0\" -p %d";

static void sig_handler(int signo)
{
	struct sigaction action;
	/*
	 * Restore default action and re-raise signal so SIGSEGV and
	 * SIGABRT can trigger a core dump.
	 */
	action.sa_handler = SIG_DFL;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	(void) sigaction(signo, &action, NULL);

	if (rto_opts.rto_gdb) {

		if (system(gdb));
	}

	raise(signo);
}

static void print_opts(void)
{
	char *verbose;
	switch (rto_opts.rto_v) {
		case 0:
			verbose = "no";
			break;
		case 1:
			verbose = "info";
			break;
		default:
			verbose = "debug";
			break;
	}

	PRINT(DBLSEP);
	PRINT("Running with options:\n"
	"  (-a) zio ashift                   : %zu\n"
	"  (-o) zio offset                   : 1 << %zu\n"
	"  (-d) number of raidz data columns : %zu\n"
	"  (-s) size of DATA                 : 1 << %zu\n"
	"  (-S) sweep parameters             : %s \n"
	"  (-v) verbose                      : %s \n\n",
	rto_opts.rto_ashift,			/* -a */
	ilog2(rto_opts.rto_offset),		/* -o */
	rto_opts.rto_dcols,			/* -d */
	ilog2(rto_opts.rto_dsize),		/* -s */
	rto_opts.rto_sweep ? "yes" : "no",	/* -S */
	verbose 				/* -v */
	);
}

static void usage(boolean_t requested)
{
	const raidz_test_opts_t *o = &rto_opts_defaults;

	FILE *fp = requested ? stdout : stderr;

	(void) fprintf(fp, "Usage:\n"
	"\t[-a zio ashift (default: %zu)]\n"
	"\t[-o zio offset, exponent radix 2 (default: %zu)]\n"
	"\t[-d number of raidz data columns (default: %zu)]\n"
	"\t[-s zio size, exponent radix 2 (default: %zu)]\n"
	"\t[-S parameter space sweep (default: %s)]\n"
	"\t[-B benchmark all raidz implementations]\n"
	"\t[-v increase verbosity (default: %zu)]\n"
	"\t[-h (print help)]\n"
	"\t[-T test the test, see if failure would be detected]\n"
	"\t[-D debug (attach gdb on SIGSEGV)]\n"
	"",
	o->rto_ashift,				/* -a */
	ilog2(o->rto_offset),			/* -o */
	o->rto_dcols,				/* -d */
	ilog2(o->rto_dsize),			/* -s */
	rto_opts.rto_sweep ? "yes" : "no",	/* -S */
	o->rto_v				/* -d */
	);

	exit(requested ? 0 : 1);
}

static void process_options(int argc, char **argv)
{
	char *end;
	size_t value;
	int opt;

	raidz_test_opts_t *o = &rto_opts;

	bcopy(&rto_opts_defaults, o, sizeof (*o));

	while ((opt = getopt(argc, argv,
	    "TDBSvha:o:d:s:")) != EOF) {
		value = 0;
		switch (opt) {
		case 'a':
		case 'o':
		case 'd':
		case 's':
			value = strtoull(optarg, &end, 0);
		}

		switch (opt) {
		case 'a':
			o->rto_ashift = MIN(12, MAX(9, value));
			break;
		case 'o':
			o->rto_offset = ((1ULL << MIN(12, value)) >> 9) << 9;
			break;
		case 'd':
			o->rto_dcols = MIN(255, MAX(1, value));
			break;
		case 's':
			o->rto_dsize = 1ULL <<  MIN(24, MAX(9, value));
			break;
		case 'v':
			o->rto_v++;
			break;
		case 'S':
			o->rto_sweep++;
			break;
		case 'B':
			o->rto_benchmark++;
			break;
		case 'D':
			o->rto_gdb = 1;
			break;
		case 'T':
			o->rto_sanity = 1;
			break;
		case 'h':
			usage(B_TRUE);
			break;
		case '?':
		default:
			usage(B_FALSE);
			break;
		}
	}
}

static zio_t *zio_golden;
static raidz_map_t *rm_golden;

#define	D_INFO 1
#define	D_DEBUG 2

#define	DATA_COL(rm, i) ((rm)->rm_col[raidz_parity(rm) + (i)].rc_data)
#define	DATA_COL_SIZE(rm, i) ((rm)->rm_col[raidz_parity(rm) + (i)].rc_size)

#define	CODE_COL(rm, i) ((rm)->rm_col[(i)].rc_data)
#define	CODE_COL_SIZE(rm, i) ((rm)->rm_col[(i)].rc_size)

static int
cmp_code(const raidz_map_t *rma, const raidz_map_t *rmb,
	const int parity)
{
	int i, eret = 0;

	VERIFY(parity >= 1 && parity <= 3);

	for (i = 0; i < parity; i++) {
		if (0 != abd_cmp(CODE_COL(rma, i), CODE_COL(rmb, i),
			CODE_COL_SIZE(rma, i))) {
			eret++;
			if (rto_opts.rto_v > D_INFO) {
				PRINT("\nparity block [%d] different!\n", i);
			}
		}
	}
	return (eret);
}

static int
cmp_data(raidz_map_t *rm_golden, raidz_map_t *rm)
{
	int i, eret = 0;

	for (i = 0; i < rm_golden->rm_cols-raidz_parity(rm_golden); i++) {
		if (0 != abd_cmp(DATA_COL(rm_golden, i), DATA_COL(rm, i),
			DATA_COL_SIZE(rm_golden, i))) {
			eret++;
			if (rto_opts.rto_v > D_INFO) {
				PRINT("data block [%d] different!\n", i);
			}
		}
	}
	return (eret);
}

static int
init_rand(void *data, uint64_t size, void *private)
{
	int i;
	int *dst = (int *) data;

	for (i = 0; i < size / sizeof (int); i++) {
		dst[i] = rand();
	}
	return (0);
}

void
corrupt_colums(raidz_map_t *rm, const int *tgts, const int cnt)
{
	int i;
	raidz_col_t *col;
	srand((unsigned)time(NULL) * getpid());

	for (i = 0; i < cnt; i++) {
		col = &rm->rm_col[tgts[i]];
		abd_iterate_wfunc(col->rc_data, col->rc_size,
		    init_rand, NULL);
	}
}

void
init_zio_data(zio_t *zio)
{
	srand(getpid());

	abd_iterate_wfunc(zio->io_data, zio->io_size,
		init_rand, NULL);
}

static void
fini_raidz_map(zio_t **zio, raidz_map_t **rm)
{
	vdev_raidz_map_free(*rm);
	abd_free((*zio)->io_data, (*zio)->io_size);
	umem_free(*zio, sizeof (zio_t));

	*zio = NULL;
	*rm = NULL;
}

static void
init_raidz_golden_map(const int parity)
{
	unsigned err = 0;
	zio_t *zio_test;
	raidz_map_t *rm_test;
	const size_t alloc_dsize = rto_opts.rto_dsize;
	const size_t total_ncols = rto_opts.rto_dcols + parity;

	if (rm_golden) {
		fini_raidz_map(&zio_golden, &rm_golden);
	}

	zio_golden = umem_zalloc(sizeof (zio_t), UMEM_NOFAIL);
	zio_test = umem_zalloc(sizeof (zio_t), UMEM_NOFAIL);

	zio_golden->io_offset = zio_test->io_offset = 0;
	zio_golden->io_size = zio_test->io_size = alloc_dsize;

	/*
	 * To permit larger column sizes these have to be done
	 * allocated using aligned alloc instead of zio_data_buf_alloc
	 */
	zio_golden->io_data = abd_alloc_scatter(alloc_dsize);
	zio_test->io_data = abd_alloc_scatter(alloc_dsize);

	init_zio_data(zio_golden);
	init_zio_data(zio_test);

	VERIFY0(zfs_raidz_math_impl_set("original", NULL));

	rm_golden = vdev_raidz_map_alloc(zio_golden, rto_opts.rto_ashift,
	    total_ncols, parity);
	rm_test = vdev_raidz_map_alloc(zio_test, rto_opts.rto_ashift,
	    total_ncols, parity);

	vdev_raidz_generate_parity(rm_golden);
	vdev_raidz_generate_parity(rm_test);

	/* sanity check */
	err |= cmp_data(rm_test, rm_golden);
	err |= cmp_code(rm_test, rm_golden, parity);

	if (err) {
		PRINT(DBLSEP);
		PRINT("initializing the golden copy ... ");
		PRINT("[FAIL]!\n");
		exit(-1);
	}
	/* tear down raidz_map of test zio */
	fini_raidz_map(&zio_test, &rm_test);

	VERIFY(zio_golden);
	VERIFY(rm_golden);
}

static raidz_map_t *
init_raidz_map(zio_t **zio, const int parity)
{
	unsigned err = 0;
	raidz_map_t *rm = NULL;
	const size_t alloc_dsize = rto_opts.rto_dsize;
	const size_t total_ncols = rto_opts.rto_dcols + parity;
	const int ccols[] = { 0, 1, 2 };

	VERIFY(zio);
	VERIFY(parity <= 3 && parity >= 1);

	*zio = umem_alloc(sizeof (zio_t), UMEM_NOFAIL);

	bzero(*zio, sizeof (zio_t));

	(*zio)->io_offset = 0;
	(*zio)->io_size = alloc_dsize;
	(*zio)->io_data = abd_alloc_scatter(alloc_dsize);
	init_zio_data(*zio);

	rm = vdev_raidz_map_alloc(*zio, rto_opts.rto_ashift,
		total_ncols, parity);
	VERIFY(rm);

	/* Make sure code columns are destroyed */
	corrupt_colums(rm, ccols, parity);


	if (err) {
		PRINT("ERROR!\n");
		exit(-1);
	}

	return (rm);
}

int
run_gen_check(void)
{
	char **impl_name;
	int fn, err = 0;
	zio_t *zio_test;
	raidz_map_t *rm_test;

	init_raidz_golden_map(PARITY_PQR);

	PRINT(DBLSEP);
	PRINT("Testing parity generation...\n");

	for (impl_name = (char **)raidz_impl_names; *impl_name != NULL;
	    impl_name++) {

		PRINT(SEP);
		PRINT("\tTesting [%s] implementation...", *impl_name);

		if (0 != zfs_raidz_math_impl_set(*impl_name, NULL)) {
			PRINT("[SKIP]\n");
			continue;
		} else {
			PRINT("[SUPPORTED]\n");
		}

		for (fn = 0; fn < RAIDZ_GEN_NUM; fn++) {

			/* create suitable raidz_map */
			rm_test = init_raidz_map(&zio_test, fn+1);
			VERIFY(rm_test);

			PRINT("\t\tTesting method [%s] ...",
			    raidz_gen_name[fn]);

			if (!rto_opts.rto_sanity)
				vdev_raidz_generate_parity(rm_test);

			if (0 == cmp_code(rm_test, rm_golden,
			    fn+1)) {
				PRINT("[PASS]\n");
			} else {
				PRINT("[FAIL]\n");
				err++;
			}

			fini_raidz_map(&zio_test, &rm_test);
		}
	}

	fini_raidz_map(&zio_golden, &rm_golden);

	return (err);
}

unsigned
__run_rec_check(raidz_map_t *rm, const int fn)
{
	int x0, x1, x2;
	int tgtidx[3];
	int err = 0;
	static const int rec_tgts[7][3] = {
		{1, 2, 3},	/* rec_p:   bad QR & D[0]	*/
		{0, 2, 3},	/* rec_q:   bad PR & D[0]	*/
		{0, 1, 3},	/* rec_r:   bad PQ & D[0]	*/
		{2, 3, 4},	/* rec_pq:  bad R  & D[0][1]	*/
		{1, 3, 4},	/* rec_pr:  bad Q  & D[0][1]	*/
		{0, 3, 4},	/* rec_qr:  bad P  & D[0][1]	*/
		{3, 4, 5}	/* rec_pqr: bad    & D[0][1][2] */
	};

	memcpy(tgtidx, rec_tgts[fn], sizeof (tgtidx));

	if (fn < RAIDZ_REC_PQ) {
		/* can reconstruct 1 failed data disk */
		for (x0 = 0; x0 < rto_opts.rto_dcols; x0++) {
			if (x0 >= rm->rm_cols - raidz_parity(rm))
				continue;

			tgtidx[2] = x0 + raidz_parity(rm);

			if (rto_opts.rto_v >= D_INFO) {
				PRINT("[%d] ", x0);
			}

			corrupt_colums(rm, tgtidx+2, 1);

			if (!rto_opts.rto_sanity)
				vdev_raidz_reconstruct(rm, tgtidx, 3);

			if (cmp_data(rm_golden, rm)) {
				err++;
				if (rto_opts.rto_v >= D_INFO)
					PRINT("\nREC D[%d]... [FAIL]\n", x0);
			}
		}

	} else if (fn < RAIDZ_REC_PQR) {
		/* can reconstruct 2 failed data disk */
		for (x0 = 0; x0 < rto_opts.rto_dcols; x0++) {
			if (x0 >= rm->rm_cols - raidz_parity(rm))
				continue;
			for (x1 = x0 + 1; x1 < rto_opts.rto_dcols; x1++) {
				if (x1 >= rm->rm_cols - raidz_parity(rm))
					continue;

				tgtidx[1] = x0 + raidz_parity(rm);
				tgtidx[2] = x1 + raidz_parity(rm);

				if (rto_opts.rto_v >= D_INFO) {
					PRINT("[%d %d] ", x0, x1);
				}

				corrupt_colums(rm, tgtidx+1, 2);

				if (!rto_opts.rto_sanity)
					vdev_raidz_reconstruct(rm, tgtidx, 3);

				err += cmp_data(rm_golden, rm);
				if (cmp_data(rm_golden, rm)) {
					err++;
					if (rto_opts.rto_v >= D_INFO)
						PRINT("\nREC D[%d %d]... "
						    "[FAIL]\n", x0, x1);
				}
			}
		}
	} else {
		/* can reconstruct 3 failed data disk */
		for (x0 = 0;
			x0 < rto_opts.rto_dcols; x0++) {
			if (x0 >= rm->rm_cols - raidz_parity(rm))
				continue;
			for (x1 = x0 + 1;
				x1 < rto_opts.rto_dcols; x1++) {
				if (x1 >= rm->rm_cols - raidz_parity(rm))
					continue;
				for (x2 = x1 + 1;
					x2 < rto_opts.rto_dcols; x2++) {
					if (x2 >=
						rm->rm_cols - raidz_parity(rm))
						continue;

					tgtidx[0] = x0 + raidz_parity(rm);
					tgtidx[1] = x1 + raidz_parity(rm);
					tgtidx[2] = x2 + raidz_parity(rm);

					if (rto_opts.rto_v >= D_INFO) {
						PRINT("[%d %d %d]", x0, x1, x2);
					}

					corrupt_colums(rm, tgtidx, 3);

					if (!rto_opts.rto_sanity)
						vdev_raidz_reconstruct(rm,
							tgtidx, 3);

					if (cmp_data(rm_golden, rm)) {
						err++;

						if (rto_opts.rto_v >= D_INFO)
							PRINT("\nREC "
							    "D[%d %d %d]... "
							    "[FAIL]\n",
							    x0, x1, x2);
					}
				}
			}
		}
	}
	return (err);
}

int
run_rec_check(void)
{
	char **impl_name;
	unsigned fn, err = 0;
	zio_t *zio_test;
	raidz_map_t *rm_test;

	init_raidz_golden_map(PARITY_PQR);

	PRINT(DBLSEP);
	PRINT("Testing data reconstruction...\n");

	for (impl_name = (char **)raidz_impl_names; *impl_name != NULL;
	    impl_name++) {

		PRINT(SEP);
		PRINT("\tTesting [%s] implementation...", *impl_name);

		if (0 != zfs_raidz_math_impl_set(*impl_name, NULL)) {
			PRINT("[SKIP]\n");
			continue;
		} else {
			PRINT("[SUPPORTED]\n");
		}

		/* create suitable raidz_map */
		rm_test = init_raidz_map(&zio_test, PARITY_PQR);
		/* generate parity */
		vdev_raidz_generate_parity(rm_test);

		for (fn = 0; fn < RAIDZ_REC_NUM; fn++) {

			PRINT("\t\tTesting method [%s] ...",
				raidz_rec_name[fn]);

			if (0 == __run_rec_check(rm_test, fn)) {
				PRINT("[PASS]\n");
			} else {
				err++;
				PRINT("[FAIL]\n");
			}
		}
		/* tear down test raidz_map */
		fini_raidz_map(&zio_test, &rm_test);
	}

	fini_raidz_map(&zio_golden, &rm_golden);

	return (err);
}

static int
run_test(void)
{
	int err = 0;

	print_opts();

	err |= run_gen_check();
	err |= run_rec_check();

	return (err);
}

int
main(int argc, char **argv)
{
	size_t zashift, zoffset, dcols, dcsize;
	struct sigaction action;

	/* init pmd string early */
	(void) sprintf(gdb, gdb_tmpl, getpid());

	action.sa_handler = sig_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;

	if (sigaction(SIGSEGV, &action, NULL) < 0) {
		(void) fprintf(stderr, "ztest: cannot catch SIGSEGV: %s.\n",
		    strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (sigaction(SIGABRT, &action, NULL) < 0) {
		(void) fprintf(stderr, "ztest: cannot catch SIGABRT: %s.\n",
		    strerror(errno));
		exit(EXIT_FAILURE);
	}

	(void) setvbuf(stdout, NULL, _IOLBF, 0);

	dprintf_setup(&argc, argv);

	process_options(argc, argv);

	kernel_init(FREAD);

	if (rto_opts.rto_benchmark) {
		run_raidz_benchmark();
		return (0);
	}

	if (rto_opts.rto_sweep) {
		for (dcols = MIN_DCOLS;
			dcols <= MAX_DCOLS;
			dcols++) {
			rto_opts.rto_dcols = dcols;
			for (zashift = MIN_ASHIFT;
				zashift <= MAX_ASHIFT;
				zashift += 3) {
				rto_opts.rto_ashift = zashift;
				for (zoffset = MIN_OFFSET;
					zoffset <= MAX_OFFSET;
					zoffset += 1) {

					rto_opts.rto_offset = 1ULL << zoffset;
					if (rto_opts.rto_offset < 512)
						rto_opts.rto_offset = 0;

					for (dcsize = MIN_DCSIZE;
						dcsize <= MAX_DCSIZE;
						dcsize++) {
						rto_opts.rto_dsize =
							1ULL << dcsize;

						if ((dcsize <
							rto_opts.rto_ashift) ||
							(rto_opts.rto_dsize <
							rto_opts.rto_offset))
							continue;

						if (0 != run_test()) {
							PRINT("\nParameter "
							"sweep test [FAIL]\n"
							"Exiting...\n");
							exit(-1);
						}
					}
				}
			}
		}
	} else {
		return (run_test());
	}

	return (0);
}
