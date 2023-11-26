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
#include <stdio.h>
#include <string.h>

#include "../sqlbox.h"
#include "regress.h"

int
main(int argc, char *argv[])
{
	size_t		 	 dbid, stmtid, parmsz = 900,
				 i, buf1sz, buf2sz;
	char			 tmpbuf[1024];
	char			*buf1, *buf2;
	struct sqlbox		*p;
	struct sqlbox_cfg	 cfg;
	struct sqlbox_src	 srcs[] = {
		{ .fname = (char *)":memory:",
		  .mode = SQLBOX_SRC_RW }
	};
	struct sqlbox_pstmt	 pstmts[] = {
		{ .stmt = NULL },
		{ .stmt = NULL },
		{ .stmt = (char *)"SELECT * FROM foo" }
	};
	struct sqlbox_parm	*parms;
	const struct sqlbox_parmset *res;

	/* 
	 * Allocate "create table" with enough space for all columns
	 * consisting of "colxxxx INT".
	 */

	buf1sz = 64 + (parmsz * 14);
	if ((buf1 = calloc(buf1sz, 1)) == NULL)
		err(EXIT_FAILURE, "calloc");
	strlcat(buf1, "CREATE TABLE foo (", buf1sz);
	for (i = 0; i < parmsz; i++) {
		snprintf(tmpbuf, sizeof(tmpbuf), "col%.5zu INT", i);
		strlcat(buf1, tmpbuf, buf1sz);
		if (i < parmsz - 1)
			strlcat(buf1, ", ", buf1sz);
	}
	strlcat(buf1, ")", buf1sz);

	/* Now "insert into". */

	buf2sz = 64 + (parmsz * 12);
	if ((buf2 = calloc(buf2sz, 1)) == NULL)
		err(EXIT_FAILURE, "calloc");
	strlcat(buf2, "INSERT INTO foo (", buf2sz);
	for (i = 0; i < parmsz; i++) {
		snprintf(tmpbuf, sizeof(tmpbuf), "col%.5zu", i);
		strlcat(buf2, tmpbuf, buf2sz);
		if (i < parmsz - 1)
			strlcat(buf2, ", ", buf2sz);
	}
	strlcat(buf2, ") VALUES (", buf2sz);
	for (i = 0; i < parmsz; i++) {
		strlcat(buf2, "?", buf2sz);
		if (i < parmsz - 1)
			strlcat(buf2, ",", buf2sz);
	}
	strlcat(buf2, ")", buf2sz);

	pstmts[0].stmt = buf1;
	pstmts[1].stmt = buf2;

	/* Now our statements are ready. */

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

	/* ...and the parameters... */

	parms = calloc(parmsz, sizeof(struct sqlbox_parm));
	if (parms == NULL)
		err(EXIT_FAILURE, NULL);
	for (i = 0; i < parmsz; i++) {
		parms[i].type = SQLBOX_PARM_INT;
		parms[i].iparm = i;
	}

	if (!(stmtid = sqlbox_prepare_bind
	    (p, dbid, 1, parmsz, parms, 0)))
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
	if (res->psz != parmsz)
		errx(EXIT_FAILURE, "res->psz != parmsz");

	for (i = 0; i < parmsz; i++) {
		if (res->ps[i].type != SQLBOX_PARM_INT)
			errx(EXIT_FAILURE, 
				"res->ps[%zu].type != "
				"SQLBOX_PARM_INT", i);
		if (res->ps[i].iparm != (int64_t)i)
			errx(EXIT_FAILURE, 
				"res->ps[%zu].type != %zu", i, i);
	}

	if (!sqlbox_finalise(p, stmtid))
		errx(EXIT_FAILURE, "sqlbox_finalise");
	if (!sqlbox_ping(p))
		errx(EXIT_FAILURE, "sqlbox_ping");
	if (!sqlbox_close(p, dbid))
		errx(EXIT_FAILURE, "sqlbox_close");
	if (!sqlbox_ping(p))
		errx(EXIT_FAILURE, "sqlbox_ping");

	sqlbox_free(p);
	free(parms);
	free(buf1);
	free(buf2);
	return EXIT_SUCCESS;
}
