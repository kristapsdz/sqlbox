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

const struct sqlbox_parmset *
sqlbox_step(struct sqlbox *box, size_t stmtid)
{
	uint32_t		 v = htonl(stmtid);
	const char		*frame;
	size_t			 framesz;
	struct sqlbox_stmt 	*st;

	TAILQ_FOREACH(st, &box->stmtq, gentries)
		if (st->id == stmtid)
			break;

	if (st == NULL) {
		sqlbox_warnx(&box->cfg, "step: bad stmt %zu", stmtid);
		return NULL;
	}

	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_STEP, (char *)&v, sizeof(uint32_t))) {
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

	/* 
	 * Next, extract the parsed parameters from the frame.
	 * We use the signed psz in sqlbox_res as the size indicator
	 * because it will store whether this is our first step.
	 * This will make sure, on reentry, that we're unpacking the
	 * same number of parameters as the first time.
	 */

	if (!sqlbox_parm_unpack(box,
	    &st->res.set.ps, &st->res.psz, frame, framesz)) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_bound_unpack");
		return NULL;
	}

	assert(st->res.psz >= 0);
	st->res.set.psz = (size_t)st->res.psz;
	return &st->res.set;
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
	size_t			 pos, id, attempt = 0, i;
	int			 hasrow;
	uint32_t		 val;

	/* Look up the statement in our global list. */

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "step: "
			"bad frame size: %zu", sz);
		return 0;
	}
	id = ntohl(*(uint32_t *)buf);
	TAILQ_FOREACH(st, &box->stmtq, gentries)
		if (st->id == id)
			break;
	if (st == NULL) {
		sqlbox_warnx(&box->cfg, "step: bad stmt: %zu", id);
		return 0;
	}

	/* Now we run the database routine. */
	
again:
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
		hasrow = 0;
		break;
	case SQLITE_ROW:
		hasrow = sqlite3_column_count(st->stmt);
		break;
	default:
		sqlbox_warnx(&box->cfg, "%s: step: %s", 
			st->db->src->fname, 
			sqlite3_errmsg(st->db->db));
		return 0;
	}

	/*
	 * See if we need to prime our result set.
	 * Our result sizes must be the same, but the result types may
	 * change.
	 * Note that the text and blob pointers are immediately
	 * serialised below, so we don't need to worry about the return
	 * pointers going stale.
	 */

	if (hasrow < 0) {
		sqlbox_warnx(&box->cfg, "%s: step: negative result "
			"length: %d", st->db->src->fname, hasrow);
		return 0;
	} else if (st->res.psz >= 0 && hasrow != st->res.psz) {
		sqlbox_warnx(&box->cfg, "%s: step: result length "
			"mismatch (have %zd, %d in results)",
			st->db->src->fname, st->res.psz, hasrow);
		return 0;
	} else if (st->res.psz < 0 && hasrow > 0) {
		st->res.set.ps = calloc
			(hasrow, sizeof(struct sqlbox_parm));
		if (st->res.set.ps == NULL) {
			sqlbox_warn(&box->cfg, "step: calloc");
			return 0;
		}
		st->res.psz = hasrow;
		st->res.set.psz = (size_t)hasrow;
	} else if (st->res.psz < 0) {
		assert(hasrow == 0);
		st->res.psz = 0;
		st->res.set.psz = 0;
	}

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
			st->res.set.ps[i].type = SQLBOX_PARM_INT;
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
			st->res.set.ps[i].sparm = 
				sqlite3_column_text(st->stmt, i);
			st->res.set.ps[i].sz = strlen
				(st->res.set.ps[i].sparm) + 1;
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
	 * Start with at least a 1024 byte buffer.
	 */

	if (st->res.bufsz == 0) {
		st->res.bufsz = 1024;
		st->res.buf = malloc(st->res.bufsz);
		if (st->res.buf == NULL) {
			sqlbox_warn(&box->cfg, "step: malloc");
			return 0;
		}
	}
	assert(st->res.bufsz >= 0);

	/* Skip the frame size til we get the packed parms. */

	pos = sizeof(uint32_t);

	if (!sqlbox_parm_pack(box, st->res.set.psz, st->res.set.ps, 
	    &st->res.buf, &pos, &st->res.bufsz)) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_parm_pack");
		return 0;
	}

	/* Now write our frame size. */

	val = htonl(pos - 4);
	memcpy(st->res.buf, (char *)&val, sizeof(uint32_t));

	pos = pos > 1024 ? pos : 1024;

	if (!sqlbox_write(box, st->res.buf, pos)) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_write");
		return 0;
	}
	return 1;
}