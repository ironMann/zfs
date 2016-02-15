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
#include <sys/vdev_raidz_impl.h>
#include <stdio.h>
#ifdef __GNUC__
#include <execinfo.h> /* for backtrace() */
#endif
#include "raidz_test.h"

#define	BACKTRACE_SZ	100

static void sig_handler(int signo)
{
	struct sigaction action;
#ifdef __GNUC__ /* backtrace() is a GNU extension */
	int nptrs;
	void *buffer[BACKTRACE_SZ];

	nptrs = backtrace(buffer, BACKTRACE_SZ);
	backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);
#endif

	/*
	 * Restore default action and re-raise signal so SIGSEGV and
	 * SIGABRT can trigger a core dump.
	 */
	action.sa_handler = SIG_DFL;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	(void) sigaction(signo, &action, NULL);
	raise(signo);
}

raidz_test_opts_t rto_opts;

static void print_opts(void)
{
	char *verbose;
	switch (rto_opts.rto_verbose) {
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
	"  (-a) zio ashift              : %zu\n"
	"  (-o) zio offset              : 1 << %zu\n"
	"  (-d) number of DATA columns  : %zu\n"
	"  (-s) size of DATA column     : 1 << %zu\n"
	"  (-S) sweep parameters        : %s \n"
	"  (-v) verbose                 : %s \n\n",
	rto_opts.rto_ashift,				/* -a */
	ilog2(rto_opts.rto_offset),			/* -o */
	rto_opts.rto_dcols,					/* -d */
	ilog2(rto_opts.rto_dcolsize),		/* -s */
	rto_opts.rto_sweep ? "yes" : "no",	/* -S */
	verbose 							/* -v */
	);
}

