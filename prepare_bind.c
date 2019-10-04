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

/*
 * Shared by both asynchronous and synchronous version.
 * Sends the prepare-bind instruction to the server.
 * The caller should handle any possible synchrony.
 * Returns the allocated statement or NULL on failure.
 */
static struct sqlbox_stmt *
sqlbox_pbind(struct sqlbox *box, enum sqlbox_op op, size_t srcid, 
	size_t pstmt, size_t psz, const struct sqlbox_parm *ps)
{
	size_t			 pos = 0, bufsz = SQLBOX_FRAME, i;
	uint32_t		 val;
	char			*buf;
	struct sqlbox_stmt	*st;

	/* 
	 * Make sure explicit-sized strings are NUL terminated.
	 * FIXME: kill server on error.
	 */

	for (i = 0; i < psz; i++) 
		if (ps[i].type == SQLBOX_PARM_STRING &&
		    ps[i].sz > 0 &&
		    ps[i].sparm[ps[i].sz - 1] != '\0') {
			sqlbox_warnx(&box->cfg, "prepare-bind: "
				"parameter %zu is malformed", i);
			return NULL;
		}

	/* Initialise our result set holder and frame. */

	if ((st = calloc(1, sizeof(struct sqlbox_stmt))) == NULL) {
		sqlbox_warn(&box->cfg, "prepare-bind: calloc");
		return NULL;
	} else if ((buf = calloc(bufsz, 1)) == NULL) {
		sqlbox_warn(&box->cfg, "prepare-bind: calloc");
		free(st);
		return NULL;
	}
	st->res.psz = -1;

	/* Skip the frame size til we get the packed parms. */

	pos = sizeof(uint32_t);

	/* Pack operation, source, statement, and parameters. */

	val = htole32(op);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	val = htole32(srcid);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	val = htole32(pstmt);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	if (!sqlbox_parm_pack(box, psz, ps, &buf, &pos, &bufsz)) {
		sqlbox_warnx(&box->cfg,
			"prepare-bind: sqlbox_parm_pack");
		free(buf);
		free(st);
		return NULL;
	}

	/* Go back and do the frame size. */

	val = htole32(pos - 4);
	memcpy(buf, (char *)&val, sizeof(uint32_t));

	/* Write data, free our buffer. */

	if (!sqlbox_write(box, buf, pos > SQLBOX_FRAME ? pos : SQLBOX_FRAME)) {
		sqlbox_warnx(&box->cfg, "prepare-bind: sqlbox_write");
		free(buf);
		free(st);
		return NULL;
	}
	free(buf);
	return st;
}

int
sqlbox_prepare_bind_async(struct sqlbox *box, size_t srcid,
	size_t pstmt, size_t psz, const struct sqlbox_parm *ps)
{
	struct sqlbox_stmt	*st;

	if ((st = sqlbox_pbind(box, SQLBOX_OP_PREPARE_BIND_ASYNC, 
	    srcid, pstmt, psz, ps)) == NULL) {
		sqlbox_warnx(&box->cfg, 
			"prepare-bind-async: sqlbox_pbind");
		return 0;
	}

	/* Use zero for this implicit case. */

	st->id = 0;
	TAILQ_INSERT_TAIL(&box->stmtq, st, gentries);
	return 1;
}

size_t
sqlbox_prepare_bind(struct sqlbox *box, size_t srcid,
	size_t pstmt, size_t psz, const struct sqlbox_parm *ps)
{
	struct sqlbox_stmt	*st;
	uint32_t		 val;

	if ((st = sqlbox_pbind(box, SQLBOX_OP_PREPARE_BIND_SYNC,
	    srcid, pstmt, psz, ps)) == NULL) {
		sqlbox_warnx(&box->cfg, 
			"prepare-bind-sync: sqlbox_pbind");
		return 0;
	}

	if (!sqlbox_read(box, (char *)&val, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "prepare-bind: sqlbox_read");
		free(st);
		return 0;
	}

	/* Use the server's identifier for this explicit case. */

	if ((st->id = le32toh(val)) == 0) {
		sqlbox_warnx(&box->cfg, "prepare-bind: server "
			"wrote back identifier of zero");
		free(st);
		return 0;
	}

	TAILQ_INSERT_TAIL(&box->stmtq, st, gentries);
	return st->id;
}

/*
 * Prepare and bind parameters to a statement in one step.
 * Do not send anything back to the client: this is done by the caller
 * depending upon the mode.
 * Return the allocated statement on success, NULL on failure.
 */
