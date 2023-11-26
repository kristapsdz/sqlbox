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
#include COMPAT_ENDIAN_H
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

/*
 * When we're caching results, cache at most 10 times the frame size.
 * XXX: this will probably end up being tuned or dynamically set.
 */
#define	SQLBOX_CACHE_MAX (SQLBOX_FRAME * 10)

struct	freen {
	void			 *dat;
	void			(*fp)(void *);
	TAILQ_ENTRY(freen)	 entries;
};

TAILQ_HEAD(freeq, freen);

const struct sqlbox_parmset *
sqlbox_step(struct sqlbox *box, size_t stmtid)
{
	uint32_t		 val;
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

	/* Write id frame. */

	val = htole32(stmtid);
	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_STEP, (char *)&val, sizeof(uint32_t))) {
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

/*
 * Read a single result from the wire and append it to the packed
 * parameters we already have in our buffer.
 * Return <0 on error, 0 if there are no results, >0 if we have a row.
 */
static int
sqlbox_pack_step(struct sqlbox *box, size_t *bufpos, struct sqlbox_stmt *st)
{
	enum sqlbox_code	 code;
	struct sqlbox_parmset	 set;
	size_t			 cols = 0, i, j;
	int			 has_cstep = 0, rc = -1;
	void			*pp, *arg;
	struct freeq		 fq;
	struct freen		*fn;
	uint32_t		 val;

	TAILQ_INIT(&fq);

	/* Start with the step itself. */

	code = sqlbox_wrap_step(box, st->db, 
		st->pstmt, st->stmt, &cols, 
		(st->flags & SQLBOX_STMT_CONSTRAINT));
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
		pp = realloc(st->res.buf, *bufpos + SQLBOX_FRAME);
		if (pp == NULL) {
			sqlbox_warn(&box->cfg, "step: realloc");
			goto out;
		}

		/* Initialise the memory. */

		memset(pp + st->res.bufsz, 0,
			(*bufpos + SQLBOX_FRAME) - st->res.bufsz);
		st->res.buf = pp;
		st->res.bufsz = *bufpos + SQLBOX_FRAME;
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
	rc = (cols > 0);

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
	size_t			 i, pos;
	int			 rc, done, wrote = 0;
	uint32_t		 val;
	
	/* Look up the statement in our global list. */

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "step: bad frame size");
		return 0;
	}
	if ((st = sqlbox_stmt_find
	    (box, le32toh(*(uint32_t *)buf))) == NULL) {
		sqlbox_warnx(&box->cfg, "step: sqlbox_stmt_find");
		return 0;
	}

	/* 
	 * Immediately write any cached responses.
	 * Make sure to update our "done" marker after clearing out the
	 * buffer of rows, which sqlbox_res_clear() otherwise clears.
	 */

	if (st->res.bufsz) {
		if (!sqlbox_write(box, st->res.buf, st->res.bufsz)) {
			sqlbox_warnx(&box->cfg, "%s: step: "
				"sqlbox_write", st->db->src->fname);
			return 0;
		}
		wrote = 1;
		done = st->res.done;
		sqlbox_res_clear(&st->res);
		st->res.done = done;
	}

	/* 
	 * No cached responses: did we already run this to completion?
	 * This is an ok situation if we just wrote a response message
	 * which would indicate that we're done, as this isn't a retry.
	 */

	if (st->res.done) {
		if (wrote)
			return 1;
		sqlbox_warnx(&box->cfg, "%s: step: already "
			"stepped", st->db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: statement: %s", 
			st->db->src->fname, st->pstmt->stmt);
		return 0;
	}

	/*
	 * If we haven't written anything yet, which is the norm for
	 * non-multi stepping, then perform a single step and write it.
	 */

	if (!wrote) {
		st->res.bufsz = SQLBOX_FRAME;
		st->res.buf = calloc(st->res.bufsz, 1);
		if (st->res.buf == NULL) {
			sqlbox_warn(&box->cfg, "step: calloc");
			return 0;
		}
		pos = sizeof(uint32_t);
		if ((rc = sqlbox_pack_step(box, &pos, st)) < 0) {
			sqlbox_warnx(&box->cfg, "%s: step: "
				"sqlbox_pack_step", st->db->src->fname);
			sqlbox_warnx(&box->cfg, "%s: statement: %s",
				st->db->src->fname, st->pstmt->stmt);
			return 0;
		} else if (rc == 0)
			st->res.done = 1;

		val = htole32(pos - sizeof(uint32_t));
		memcpy(st->res.buf, (char *)&val, sizeof(uint32_t));
		pos = pos > SQLBOX_FRAME ? pos : SQLBOX_FRAME;
		if (!sqlbox_write(box, st->res.buf, pos)) {
			sqlbox_warnx(&box->cfg, "step: sqlbox_write");
			return 0;
		}
		done = st->res.done;
		sqlbox_res_clear(&st->res);
		st->res.done = done;

		/*
		 * If we're doing a multi-step and we just wrote some
		 * data, then continue to collect for the next request.
		 */

		if ((st->flags & SQLBOX_STMT_MULTI))
			wrote = 1;
	} 

	/*
	 * We've written data but want to cache up for the next call to
	 * this function.
	 */

	if (wrote && !st->res.done) {
		assert(st->res.bufsz == 0);
		st->res.bufsz = SQLBOX_FRAME;
		st->res.buf = calloc(st->res.bufsz, 1);
		if (st->res.buf == NULL) {
			sqlbox_warn(&box->cfg, "step: calloc");
			return 0;
		}
		pos = sizeof(uint32_t);
		assert(!st->res.done);
		i = 0;
		while (pos < SQLBOX_CACHE_MAX) {
			rc = sqlbox_pack_step(box, &pos, st);
			if (rc < 0) {
				sqlbox_warnx(&box->cfg, "%s: step: "
					"sqlbox_pack_step (multi)", 
					st->db->src->fname);
				sqlbox_warnx(&box->cfg, 
					"%s: statement: %s",
					st->db->src->fname, 
					st->pstmt->stmt);
				return 0;
			} else if (rc == 0) {
				st->res.done = 1;
				break;
			}
			i++;
		}
		val = htole32(pos - sizeof(uint32_t));
		memcpy(st->res.buf, (char *)&val, sizeof(uint32_t));
		st->res.bufsz = pos > SQLBOX_FRAME ? pos : SQLBOX_FRAME;
	}

	return 1;
}