static void usage(boolean_t requested)
{
	const raidz_test_opts_t *o = &rto_opts_defaults;

	FILE *fp = requested ? stdout : stderr;

	(void) fprintf(fp, "Usage:\n"
	"\t[-a zio ashift (default: %zu)]\n"
	"\t[-o zio offset, exponent radix 2 (default: %zu)]\n"
	"\t[-d number of DATA columns (default: %zu)]\n"
	"\t[-s size of single DATA column, exponent radix 2 (default: %zu)]\n"
	"\t[-S parameter space sweep (default: %s)]\n"
	"\t[-B benchmark all raidz implementations]\n"
	"\t[-v increase verbosity (default: %zu)]\n"
	"\t[-h] (print help)\n"
	"",
	o->rto_ashift,						/* -a */
	ilog2(o->rto_offset),				/* -o */
	o->rto_dcols,						/* -d */
	ilog2(o->rto_dcolsize),				/* -s */
	rto_opts.rto_sweep ? "yes" : "no",	/* -S */
	o->rto_verbose						/* -d */
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
	    "BSvha:o:d:s:")) != EOF) {
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
			o->rto_dcolsize = 1ULL <<  MIN(24, MAX(9, value));
			break;
		case 'v':
			o->rto_verbose++;
			break;
		case 'S':
			o->rto_sweep++;
			break;
		case 'B':
			o->rto_benchmark++;
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

static zio_t zio_golden;
static zio_t zio_test;
static raidz_map_t *rm_golden;
static raidz_map_t *rm_test;

#define	D_INFO 1
#define	D_DEBUG 2

#define	DATA_COL(rm, i) ((rm)->rm_col[raidz_parity(rm) + (i)].rc_data)
#define	DATA_COL_SIZE(rm, i) ((rm)->rm_col[raidz_parity(rm) + (i)].rc_size)

#define	CODE_COL(rm, i) ((rm)->rm_col[(i)].rc_data)
#define	CODE_COL_SIZE(rm, i) ((rm)->rm_col[(i)].rc_size)

static int
compare_raidz_maps_code(const raidz_map_t *rma, const raidz_map_t *rmb,
	const int code)
{
	int i, eret = 0;

	for (i = 0; i < code; i++) {
		if (0 != memcmp(CODE_COL(rma, i), CODE_COL(rmb, i),
			CODE_COL_SIZE(rma, i))) {
			eret++;
			if (rto_opts.rto_verbose > D_INFO) {
				PRINT("\nparity block [%d] different!\n", i);
			}
		}
	}
	return (eret);
}

static int
compare_raidz_maps_data(raidz_map_t *rm_golden, raidz_map_t *rm)
{
	int i, j, eret = 0;

	for (i = 0; i < rto_opts.rto_dcols; i++) {
		if (0 != memcmp(DATA_COL(rm_golden, i), DATA_COL(rm, i),
			DATA_COL_SIZE(rm_golden, i))) {
			eret++;
			if (rto_opts.rto_verbose > D_INFO) {
				PRINT("data block [%d] different!\n", i);
			}
			if (rto_opts.rto_verbose >= D_DEBUG) {
				PRINT("golden: data blocks [%d]:\n", i);
				for (j = 0; j < 16; j++)
					PRINT("%.2x ",
					((uint8_t *)DATA_COL(rm_golden, i))[j]);
				PRINT("\ntest: data blocks [%d]:\n", i);
				for (j = 0; j < 16; j++)
					PRINT("%.2x ",
					((uint8_t *)DATA_COL(rm, i))[j]);
				PRINT("\n");
			}
		}
	}
	return (eret);
}

static void
corrupt_raidz_map_data_colum(raidz_map_t *rm, int col)
{
	size_t i;

	srand(getpid() - 1);

	for (i = 0; i < DATA_COL_SIZE(rm, col) / sizeof (int); i++) {
		((int *)(DATA_COL(rm, col)))[i] = rand();
	}
}

void
init_zio_data(zio_t *zio)
{
	size_t i;

	srand(getpid());

	for (i = 0; i < zio->io_size / sizeof (int); i++) {
		((int *)(zio->io_data))[i] = rand();
	}
}

static void
init_raidz_maps(void)
{
	unsigned err = 0;
	const size_t alloc_dsize = rto_opts.rto_dcolsize * rto_opts.rto_dcols;
	const size_t total_ncols = rto_opts.rto_dcols + CODE_PQR;

	PRINT(DBLSEP);
	PRINT("initializing the golden copy ... ");

	zio_golden.io_offset = zio_test.io_offset = 0;
	zio_golden.io_size = zio_test.io_size = alloc_dsize;

	/*
	 * To permit larger column sizes these have to be done
	 * allocated using aligned alloc instead of zio_data_buf_alloc
	 */
	zio_golden.io_data = umem_alloc_aligned(alloc_dsize, 32, UMEM_NOFAIL);
	zio_test.io_data = umem_alloc_aligned(alloc_dsize, 32, UMEM_NOFAIL);

	init_zio_data(&zio_golden);
	init_zio_data(&zio_test);

	zfs_raidz_math_impl_set("original", NULL);

	rm_golden = vdev_raidz_map_alloc(&zio_golden, rto_opts.rto_ashift,
		total_ncols, CODE_PQR);
	rm_test = vdev_raidz_map_alloc(&zio_test, rto_opts.rto_ashift,
		total_ncols, CODE_PQR);

	vdev_raidz_generate_parity(rm_golden);
	vdev_raidz_generate_parity(rm_test);

	/* sanity check */
	err |= compare_raidz_maps_data(rm_test, rm_golden);
	err |= compare_raidz_maps_code(rm_test, rm_golden, CODE_PQR);

	if (!err) {
		PRINT("SUCCESS!\n");
	} else {
		PRINT("ERROR!\n");
		exit(-1);
	}

	/* tear down raidz_map of test zio */
	vdev_raidz_map_free(rm_test);
}

static void
fini_raidz_maps(void)
{
	/* tear down golden zio */
	vdev_raidz_map_free(rm_golden);
	umem_free(zio_golden.io_data, zio_golden.io_size);
	bzero(&zio_golden, sizeof (zio_t));

	/* tear down test zio */
	umem_free(zio_test.io_data, zio_test.io_size);
	bzero(&zio_test, sizeof (zio_t));
}


static int
run_gen_check(void)
{
	int impl, fn;
	raidz_math_ops_t *c_ops;
	int err = 0;

	PRINT(DBLSEP);
	PRINT("Testing parity generation...\n");

	for (impl = 0; impl < raidz_supp_maths_cnt; impl++) {
		c_ops = raidz_supp_maths[impl];

	PRINT(SEP);
		PRINT("\tTesting [%s] implementation...\n",
			c_ops->name);


		for (fn = 0; fn < RAIDZ_GEN_NUM; fn++) {

			/* create suitable raidz_map */
			rm_test = vdev_raidz_map_alloc(&zio_test,
				rto_opts.rto_ashift, rto_opts.rto_dcols + fn+1,
				fn+1);

			PRINT("\t\tTesting method [%s] ...",
				raidz_gen_name[fn]);

			c_ops->gen[fn](rm_test);

			if (0 == compare_raidz_maps_code(rm_test, rm_golden,
				fn)) {
				PRINT("SUCCESS\n");
			} else {
				PRINT("ERROR!\n");
				err++;
			}

			vdev_raidz_map_free(rm_test);
		}
	}

	return (err);
}

unsigned
__run_rec_check(raidz_map_t *rm, const raidz_math_ops_t *impl,
	const int fn)
{
	int x0, x1, x2;
	int tgtidx[3];
	int err = 0;

	if (fn < RAIDZ_REC_PQ) {
		/* can reconstruct 1 failed data disk */
		for (x0 = 0; x0 < rto_opts.rto_dcols; x0++) {
			tgtidx[0] = x0 + raidz_parity(rm);

			if (rto_opts.rto_verbose >= D_INFO) {
				PRINT("[%d] ", x0);
			}

			corrupt_raidz_map_data_colum(rm, x0);

			impl->rec[fn](rm, tgtidx);

			if (compare_raidz_maps_data(rm_golden, rm_test)) {
				err++;
				init_zio_data(&zio_test);

				PRINT("\nReconstruction of data block [%d]"
					" FAILED!\n", x0);
			}
		}

	} else if (fn < RAIDZ_REC_PQR) {
		/* can reconstruct 2 failed data disk */
		for (x0 = 0; x0 < rto_opts.rto_dcols; x0++) {
			for (x1 = x0 + 1; x1 < rto_opts.rto_dcols; x1++) {
				tgtidx[0] = x0 + raidz_parity(rm);
				tgtidx[1] = x1 + raidz_parity(rm);

				if (rto_opts.rto_verbose >= D_INFO) {
					PRINT("[%d %d] ",
						x0, x1);
				}

				corrupt_raidz_map_data_colum(rm, x0);
				corrupt_raidz_map_data_colum(rm, x1);

				impl->rec[fn](rm, tgtidx);

				err += compare_raidz_maps_data(rm_golden,
					rm_test);
				if (compare_raidz_maps_data(rm_golden,
					rm_test)) {
					err++;
					init_zio_data(&zio_test);

					(void) fprintf(stderr,
						"\nReconstruction of data "
						"blocks [%d %d] FAILED!\n",
						x0, x1);
				}
			}
		}
	} else {
		/* can reconstruct 3 failed data disk */
		for (x0 = 0;
			x0 < rto_opts.rto_dcols; x0++) {
			for (x1 = x0 + 1;
				x1 < rto_opts.rto_dcols; x1++) {
				for (x2 = x1 + 1;
					x2 < rto_opts.rto_dcols; x2++) {
					tgtidx[0] = x0 + raidz_parity(rm);
					tgtidx[1] = x1 + raidz_parity(rm);
					tgtidx[2] = x2 + raidz_parity(rm);

					if (rto_opts.rto_verbose >= D_INFO) {
						PRINT("[%d %d %d]",
							x0, x1, x2);
					}

					corrupt_raidz_map_data_colum(rm, x0);
					corrupt_raidz_map_data_colum(rm, x1);
					corrupt_raidz_map_data_colum(rm, x2);

					impl->rec[fn](rm, tgtidx);

					if (compare_raidz_maps_data(rm_golden,
						rm_test)) {
						err++;
						init_zio_data(&zio_test);

						PRINT("\nReconstruction of "
							"data blocks [%d %d %d]"
							" FAILED!\n",
							x0, x1, x2);
					}
				}
			}
		}
	}
	return (err);
}

static int
run_rec_check(void)
{
	int impl, fn;
	raidz_math_ops_t *c_ops;
	unsigned err = 0;

	PRINT(DBLSEP);
	PRINT("Testing data reconstruction...\n");

	/* create suitable raidz_map */
	rm_test = vdev_raidz_map_alloc(&zio_test, rto_opts.rto_ashift,
		rto_opts.rto_dcols + CODE_PQR, CODE_PQR);
	/* generate parity */
	vdev_raidz_generate_parity(rm_test);

	for (impl = 0; impl < raidz_supp_maths_cnt; impl++) {
		c_ops = raidz_supp_maths[impl];

		PRINT(SEP);
		PRINT("\tTesting [%s] implementation...\n", c_ops->name);

		for (fn = 0; fn < RAIDZ_REC_NUM; fn++) {

			PRINT("\t\tTesting method [%s] ...",
				raidz_rec_name[fn]);

			if (0 == __run_rec_check(rm_test, c_ops, fn)) {
				PRINT("SUCCESS\n");
			} else {
				err++;
				PRINT("ERROR!\n");
			}
		}
	}

	/* tear down test raidz_map */
	vdev_raidz_map_free(rm_test);

	return (err);
}

static int
run_test(void)
{
	int err = 0;

	print_opts();
	init_raidz_maps();

	err |= run_gen_check();
	err |= run_rec_check();

	fini_raidz_maps();

	return (err);
}


int
main(int argc, char **argv)
{
	size_t zashift, zoffset, dcols, dcsize;
	struct sigaction action;

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
						rto_opts.rto_dcolsize =
							1ULL << dcsize;

						if ((dcsize <
							rto_opts.rto_ashift) ||
							(rto_opts.rto_dcolsize <
							rto_opts.rto_offset))
							continue;

						if (0 != run_test()) {
							PRINT("\n Parameter "
							"sweep test FAILED! "
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
