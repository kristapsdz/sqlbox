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

struct	freen {
	void			 *dat;
	void			(*fp)(void *);
	TAILQ_ENTRY(freen)	 entries;
};

TAILQ_HEAD(freeq, freen);

static const struct sqlbox_parmset *
sqlbox_step_inner(struct sqlbox *box, int constraint, size_t stmtid)
{
	uint32_t		 val;
	char			 buf[sizeof(uint32_t) * 2];
	const char		*frame;
	size_t			 i, framesz, psz;
	struct sqlbox_stmt 	*st;
	void			*pp;

	/* Look up the statement. */

	if ((st = sqlbox_stmt_find(box, stmtid)) == NULL) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_stmt_find");
		return NULL;
	}

	/* Return last cached response, if applicable. */

	if (st->res.curset < st->res.setsz)
		return &st->res.set[st->res.curset++];

	/* Clear any existing results. */

	sqlbox_res_clear(&st->res);

	/* 
	 * Write id and whether we accept constraint violations.
	 * Mixing constraint and no-constraint is allowed: jumping
	 * through hoops to prevent it just introduces complexity with
	 * little benefit.
	 */

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

	/* Read as many results sets as are available. */

	while (framesz > 0) {
		if (framesz < sizeof(uint32_t)) {
			sqlbox_warnx(&box->cfg, 
				"step: bad frame size");
			return NULL;
		}

		pp = reallocarray(st->res.set, 
			st->res.setsz + 1, 
			sizeof(struct sqlbox_parmset));
		if (pp == NULL) {
			sqlbox_warn(&box->cfg, "step: reallocarray");
			return NULL;
		}
		st->res.set = pp;
		i = st->res.setsz++;
		memset(&st->res.set[i], 0, 
			sizeof(struct sqlbox_parmset));

		st->res.set[i].code = le32toh(*(uint32_t *)frame);
		frame += sizeof(uint32_t);
		framesz -= sizeof(uint32_t);

		psz = sqlbox_parm_unpack(box, 
			&st->res.set[i].ps, 
			&st->res.set[i].psz,
			frame, framesz);
		if (psz == 0) {
			sqlbox_warnx(&box->cfg, 
				"step: sqlbox_parm_unpack");
			return NULL;
		}
		frame += psz;
		framesz -= psz;
	}

	/* Return the first cached entry. */

	return &st->res.set[st->res.curset++];
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
 * Read a single result from the wire and append it to the packed
 * parameters we already have in our buffer.
 */
static int
sqlbox_pack_step(struct sqlbox *box, size_t *bufpos,
	struct sqlbox_stmt *st, int allow_cstep)
{
	enum sqlbox_code	 code;
	struct sqlbox_parmset	 set;
	size_t			 cols = 0, i, j;
	int			 has_cstep = 0, rc = 0;
	void			*pp, *arg;
	struct freeq		 fq;
	struct freen		*fn;
	uint32_t		 val;

	TAILQ_INIT(&fq);

	/* Start with the step itself. */

	code = sqlbox_wrap_step(box, st->db, 
		st->pstmt, st->stmt, &cols, allow_cstep);
	if (code == SQLBOX_CODE_ERROR) {
		sqlbox_warnx(&box->cfg, "%s: step: "
			"sqlbox_wrap_step", st->db->src->fname);
		return -1;
	} else if (code == SQLBOX_CODE_CONSTRAINT)
		has_cstep = 1;
	
	if (cols > 0) {
		set.psz = cols;
		set.ps = calloc(cols, sizeof(struct sqlbox_parm));
		if (set.ps == NULL) {
			sqlbox_warn(&box->cfg, "step: calloc");
			return -1;
		}
	} else {
		set.psz = 0;
		set.ps = NULL;
	}

	/*
	 * Text and blob pointers are immediately serialised, so we
	 * don't need to worry about the return pointers going stale.
	 * Maintain a list of pointers we need to pass to custom "free"
	 * routines in "fq".
	 */

	for (i = 0; i < set.psz; i++) {
		/*
		 * See if we have a filter for generating data instead
		 * of using the database.
		 */

		for (j = 0; j < box->cfg.filts.filtsz; j++) 
			if (box->cfg.filts.filts[j].stmt == st->idx &&
			    box->cfg.filts.filts[j].type == 
			      SQLBOX_FILT_GEN_OUT &&
			    box->cfg.filts.filts[j].col == i)
				break;
		if (j < box->cfg.filts.filtsz) {
			arg = NULL;
			if (!(*box->cfg.filts.filts[j].filt)
			    (&set.ps[i], &arg)) {
				sqlbox_warn(&box->cfg, "%s: step: "
					"filter: position %zu",
					st->db->src->fname, i);
				sqlbox_warnx(&box->cfg, "%s: step: "
					"statement: %s", 
					st->db->src->fname, 
					st->pstmt->stmt);
				goto out;
			}
			if (box->cfg.filts.filts[j].free == NULL) 
				continue;

			/* Create an exit hook for free. */

			fn = calloc(1, sizeof(struct freen));
			if (fn == NULL) {
				sqlbox_warn(&box->cfg, 
					"step: calloc");
				(*box->cfg.filts.filts[j].free)(arg);
				goto out;
			}
			fn->dat = arg;
			fn->fp = box->cfg.filts.filts[j].free;
			TAILQ_INSERT_TAIL(&fq, fn, entries);
			continue;
		}

		switch (sqlite3_column_type(st->stmt, i)) {
		case SQLITE_BLOB:
			set.ps[i].type = SQLBOX_PARM_BLOB;
			set.ps[i].bparm = sqlite3_column_blob(st->stmt, i);
			set.ps[i].sz = sqlite3_column_bytes(st->stmt, i);
			break;
		case SQLITE_FLOAT:
			set.ps[i].type = SQLBOX_PARM_FLOAT;
			set.ps[i].fparm = sqlite3_column_double(st->stmt, i);
			set.ps[i].sz = sizeof(double);
			break;
		case SQLITE_INTEGER:
			set.ps[i].type = SQLBOX_PARM_INT;
			set.ps[i].iparm = sqlite3_column_int64(st->stmt, i);
			set.ps[i].sz = sizeof(int64_t);
			break;
		case SQLITE_TEXT:
			set.ps[i].type = SQLBOX_PARM_STRING;
			set.ps[i].sparm = (char *)
				sqlite3_column_text(st->stmt, i);
			set.ps[i].sz = sqlite3_column_bytes(st->stmt, i) + 1;
			break;
		case SQLITE_NULL:
			set.ps[i].type = SQLBOX_PARM_NULL;
			set.ps[i].sz = 0;
			break;
		default:
			sqlbox_warnx(&box->cfg, "%s: step: "
				"unknown column type: %d "
				"(position %zu)", st->db->src->fname,
				sqlite3_column_type(st->stmt, i), i);
			sqlbox_warnx(&box->cfg, "%s: step: "
				"statement: %s", st->db->src->fname, 
				st->pstmt->stmt);
			goto out;
		}
	}

