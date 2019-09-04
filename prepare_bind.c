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

/*
 * Return TRUE if the current role has the ability to prepare the given
 * statement (or no roles are specified), FALSE if otherwise.
 */
static int
sqlbox_rolecheck_stmt(struct sqlbox *box, size_t idx)
{
	size_t	 i;

	if (box->cfg.roles.rolesz == 0)
		return 1;
	for (i = 0; i < box->cfg.roles.roles[box->role].stmtsz; i++)
		if (box->cfg.roles.roles[box->role].stmts[i] == idx)
			return 1;
	sqlbox_warnx(&box->cfg, "prepare-bind: statement "
		"%zu denied to role %zu", idx, box->role);
	return 0;
}

size_t
sqlbox_prepare_bind(struct sqlbox *box, size_t srcid,
	size_t pstmt, size_t psz, const struct sqlbox_bound *ps)
{
	size_t		 pos = 0, bufsz = 1024;
	uint32_t	 val;
	char		*buf;

	/* Initialise our frame. */

	if ((buf = malloc(bufsz)) == NULL) {
		sqlbox_warn(&box->cfg, "prepare-bind: malloc");
		return 0;
	}

	/* Skip the frame size til we get the packed parms. */

	pos = sizeof(uint32_t);

	/* Pack operation, source, statement, and parameters. */

	val = htonl(SQLBOX_OP_PREPARE_BIND);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	val = htonl(srcid);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	val = htonl(pstmt);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	if (!sqlbox_bound_pack(box, psz, ps, &buf, &pos, &bufsz)) {
		sqlbox_warnx(&box->cfg,
			"prepare-bind: sqlbox_bound_pack");
		free(buf);
		return 0;
	}

	/* Go back and do the frame size. */

	val = htonl(pos - 4);
	memcpy(buf, (char *)&val, sizeof(uint32_t));

	/* Write data, free our buffer. */

	if (!sqlbox_write(box, buf, bufsz)) {
		sqlbox_warnx(&box->cfg, "prepare-bind: sqlbox_write");
		free(buf);
		return 0;
	}
	free(buf);

	/* Read the statement identifier. */

	if (!sqlbox_read(box, (char *)&val, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "prepare-bind: sqlbox_read");
		return 0;
	}

	return (size_t)ntohl(val);
}

/*
 * Prepare and bind parameters to a statement in one step.
 * Return TRUE on success, FALSE on failure (nothing is allocated).
 */
int
sqlbox_op_prepare_bind(struct sqlbox *box, const char *buf, size_t sz)
{
	size_t	 		 i, idx, attempt = 0, parmsz;
	enum sqlbox_boundt	 type;
	struct sqlbox_db	*db;
	sqlite3_stmt		*stmt = NULL;
	struct sqlbox_stmt	*st;
	struct sqlbox_pstmt	*pst = NULL;
	int			 c;
	uint32_t		 ack;
	struct sqlbox_bound	*parms = NULL;

	/* Read the source identifier. */

	if (sz < sizeof(uint32_t))
		goto badframe;
	db = sqlbox_db_find(box, ntohl(*(uint32_t *)buf));
	if (db == NULL) {
		sqlbox_warnx(&box->cfg, "prepare-bind: sqlbox_db_find");
		return 0;
	}
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	/* Read and validate the statement identifier. */

	if (sz < sizeof(uint32_t))
		goto badframe;
	idx = ntohl(*(uint32_t *)buf);
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	if (idx >= box->cfg.stmts.stmtsz) {
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"bad statement %zu", db->src->fname, idx);
		return 0;
	} else if (!sqlbox_rolecheck_stmt(box, idx)) {
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"sqlbox_rolecheck_stmt", db->src->fname);
		return 0;
	}
	pst = &box->cfg.stmts.stmts[idx];

	/* 
	 * Now the number of parameters.
	 * FIXME: we should have nothing left in the buffer after this.
	 */

	if (!sqlbox_bound_unpack(box, &parmsz, &parms, buf, sz)) {
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"sqlbox_bound_unpack", db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: prepare-bind "
			"statement: %s", db->src->fname, pst->stmt);
		return 0;
	}

	/* 
	 * Actually prepare the statement.
	 * In the usual way we sleep if SQLite gives us a busy, locked,
	 * or weird protocol error.
	 * All other errors are real errorrs.
	 */
again:
	stmt = NULL;
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
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: %s", 
			db->src->fname, sqlite3_errmsg(db->db));
		sqlbox_warnx(&box->cfg, "%s: prepare-bind "
			"statement: %s", db->src->fname, pst->stmt);
		if (stmt != NULL)
			sqlite3_finalize(stmt);
		free(parms);
		return 0;
	}

	/* 
	 * Now bind parameters.
	 * Note that we mark the strings as SQLITE_TRANSIENT because
	 * we're probably going to lose the buffer during the next read
	 * and so it needs to be stored.
	 */

	for (i = 0; i < parmsz; i++) {
		switch (parms[i].type) {
		case SQLBOX_BOUND_INT:
			c = sqlite3_bind_int64
				(stmt, i + 1, parms[i].iparm);
			break;
		case SQLBOX_BOUND_NULL:
			c = sqlite3_bind_null(stmt, i + 1);
			break;
		case SQLBOX_BOUND_STRING:
			c = sqlite3_bind_text16(stmt, i + 1,
				parms[i].sparm, -1, SQLITE_TRANSIENT);
			break;
		default:
			sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
				"bad type: %d", db->src->fname, type);
			sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
				"statement: %s", db->src->fname, 
				pst->stmt);
			sqlite3_finalize(stmt);
			free(parms);
			return 0;
		}
	}
	free(parms);

	/* 
	 * Allocate and queue up, then send our identifier back to the
	 * client just as with the open.
	 */

	if ((st = calloc(1, sizeof(struct sqlbox_stmt))) == NULL) {
		sqlbox_warn(&box->cfg, "%s: prepare-bind: "
			"calloc", db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"statement: %s", db->src->fname, pst->stmt);
		sqlite3_finalize(stmt);
		return 0;
	}

	st->stmt = stmt;
	st->pstmt = pst;
	st->idx = idx;
	st->db = db;
	st->id = ++box->lastid;
	TAILQ_INSERT_TAIL(&db->stmtq, st, entries);

	ack = htonl(db->id);
	if (!sqlbox_write(box, (char *)&ack, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"sqlbox_write", db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"statement: %s", db->src->fname, pst->stmt);
		TAILQ_REMOVE(&db->stmtq, st, entries);
		sqlite3_finalize(stmt);
		free(st);
		return 0;
	}
	return 1;

badframe:
	sqlbox_warnx(&box->cfg, "prepare-bind: "
		"bad frame size: %zu", sz);
	if (pst != NULL && db != NULL) 
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: statement: "
			"%s", db->src->fname, pst->stmt);
	if (stmt != NULL)
		sqlite3_finalize(stmt);
	return 0;
}

