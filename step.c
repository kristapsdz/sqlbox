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

static const struct sqlbox_parmset *
sqlbox_step_inner(struct sqlbox *box, int constraint, size_t stmtid)
{
	uint32_t		 val;
	char			 buf[sizeof(uint32_t) * 2];
	const char		*frame;
	size_t			 framesz;
	struct sqlbox_stmt 	*st;

	if ((st = sqlbox_stmt_find(box, stmtid)) == NULL) {
		sqlbox_warnx(&box->cfg, "step: bad stmt %zu", stmtid);
		return NULL;
	}

	/* Write id and whether we accept constraint violations. */

	val = htole32(stmtid);
	memcpy(buf, (char *)&val, sizeof(uint32_t));

	val = htole32(constraint);
	memcpy(buf + sizeof(uint32_t), (char *)&val, sizeof(uint32_t));

	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_STEP, buf, sizeof(buf))) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_write_frame");
		return NULL;
	}

	/* 
	 * This will read the entire result set in its binary format
	 * using the given buffer.
	 * It will set frame and framesz to be the binary area for the
	 * packed parameters.
	 */

	if (sqlbox_read_frame(box, 
	    &st->res.buf, &st->res.bufsz, &frame, &framesz) <= 0) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_read_frame");
		return NULL;
	}

	if (framesz < sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "step: "
			"bad frame size: %zu", framesz);
		return NULL;
	}

	/* Extract the return code. */

	st->res.set.code = le32toh(*(uint32_t *)frame);
	frame += sizeof(uint32_t);
	framesz -= sizeof(uint32_t);

	/* 
	 * Next, extract the parsed parameters from the frame.
	 * We use the signed psz in sqlbox_res as the size indicator
	 * because it will store whether this is our first step.
	 * This will make sure, on reentry, that we're unpacking the
	 * same number of parameters as the first time.
	 */

	if (!sqlbox_parm_unpack(box,
	    &st->res.set.ps, &st->res.psz, frame, framesz)) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_parm_unpack");
		return NULL;
	}

	assert(st->res.psz >= 0);
	st->res.set.psz = (size_t)st->res.psz;
	return &st->res.set;
}

const struct sqlbox_parmset *
sqlbox_step(struct sqlbox *box, size_t stmtid)
{

	return sqlbox_step_inner(box, 0, stmtid);
}

const struct sqlbox_parmset *
sqlbox_cstep(struct sqlbox *box, size_t stmtid)
{

	return sqlbox_step_inner(box, 1, stmtid);
}

/*
 * Step through zero or more results to a prepared statement.
 * We do not allow constraint violations.
 * Return TRUE on success, FALSE on failure (nothing is allocated).
 */