	/* 
	 * Serialise our results.
	 * The buffer has already been primed with space for the initial
	 * byte length.
	 * FIXME: optimal buffer sizing here and in sqlbox_parm_pack()?
	 */

	assert(st->res.bufsz);
	if (*bufpos + sizeof(uint32_t) >= st->res.bufsz) {
		pp = realloc(st->res.buf, *bufpos + 1024);
		if (pp == NULL) {
			sqlbox_warn(&box->cfg, "step: realloc");
			goto out;
		}
		st->res.buf = pp;
		st->res.bufsz = *bufpos + 1024;
	}

	/* 
	 * Write our return code (whether we had a constraint violation)
	 * then the results, if any.  Make room for the size at the
	 * beginning of the buffer.
	 */

	val = htole32(has_cstep);
	memcpy(st->res.buf + *bufpos, (char *)&val, sizeof(uint32_t));
	*bufpos += sizeof(uint32_t);

	if (!sqlbox_parm_pack(box, set.psz, set.ps, 
	    &st->res.buf, bufpos, &st->res.bufsz)) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_parm_pack");
		goto out;
	}
	rc = 1;
out:
	while ((fn = TAILQ_FIRST(&fq)) != NULL) {
		TAILQ_REMOVE(&fq, fn, entries);
		(*fn->fp)(fn->dat);
		free(fn);
	}
	free(set.ps);
	return rc;
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
	size_t			 pos;
	int			 allow_cstep, rc, done;
	uint32_t		 val;
	
	/* Look up the statement in our global list. */

	if (sz != sizeof(uint32_t) * 2) {
		sqlbox_warnx(&box->cfg, "step: bad frame size");
		return 0;
	}
	if ((st = sqlbox_stmt_find
	    (box, le32toh(*(uint32_t *)buf))) == NULL) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_stmt_find");
		return 0;
	}
	allow_cstep = le32toh(*(uint32_t *)(buf + sizeof(uint32_t)));

	/* 
	 * Immediately write any cached responses.
	 * If the last caching set our "done" marker, then don't try to
	 * read any additional results.
	 * If we come back into this function we'll hit the limitation
	 * that we can't step an already-stepped function.
	 */

	if (st->res.bufsz) {
		if (!sqlbox_write(box, st->res.buf, st->res.bufsz)) {
			sqlbox_warnx(&box->cfg, "%s: step: "
				"sqlbox_write", st->db->src->fname);
			return 0;
		}
		done = st->res.done;
		sqlbox_res_clear(&st->res);
		st->res.done = done;
		if (done)
			return 1;
	}

	/* 
	 * No cached responses.
	 * Did we already run this to completion?
	 */

	if (st->res.done) {
		sqlbox_warnx(&box->cfg, "%s: step: already "
			"stepped", st->db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: statement: %s", 
			st->db->src->fname, st->pstmt->stmt);
		return 0;
	}

	st->res.bufsz = SQLBOX_FRAME;
	st->res.buf = calloc(1, st->res.bufsz);
	if (st->res.buf == NULL) {
		sqlbox_warn(&box->cfg, "step: calloc");
		return 0;
	}

	pos = sizeof(uint32_t);
	rc = sqlbox_pack_step(box, &pos, st, allow_cstep);
	if (rc < 0) {
		sqlbox_warnx(&box->cfg, "%s: step: "
			"sqlbox_pack_step", st->db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: statement: %s",
			st->db->src->fname, st->pstmt->stmt);
		return 0;
	} else if (rc == 0)
		st->res.done = 1;

	/* Now write our frame size. */

	val = htole32(pos - sizeof(uint32_t));
	memcpy(st->res.buf, (char *)&val, sizeof(uint32_t));

	/* The frame will be at least SQLBOX_FRAME long. */

	pos = pos > SQLBOX_FRAME ? pos : SQLBOX_FRAME;
	if (!sqlbox_write(box, st->res.buf, pos)) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_write");
		return 0;
	}

	done = st->res.done;
	sqlbox_res_clear(&st->res);
	st->res.done = done;
	return 1;
}
