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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../sqlbox.h"
#include "regress.h"

int
main(int argc, char *argv[])
{
	size_t		 	 dbid, stmtid;
	struct sqlbox		*p;
	struct sqlbox_cfg	 cfg;
	struct sqlbox_src	 srcs[] = {
		{ .fname = (char *)":memory:",
		  .mode = SQLBOX_SRC_RW }
	};
	struct sqlbox_pstmt	 pstmts[] = {
		{ .stmt = (char *)"CREATE TABLE foo (bar INTEGER)" },
		{ .stmt = (char *)"INSERT INTO foo (bar) VALUES (?)" },
		{ .stmt = (char *)"SELECT * FROM foo" }
	};
	struct sqlbox_parm	 parms1[] = {
		{ .iparm = INT64_MIN,
		  .type = SQLBOX_PARM_INT },
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
	if (!(stmtid = sqlbox_prepare_bind
	      (p, dbid, 1, nitems(parms1), parms1, 0)))
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
	if (res->psz != 1)
		errx(EXIT_FAILURE, "res->psz != 1");
	if (res->ps[0].type != SQLBOX_PARM_INT)
		errx(EXIT_FAILURE, "res->ps[0].type != SQLBOX_PARM_INT");

	/* The original size should be ignored. */

	if (res->ps[0].sz != sizeof(int64_t))
		errx(EXIT_FAILURE, "res->ps[0].sz != %zu", sizeof(int64_t));
	if (res->ps[0].iparm != INT64_MIN)
		errx(EXIT_FAILURE, "res->ps[0].iparm != INT64_MIN");

	if (!sqlbox_finalise(p, stmtid))
		errx(EXIT_FAILURE, "sqlbox_finalise");
	if (!sqlbox_ping(p))
		errx(EXIT_FAILURE, "sqlbox_ping");
	if (!sqlbox_close(p, dbid))
		errx(EXIT_FAILURE, "sqlbox_close");
	if (!sqlbox_ping(p))
		errx(EXIT_FAILURE, "sqlbox_ping");

	sqlbox_free(p);
	return EXIT_SUCCESS;
}
