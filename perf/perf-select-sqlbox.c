/*	$Id$ */
/*
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "perf.h"
#include "../sqlbox.h"

int
main(int argc, char *argv[])
{
	size_t		 	 i, rows = 10000;
	struct sqlbox		*p;
	struct sqlbox_cfg	 cfg;
	int			 c;
	struct sqlbox_src	 srcs[] = {
		{ .fname = (char *)":memory:",
		  .mode = SQLBOX_SRC_RW }
	};
	struct sqlbox_pstmt	 pstmts[] = {
		{ .stmt = (char *)"CREATE TABLE foo (col INT)" },
		{ .stmt = (char *)"INSERT INTO foo "
			"WITH RECURSIVE cte(x) AS "
			"(SELECT random() UNION ALL"
			" SELECT random() FROM cte LIMIT ?"
		 	") SELECT abs(x) FROM cte;" },
		{ .stmt = (char *)"SELECT col FROM foo" },
	};
	struct sqlbox_parm	 parm = {
		.type = SQLBOX_PARM_INT
	};
	const struct sqlbox_parmset *res;

	if (pledge("stdio rpath cpath wpath flock fattr proc", NULL) == -1)
		err(EXIT_FAILURE, "pledge");

	while ((c = getopt(argc, argv, "n:")) != -1)
		switch (c) {
		case 'n':
			rows = atoi(optarg);
			break;
		default:
			return EXIT_FAILURE;
		}

	memset(&cfg, 0, sizeof(struct sqlbox_cfg));
	cfg.msg.func_short = warnx;

	cfg.srcs.srcsz = 1;
	cfg.srcs.srcs = srcs;
	cfg.stmts.stmtsz = 3;
	cfg.stmts.stmts = pstmts;

	parm.iparm = rows;
	printf(">>> %zu insertions\n", rows);

	if ((p = sqlbox_alloc(&cfg)) == NULL)
		errx(EXIT_FAILURE, "sqlbox_alloc");
	if (pledge("stdio", NULL) == -1)
		err(EXIT_FAILURE, "pledge");
	if (!sqlbox_open_async(p, 0))
		errx(EXIT_FAILURE, "sqlbox_open");
	if (!sqlbox_exec_async(p, 0, 0, 0, NULL, 0))
		errx(EXIT_FAILURE, "sqlbox_exec_async");
	if (!sqlbox_exec_async(p, 0, 1, 1, &parm, 0))
		errx(EXIT_FAILURE, "sqlbox_exec_async");

	if (!sqlbox_prepare_bind_async(p, 0, 2, 0, NULL, 0))
		errx(EXIT_FAILURE, "sqlbox_prepare_bind_async");

	for (i = 0; i < rows; i++) {
		if ((res = sqlbox_step(p, 0)) == NULL)
			errx(EXIT_FAILURE, "sqlbox_step");
		if (res->psz != 1)
			errx(EXIT_FAILURE, "res->psz != 1");
		if (res->ps[0].type != SQLBOX_PARM_INT)
			errx(EXIT_FAILURE, "res->ps[0] type");
		if (res->ps[0].iparm < 0)
			errx(EXIT_FAILURE, "negative!?");
	}

	if ((res = sqlbox_step(p, 0)) == NULL)
		errx(EXIT_FAILURE, "sqlbox_step");
	if (res->psz != 0)
		errx(EXIT_FAILURE, "res->psz != 0");
	if (!sqlbox_finalise(p, 0))
		errx(EXIT_FAILURE, "sqlbox_finalise");
	if (!sqlbox_close(p, 0))
		errx(EXIT_FAILURE, "sqlbox_close");

	sqlbox_free(p);
	puts("<<< done");
	return EXIT_SUCCESS;
}
