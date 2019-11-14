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
#if HAVE_ENDIAN_H
# include <endian.h>
#else
# include <sys/endian.h>
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

/*
 * Return TRUE if the current role has the ability to open or close the
 * given close (or no roles are specified), FALSE if otherwise.
 */
static int
sqlbox_rolecheck_src(struct sqlbox *box, size_t idx)
{
	size_t	 i;

	if (box->cfg.roles.rolesz == 0)
		return 1;
	for (i = 0; i < box->cfg.roles.roles[box->role].srcsz; i++)
		if (box->cfg.roles.roles[box->role].srcs[i] == idx)
			return 1;
	sqlbox_warnx(&box->cfg, "close: source %zu "
		"denied to role %zu", idx, box->role);
	return 0;
}

int
sqlbox_close(struct sqlbox *box, size_t src)
{
	uint32_t	 v = htole32(src);

	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_CLOSE, (char *)&v, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "close: sqlbox_write_frame");
		return 0;
	}
	return 1;
}

/*
 * Close a database.
 * First check if the identifier is valid, then whether our role permits
 * closing databases.
 * Returns TRUE on success or FALSE on failure (depending on failure,
 * source may be removed).
 */
int
sqlbox_op_close(struct sqlbox *box, const char *buf, size_t sz)
{
	struct sqlbox_db *db;
	int		  rc = 0;

	/* Check source exists and we can close it. */

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "close: "
			"bad frame size: %zu", sz);
		return 0;
	}
	if ((db = sqlbox_db_find
	    (box, le32toh(*(uint32_t *)buf))) == NULL) {
		sqlbox_warnx(&box->cfg, "close: sqlbox_db_find");
		return 0;
	}
	if (!sqlbox_rolecheck_src(box, db->idx)) {
		sqlbox_warnx(&box->cfg, "%s: close: "
			"sqlbox_rolecheck_src", db->src->fname);
		return 0;
	}

	/* 
	 * Make sure we're ready to close.
	 * For the time being, we simply check if we have any open
	 * statements.
	 * If we do, exit: our main exit handler will finalise these
	 * statements for us.
	 */

	if (!TAILQ_EMPTY(&db->stmtq)) {
		sqlbox_warnx(&box->cfg, "%s: close: source %zu "
			"(id %zu) has unfinalised statements", 
			db->src->fname, db->idx, db->id);
		return 0;
	} else if (db->trans) {
		sqlbox_warnx(&box->cfg, "%s: transaction "
			"%zu still open on exit (auto rollback)", 
			db->src->fname, db->trans);
		return 0;
	}

	/* 
	 * Remove from queue so we don't double close, but let the
	 * underlying close have us error out if it fails.
	 */

	TAILQ_REMOVE(&box->dbq, db, entries);
	sqlbox_debug(&box->cfg, "sqlite3_close: %s", db->src->fname);
	if (sqlite3_close(db->db) != SQLITE_OK)
		sqlbox_warnx(&box->cfg, "%s: close: %s", 
			db->src->fname, sqlite3_errmsg(db->db));
	else
		rc = 1;

	free(db);
	return rc;
}