int
sqlbox_op_step(struct sqlbox *box, const char *buf, size_t sz)
{
	struct sqlbox_stmt	*st;
	size_t			 pos, id, attempt = 0, i, cols = 0;
	int			 ccount, has_cstep = 0, allow_cstep;
	uint32_t		 val;

	/* Look up the statement in our global list. */

	if (sz != sizeof(uint32_t) * 2) {
		sqlbox_warnx(&box->cfg, "step: "
			"bad frame size: %zu", sz);
		return 0;
	}
	id = le32toh(*(uint32_t *)buf);
	if ((st = sqlbox_stmt_find(box, id)) == NULL) {
		sqlbox_warnx(&box->cfg, "step: bad stmt: %zu", id);
		return 0;
	}
	allow_cstep = le32toh(*(uint32_t *)(buf + sizeof(uint32_t)));

	/* 
	 * Did we already run this to completion?
	 * We know this if the existing result set has size zero.
	 * If so, we're not allowed to reinvoke it.
	 */

	if (st->res.psz == 0) {
		sqlbox_warnx(&box->cfg, "%s: step: already "
			"stepped", st->db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: step: statement: "
			"%s", st->db->src->fname, st->pstmt->stmt);
		return 0;
	}

	/* Now we run the database routine. */
	
again:
	sqlbox_debug(&box->cfg, "sqlite3_step: %s, %s",
		st->db->src->fname, st->pstmt->stmt);
	switch (sqlite3_step(st->stmt)) {
	case SQLITE_BUSY:
		/*
		 * FIXME: according to sqlite3_step(3), this
		 * should return if we're in a transaction.
		 */
		sqlbox_sleep(attempt++);
		goto again;
	case SQLITE_LOCKED:
	case SQLITE_PROTOCOL:
		sqlbox_sleep(attempt++);
		goto again;
	case SQLITE_DONE:
		break;
	case SQLITE_ROW:
		if ((ccount = sqlite3_column_count(st->stmt)) > 0) {
			cols = (size_t)ccount;
			break;
		}
		sqlbox_warnx(&box->cfg, "%s: step: row without "
			"columns?", st->db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: step: statement: "
			"%s", st->db->src->fname, st->pstmt->stmt);
		break;
	case SQLITE_CONSTRAINT:
		if (allow_cstep) {
			has_cstep = 1;
			break;
		}
		/* FALLTHROUGH */
	default:
		sqlbox_warnx(&box->cfg, "%s: step: %s", 
			st->db->src->fname, 
			sqlite3_errmsg(st->db->db));
		return 0;
	}

	/*
	 * See if we need to prime our result set: if we have columns,
	 * allocate results (make sure, with more results, that the
	 * number of results are the same).
	 */

	if (cols && st->res.psz >= 0 && cols != (size_t)st->res.psz) {
		sqlbox_warnx(&box->cfg, "%s: step: result length "
			"mismatch (have %zd, %zu in results)",
			st->db->src->fname, st->res.psz, cols);
		return 0;
	} else if (cols && st->res.psz < 0) {
		st->res.set.ps = calloc
			(cols, sizeof(struct sqlbox_parm));
		if (st->res.set.ps == NULL) {
			sqlbox_warn(&box->cfg, "step: calloc");
			return 0;
		}
		st->res.psz = cols;
		st->res.set.psz = cols;
	} else if (cols == 0 || st->res.psz < 0) {
		assert(cols == 0);
		st->res.psz = 0;
		st->res.set.psz = 0;
	}

	/*
	 * Text and blob pointers are immediately serialised, so we
	 * don't need to worry about the return pointers going stale.
	 */

	for (i = 0; i < st->res.set.psz; i++) {
		switch (sqlite3_column_type(st->stmt, i)) {
		case SQLITE_BLOB:
			st->res.set.ps[i].type = SQLBOX_PARM_BLOB;
			st->res.set.ps[i].bparm = 
				sqlite3_column_blob(st->stmt, i);
			st->res.set.ps[i].sz = 
				sqlite3_column_bytes(st->stmt, i);
			break;
		case SQLITE_FLOAT:
			st->res.set.ps[i].type = SQLBOX_PARM_FLOAT;
			st->res.set.ps[i].fparm = 
				sqlite3_column_double(st->stmt, i);
			st->res.set.ps[i].sz = sizeof(double);
			break;
		case SQLITE_INTEGER:
			st->res.set.ps[i].type = SQLBOX_PARM_INT;
			st->res.set.ps[i].iparm = 
				sqlite3_column_int64(st->stmt, i);
			st->res.set.ps[i].sz = sizeof(int64_t);
			break;
		case SQLITE_TEXT:
			st->res.set.ps[i].type = SQLBOX_PARM_STRING;
			st->res.set.ps[i].sparm = (char *)
				sqlite3_column_text(st->stmt, i);
			st->res.set.ps[i].sz = 
				sqlite3_column_bytes(st->stmt, i) + 1;
			break;
		case SQLITE_NULL:
			st->res.set.ps[i].type = SQLBOX_PARM_NULL;
			st->res.set.ps[i].sz = 0;
			break;
		default:
			sqlbox_warnx(&box->cfg, "%s: step: "
				"unknown column type: %d "
				"(position %zu)",
				st->db->src->fname,
				sqlite3_column_type(st->stmt, i),
				i);
			return 0;
		}
	}

	/* 
	 * We now serialise our results over the wire.
	 * Start with at least a SQLBOX_FRAME byte buffer.
	 */

	if (st->res.bufsz == 0) {
		st->res.bufsz = SQLBOX_FRAME;
		st->res.buf = calloc(1, st->res.bufsz);
		if (st->res.buf == NULL) {
			sqlbox_warn(&box->cfg, "step: malloc");
			return 0;
		}
	}
	assert(st->res.bufsz >= SQLBOX_FRAME);

	/* 
	 * Write our return code (whether we had a constraint violation)
	 * then the results, if any.  Make room for the size at the
	 * beginning of the buffer.
	 */

	val = htole32(has_cstep);
	memcpy(st->res.buf + sizeof(uint32_t), 
		(char *)&val, sizeof(uint32_t));
	pos = sizeof(uint32_t) * 2;

	if (!sqlbox_parm_pack(box, st->res.set.psz, st->res.set.ps, 
	    &st->res.buf, &pos, &st->res.bufsz)) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_parm_pack");
		return 0;
	}

	/* Now write our frame size. */

	val = htole32(pos - sizeof(uint32_t));
	memcpy(st->res.buf, (char *)&val, sizeof(uint32_t));

	/* The frame will be at least SQLBOX_FRAME long. */

	pos = pos > SQLBOX_FRAME ? pos : SQLBOX_FRAME;
	if (!sqlbox_write(box, st->res.buf, pos)) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_write");
		return 0;
	}
	return 1;
}