static struct sqlbox_stmt *
sqlbox_op_prepare_bind(struct sqlbox *box, const char *buf, size_t sz)
{
	size_t	 		 idx;
	ssize_t			 parmsz = -1;
	struct sqlbox_db	*db;
	sqlite3_stmt		*stmt = NULL;
	struct sqlbox_stmt	*st;
	struct sqlbox_pstmt	*pst = NULL;
	struct sqlbox_parm	*parms = NULL;

	/* Read the source identifier. */

	if (sz < sizeof(uint32_t))
		goto badframe;
	db = sqlbox_db_find(box, le32toh(*(uint32_t *)buf));
	if (db == NULL) {
		sqlbox_warnx(&box->cfg, "prepare-bind: sqlbox_db_find");
		return NULL;
	}
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	/* Read and validate the statement identifier. */

	if (sz < sizeof(uint32_t))
		goto badframe;
	idx = le32toh(*(uint32_t *)buf);
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	if (idx >= box->cfg.stmts.stmtsz) {
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"bad statement %zu", db->src->fname, idx);
		return NULL;
	} else if (!sqlbox_rolecheck_stmt(box, idx)) {
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"sqlbox_rolecheck_stmt", db->src->fname);
		return NULL;
	}
	pst = &box->cfg.stmts.stmts[idx];

	/* 
	 * Now the number of parameters.
	 * FIXME: we should have nothing left in the buffer after this.
	 */

	if (!sqlbox_parm_unpack(box, &parms, &parmsz, buf, sz)) {
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"sqlbox_parm_unpack", db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: prepare-bind "
			"statement: %s", db->src->fname, pst->stmt);
		free(parms);
		return NULL;
	}
	assert(parmsz >= 0);

	/* Actually prepare the statement. */

	if ((stmt = sqlbox_prepare(box, db, pst)) == NULL) {
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"sqlbox_prepare", db->src->fname);
		free(parms);
		return NULL;
	}

	/* Now bind parameters. */

	if (!sqlbox_parm_bind(box, db, pst, stmt, parms, parmsz)) {
		sqlbox_debug(&box->cfg, "%s: sqlite3_finalize: %s", 
			db->src->fname, pst->stmt);
		sqlite3_finalize(stmt);
		free(parms);
		return NULL;
	}
	free(parms);

	/* Success!  Assuming the allocation works, we're done. */

	if ((st = calloc(1, sizeof(struct sqlbox_stmt))) == NULL) {
		sqlbox_warn(&box->cfg, "%s: prepare-bind: "
			"calloc", db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: "
			"statement: %s", db->src->fname, pst->stmt);
		sqlbox_debug(&box->cfg, "sqlite3_finalize: "
			"%s, %s", db->src->fname, pst->stmt);
		sqlite3_finalize(stmt);
		return NULL;
	}

	st->res.psz = -1;
	st->stmt = stmt;
	st->pstmt = pst;
	st->idx = idx;
	st->db = db;
	st->id = ++box->lastid;
	TAILQ_INSERT_TAIL(&db->stmtq, st, entries);
	TAILQ_INSERT_TAIL(&box->stmtq, st, gentries);
	return st;

badframe:
	sqlbox_warnx(&box->cfg, "prepare-bind: "
		"bad frame size: %zu", sz);
	if (pst != NULL && db != NULL) 
		sqlbox_warnx(&box->cfg, "%s: prepare-bind: statement: "
			"%s", db->src->fname, pst->stmt);
	if (stmt != NULL) {
		sqlbox_debug(&box->cfg, "sqlite3_finalize: "
			"%s, %s", db->src->fname, pst->stmt);
		sqlite3_finalize(stmt);
	}
	return NULL;
}

int
sqlbox_op_prepare_bind_sync(struct sqlbox *box, const char *buf, size_t sz)
{
	struct sqlbox_stmt	*st;
	uint32_t		 ack;

	if ((st = sqlbox_op_prepare_bind(box, buf, sz)) == NULL) {
		sqlbox_warnx(&box->cfg, "prepare-bind-sync: "
			"sqlbox_op_prepare_bind");
		return 0;
	}

	/* Synchronous version writes back the identifier. */

	assert(st->id != 0);
	ack = htole32(st->id);
	if (sqlbox_write(box, (char *)&ack, sizeof(uint32_t)))
		return 1;

	sqlbox_warnx(&box->cfg, "%s: prepare-bind-sync: "
		"sqlbox_write", st->db->src->fname);
	sqlbox_warnx(&box->cfg, "%s: prepare-bind-sync: "
		"statement: %s", st->db->src->fname, st->pstmt->stmt);
	TAILQ_REMOVE(&st->db->stmtq, st, entries);
	TAILQ_REMOVE(&box->stmtq, st, gentries);
	sqlbox_debug(&box->cfg, "sqlite3_finalize: "
		"%s, %s", st->db->src->fname, st->pstmt->stmt);
	sqlite3_finalize(st->stmt);
	free(st);
	return 0;
}

int
sqlbox_op_prepare_bind_async(struct sqlbox *box, const char *buf, size_t sz)
{
	struct sqlbox_stmt	*st;

	if ((st = sqlbox_op_prepare_bind(box, buf, sz)) == NULL) {
		sqlbox_warnx(&box->cfg, "prepare-bind-async: "
			"sqlbox_op_prepare_bind");
		return 0;
	}
	return 1;
}
