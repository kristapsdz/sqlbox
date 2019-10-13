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
sqlbox_wrap_prep(struct sqlbox *box, struct sqlbox_db *db,
	const struct sqlbox_pstmt *pst)
{
	sqlite3_stmt	*stmt;
	int		 c;
	size_t		 attempt = 0;

	assert(pst != NULL && pst->stmt != NULL);
	sqlbox_debug(&box->cfg, "%s: sqlite3_prepare_v2: %s",
		db->src->fname, pst->stmt);

again:
	stmt = NULL;
	c = sqlite3_prepare_v2(db->db, pst->stmt, -1, &stmt, NULL);

	switch (c) {
	case SQLITE_BUSY:
	case SQLITE_LOCKED:
	case SQLITE_PROTOCOL:
		sqlbox_wrap_finalise(box, db, pst, stmt);
		sqlbox_sleep(attempt++);
		goto again;
	case SQLITE_OK:
		assert(stmt != NULL);
		return stmt;
	default:
		break;
	}

	sqlbox_warnx(&box->cfg, "%s: sqlite3_prepare_v2: %s", 
		db->src->fname, sqlite3_errmsg(db->db));
	sqlbox_warnx(&box->cfg, "%s: statement: %s", 
		db->src->fname, pst->stmt);

	sqlbox_wrap_finalise(box, db, pst, stmt);
	return NULL;
}

/*
 * Step through statement.
 * Returns SQLBOX_CODE_OK on success, SQLBOX_CODE_CONSTRAINT if
 * allow_cstep is non-zero and there's a constraint violation, or
 * SQLBOX_CODE_ERROR otherwise.
 * If there are columns in the return of the SQL statement, this sets
 * "cols" but otherwise returns SQLBOX_CODE_OK.
 */
enum sqlbox_code
sqlbox_wrap_step(struct sqlbox *box, struct sqlbox_db *db,
	const struct sqlbox_pstmt *pst, sqlite3_stmt *stmt,
	size_t *cols, int allow_cstep)
{
	size_t	 attempt = 0;
	int	 ccount;

	*cols = 0;

	assert(pst != NULL && pst->stmt != NULL);
	sqlbox_debug(&box->cfg, "%s: sqlite3_step: %s",
		db->src->fname, pst->stmt);

again_step:
	switch (sqlite3_step(stmt)) {
	case SQLITE_BUSY:
		/*
		 * FIXME: according to sqlite3_step(3), this
		 * should return if we're in a transaction.
		 */
		sqlbox_sleep(attempt++);
		goto again_step;
	case SQLITE_LOCKED:
	case SQLITE_PROTOCOL:
		sqlbox_sleep(attempt++);
		goto again_step;
	case SQLITE_DONE:
		return SQLBOX_CODE_OK;
	case SQLITE_ROW:
		if ((ccount = sqlite3_column_count(stmt)) > 0) {
			*cols = (size_t)ccount;
			return SQLBOX_CODE_OK;
		}
		sqlbox_warnx(&box->cfg, "%s: sqlite3_step: "
			"row without columns", db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: statement: %s", 
			db->src->fname, pst->stmt);
		return SQLBOX_CODE_ERROR;
	case SQLITE_CONSTRAINT:
		if (allow_cstep)
			return SQLBOX_CODE_CONSTRAINT;
		/* FALLTHROUGH */
	default:
		break;
	}

	sqlbox_warnx(&box->cfg, "%s: sqlite3_step: %s", 
		db->src->fname, sqlite3_errmsg(db->db));
	sqlbox_warnx(&box->cfg, "%s: statement: %s", 
		db->src->fname, pst->stmt);
	return SQLBOX_CODE_ERROR;
}

/*
 * Log about and finalise the statement.
 * Does nothing if statement is NULL.
 */
void
sqlbox_wrap_finalise(struct sqlbox *box, struct sqlbox_db *db,
	const struct sqlbox_pstmt *pst, sqlite3_stmt *stmt)
{

	if (stmt == NULL)
		return;

	/* 
	 * This returns the last operation's error code, so we ignore it
	 * as it may have failed.
	 */

	sqlbox_debug(&box->cfg, "%s: sqlite3_finalize: %s",
		db->src->fname, pst->stmt);
	(void)sqlite3_finalize(stmt);
}

enum sqlbox_code
sqlbox_wrap_exec(struct sqlbox *box, struct sqlbox_db *db,
	const struct sqlbox_pstmt *pst, int allow_cstep)
{
	size_t	 attempt = 0;

	assert(pst != NULL && pst->stmt != NULL);
	sqlbox_debug(&box->cfg, "%s: sqlite3_exec: %s",
		db->src->fname, pst->stmt);

again_step:
	switch (sqlite3_exec(db->db, pst->stmt, NULL, NULL, NULL)) {
	case SQLITE_BUSY:
		/*
		 * FIXME: according to sqlite3_step(3), this
		 * should return if we're in a transaction.
		 */
		sqlbox_sleep(attempt++);
		goto again_step;
	case SQLITE_LOCKED:
	case SQLITE_PROTOCOL:
		sqlbox_sleep(attempt++);
		goto again_step;
	case SQLITE_OK:
		return SQLBOX_CODE_OK;
	case SQLITE_CONSTRAINT:
		if (allow_cstep)
			return SQLBOX_CODE_CONSTRAINT;
		/* FALLTHROUGH */
	default:
		break;
	}

	sqlbox_warnx(&box->cfg, "%s: sqlite3_exec: %s", 
		db->src->fname, sqlite3_errmsg(db->db));
	sqlbox_warnx(&box->cfg, "%s: statement: %s", 
		db->src->fname, pst->stmt);
	return SQLBOX_CODE_ERROR;
}
