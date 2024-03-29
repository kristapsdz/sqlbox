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
#include "../config.h"

#if HAVE_ERR
# include <err.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "../sqlbox.h"
#include "regress.h"

int
main(int argc, char *argv[])
{
	size_t		 	 dbid, stmtid, i;
	char			*buf1, *buf2;
	struct sqlbox		*p;
	struct sqlbox_cfg	 cfg;
	struct sqlbox_src	 srcs[] = {
		{ .fname = (char *)":memory:",
		  .mode = SQLBOX_SRC_RW }
	};
	struct sqlbox_pstmt	 pstmts[] = {
		{ .stmt = (char *)"CREATE TABLE foo "
			"(col1 TEXT, col2 TEXT)" },
		{ .stmt = (char *)"INSERT INTO foo "
			"(col1, col2) VALUES (?, ?)" },
		{ .stmt = (char *)"SELECT * FROM foo" }
	};
	struct sqlbox_parm	 parms[] = {
		{ .type = SQLBOX_PARM_STRING },
		{ .type = SQLBOX_PARM_STRING },
	};
	const struct sqlbox_parmset *res;

	memset(&cfg, 0, sizeof(struct sqlbox_cfg));
	cfg.msg.func_short = warnx;
	cfg.srcs.srcsz = nitems(srcs);
	cfg.srcs.srcs = srcs;
	cfg.stmts.stmtsz = nitems(pstmts);
	cfg.stmts.stmts = pstmts;

	if ((p = sqlbox_alloc(&cfg)) == NULL)
		errx(EXIT_FAILURE, "sqlbox_alloc");
	if (!(dbid = sqlbox_open(p, 0)))
		errx(EXIT_FAILURE, "sqlbox_open");
	if (!sqlbox_ping(p))
		errx(EXIT_FAILURE, "sqlbox_ping");

	if (!(stmtid = sqlbox_prepare_bind(p, dbid, 0, 0, NULL, 0)))
		errx(EXIT_FAILURE, "sqlbox_prepare_bind");
	if ((res = sqlbox_step(p, stmtid)) == NULL)
		errx(EXIT_FAILURE, "sqlbox_step");
	if (res->psz != 0)
		errx(EXIT_FAILURE, "res->psz != 0");
	if (!sqlbox_finalise(p, stmtid))
		errx(EXIT_FAILURE, "sqlbox_finalise");

	parms[0].sz = 1024 * 1024;
	parms[1].sz = 1000 * 1024;

	if ((buf1 = calloc(1, parms[0].sz)) == NULL)
		err(EXIT_FAILURE, "malloc");
	if ((buf2 = calloc(1, parms[1].sz)) == NULL)
		err(EXIT_FAILURE, "malloc");

	parms[0].sparm = buf1;
	parms[1].sparm = buf2;

#if HAVE_ARC4RANDOM
	for (i = 0; i < parms[0].sz - 1; i++)
		buf1[i] = arc4random_uniform(26) + 65;
	for (i = 0; i < parms[1].sz - 1; i++)
		buf2[i] = arc4random_uniform(26) + 65;
#else
	for (i = 0; i < parms[0].sz - 1; i++)
		buf1[i] = (random() % 26) + 65;
	for (i = 0; i < parms[1].sz - 1; i++)
		buf2[i] = (random() % 26) + 65;
#endif

	if (!(stmtid = sqlbox_prepare_bind
	      (p, dbid, 1, nitems(parms), parms, 0)))
		errx(EXIT_FAILURE, "sqlbox_prepare_bind");
	if ((res = sqlbox_step(p, stmtid)) == NULL)
		errx(EXIT_FAILURE, "sqlbox_step");
	if (res->psz != 0)
		errx(EXIT_FAILURE, "res->psz != 0");
	if (!sqlbox_finalise(p, stmtid))
		errx(EXIT_FAILURE, "sqlbox_finalise");

	if (!(stmtid = sqlbox_prepare_bind(p, dbid, 2, 0, NULL, 0)))
		errx(EXIT_FAILURE, "sqlbox_prepare_bind");
	if ((res = sqlbox_step(p, stmtid)) == NULL)
		errx(EXIT_FAILURE, "sqlbox_step");
	if (res->psz != 2)
		errx(EXIT_FAILURE, "res->psz != 2");
	if (res->ps[0].type != SQLBOX_PARM_STRING)
		errx(EXIT_FAILURE, "res->ps[0].type != SQLBOX_PARM_STRING");
	if (res->ps[1].type != SQLBOX_PARM_STRING)
		errx(EXIT_FAILURE, "res->ps[1].type != SQLBOX_PARM_STRING");

	if (res->ps[0].sz != parms[0].sz)
		errx(EXIT_FAILURE, "res->ps[0].sz != parms[0].sz");
	if (strcmp(res->ps[0].sparm, parms[0].sparm))
		errx(EXIT_FAILURE, "res->ps[0].sparm != parms[]0].sparm");
	if (res->ps[1].sz != parms[1].sz)
		errx(EXIT_FAILURE, "res->ps[1].sz != parms[1].sz");
	if (strcmp(res->ps[1].sparm, parms[1].sparm))
		errx(EXIT_FAILURE, "res->ps[0].sparm != parms[1].sparm");

	if (!sqlbox_finalise(p, stmtid))
		errx(EXIT_FAILURE, "sqlbox_finalise");
	if (!sqlbox_ping(p))
		errx(EXIT_FAILURE, "sqlbox_ping");
	if (!sqlbox_close(p, dbid))
		errx(EXIT_FAILURE, "sqlbox_close");
	if (!sqlbox_ping(p))
		errx(EXIT_FAILURE, "sqlbox_ping");

	sqlbox_free(p);
	free(buf1);
	free(buf2);
	return EXIT_SUCCESS;
}
