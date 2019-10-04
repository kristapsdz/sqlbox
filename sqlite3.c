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
#include "config.h"

#if HAVE_SYS_QUEUE
# include <sys/queue.h>
#endif 

#include <assert.h>
#include <stdlib.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

/* 
 * Actually prepare a statement "pst".
 * In the usual way we sleep if SQLite gives us a busy, locked, or weird
 * protocol error.
 * All other errors are real errorrs.
 * Returns the statement or NULL on failure.
 */
sqlite3_stmt *
sqlbox_wrap_prepare(struct sqlbox *box, struct sqlbox_db *db,
	const struct sqlbox_pstmt *pst)
{
	sqlite3_stmt	*stmt;
	int		 c;
	size_t		 attempt = 0;

	assert(pst != NULL && pst->stmt != NULL);
again:
	stmt = NULL;
	sqlbox_debug(&box->cfg, "%s: sqlite3_prepare_v2: %s",
		db->src->fname, pst->stmt);
	c = sqlite3_prepare_v2(db->db, pst->stmt, -1, &stmt, NULL);

	switch (c) {
	case SQLITE_BUSY:
	case SQLITE_LOCKED:
	case SQLITE_PROTOCOL:
		sqlbox_sleep(attempt++);
		goto again;
	case SQLITE_OK:
		break;
	default:
		sqlbox_warnx(&box->cfg, "%s: sqlite3_prepare_v2: %s", 
			db->src->fname, sqlite3_errmsg(db->db));
		sqlbox_warnx(&box->cfg, "%s: statement: %s", 
			db->src->fname, pst->stmt);
		if (stmt == NULL)
			return NULL;
		sqlbox_debug(&box->cfg, "%s: sqlite3_finalize: %s",
			db->src->fname, pst->stmt);
		sqlite3_finalize(stmt);
		return NULL;
	}

	assert(stmt != NULL);
	return stmt;
}
