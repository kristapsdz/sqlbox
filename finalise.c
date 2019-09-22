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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

int
sqlbox_finalise(struct sqlbox *box, size_t id)
{
	uint32_t	 	 v = htole32(id);
	struct sqlbox_stmt	*st;

	/*
	 * The client maintains a queue of active statements that hold
	 * the last result set.
	 */

	if ((st = sqlbox_stmt_find(box, id)) == NULL) {
		sqlbox_warnx(&box->cfg, "finalise: bad stmt %zu", id);
		return 0;
	}
	TAILQ_REMOVE(&box->stmtq, st, gentries);
	sqlbox_stmt_free(st);

	/* Now pass to the server. */

	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_FINAL, (char *)&v, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "finalise: sqlbox_write_frame");
		return 0;
	}

	return 1;
}

int
sqlbox_op_finalise(struct sqlbox *box, const char *buf, size_t sz)
{
	struct sqlbox_stmt	*st;
	size_t			 id;

	/* Look up the statement in our global list. */

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "finalise: "
			"bad frame size: %zu", sz);
		return 0;
	}
	id = le32toh(*(uint32_t *)buf);
	if ((st = sqlbox_stmt_find(box, id)) == NULL) {
		sqlbox_warnx(&box->cfg, "finalise: bad stmt: %zu", id);
		return 0;
	}
	TAILQ_REMOVE(&box->stmtq, st, gentries);
	TAILQ_REMOVE(&st->db->stmtq, st, entries);
	sqlbox_debug(&box->cfg, "sqlite3_finalize: %s, %s",
		st->db->src->fname, st->pstmt->stmt);
	sqlite3_finalize(st->stmt);
	sqlbox_stmt_free(st);
	return 1;
}
