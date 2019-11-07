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
	sqlbox_warnx(&box->cfg, "exec: statement "
		"%zu denied to role %zu", idx, box->role);
	return 0;
}

/*
 * Shared by both asynchronous and synchronous version.
 * Sends the exec instruction to the server.
 * The caller should handle any possible synchrony.
 * Returns TRUE on success, FALSE on failure.
 */
static int
sqlbox_exec_inner(struct sqlbox *box,
	enum sqlbox_op op, size_t srcid, size_t pstmt, size_t psz, 
	const struct sqlbox_parm *ps, unsigned long flags)
{
	size_t		 pos = 0, bufsz = SQLBOX_FRAME, i;
	uint32_t	 val;
	char		*buf;

	/* 
	 * Make sure explicit-sized strings are NUL terminated.
	 * FIXME: kill server on error.
	 */

	for (i = 0; i < psz; i++) 
		if (ps[i].type == SQLBOX_PARM_STRING &&
		    ps[i].sz > 0 &&
		    ps[i].sparm[ps[i].sz - 1] != '\0') {
			sqlbox_warnx(&box->cfg, "exec: "
				"parameter %zu is malformed", i);
			return 0;
		}

	if ((buf = calloc(bufsz, 1)) == NULL) {
		sqlbox_warn(&box->cfg, "exec: calloc");
		return 0;
	}

	/* Skip the frame size til we get the packed parms. */

	pos = sizeof(uint32_t);

	/* Pack operation, source, statement, and parameters. */

	val = htole32(op);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	val = htole32(flags);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	val = htole32(srcid);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	val = htole32(pstmt);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	if (!sqlbox_parm_pack(box, psz, ps, &buf, &pos, &bufsz)) {
		sqlbox_warnx(&box->cfg, "exec: sqlbox_parm_pack");
		free(buf);
		return 0;
	}

	/* Go back and do the frame size. */

	val = htole32(pos - 4);
	memcpy(buf, (char *)&val, sizeof(uint32_t));

	/* Write data, free our buffer. */

	if (!sqlbox_write(box, buf, pos > SQLBOX_FRAME ? pos : SQLBOX_FRAME)) {
		sqlbox_warnx(&box->cfg, "exec: sqlbox_write");
		free(buf);
		return 0;
	}

	free(buf);
	return 1;
}

int
sqlbox_exec_async(struct sqlbox *box, size_t srcid, size_t pstmt, 
	size_t psz, const struct sqlbox_parm *ps, unsigned long opts)
{

	if (!sqlbox_exec_inner(box, SQLBOX_OP_EXEC_ASYNC, 
	    srcid, pstmt, psz, ps, opts)) {
		sqlbox_warnx(&box->cfg, "exec-async: sqlbox_exec_inner");
		return 0;
	}

	return 1;
}

enum sqlbox_code
sqlbox_exec(struct sqlbox *box, size_t srcid, size_t pstmt, 
	size_t psz, const struct sqlbox_parm *ps, unsigned long opts)
{
	uint32_t	 val;

	if (!sqlbox_exec_inner(box, SQLBOX_OP_EXEC_SYNC,
	    srcid, pstmt, psz, ps, opts)) {
		sqlbox_warnx(&box->cfg, "exec-sync: sqlbox_exec_inner");
		return SQLBOX_CODE_ERROR;
	}

	if (!sqlbox_read(box, (char *)&val, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "exec-sync: sqlbox_read");
		return SQLBOX_CODE_ERROR;
	}

	return (enum sqlbox_code)val;
}

/*
 * Prepare and bind parameters to a statement in one step.
 * Do not send anything back to the client: this is done by the caller
 * depending upon the mode.
 * Return the allocated statement on success, NULL on failure.
 */
static enum sqlbox_code
sqlbox_op_exec(struct sqlbox *box, const char *buf, size_t sz)
{
	size_t	 		 idx, cols, psz, parmsz;
	struct sqlbox_db	*db;
	sqlite3_stmt		*stmt = NULL;
	struct sqlbox_pstmt	*pst = NULL;
	struct sqlbox_parm	*parms = NULL;
	enum sqlbox_code	 code;
	unsigned long		 flags;

	/* 
	 * Read the source identifier, whether we can have constraints,
	 * and the statement identifier. 
	 */

	if (sz < sizeof(uint32_t) * 3) {
		sqlbox_warnx(&box->cfg, "exec: bad frame size");
		return SQLBOX_CODE_ERROR;
	}

	flags = le32toh(*(uint32_t *)buf);
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	db = sqlbox_db_find(box, le32toh(*(uint32_t *)buf));
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	if (db == NULL) {
		sqlbox_warnx(&box->cfg, "exec: sqlbox_db_find");
		return SQLBOX_CODE_ERROR;
	}

	idx = le32toh(*(uint32_t *)buf);
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	if (idx >= box->cfg.stmts.stmtsz) {
		sqlbox_warnx(&box->cfg, "%s: exec: "
			"bad statement %zu", db->src->fname, idx);
		return SQLBOX_CODE_ERROR;
	}
	if (!sqlbox_rolecheck_stmt(box, idx)) {
		sqlbox_warnx(&box->cfg, "%s: exec: "
			"sqlbox_rolecheck_stmt", db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: exec: "
			"statement: %s", db->src->fname, 
			box->cfg.stmts.stmts[idx].stmt);
		return SQLBOX_CODE_ERROR;
	}
	pst = &box->cfg.stmts.stmts[idx];

	/* Now the parameters. */

	psz = sqlbox_parm_unpack(box, &parms, &parmsz, buf, sz);
	if (psz == 0) {
		sqlbox_warnx(&box->cfg, "%s: exec: "
			"sqlbox_parm_unpack", db->src->fname);
		free(parms);
		return SQLBOX_CODE_ERROR;
	}
	buf += psz;
	sz -= psz;
	if (sz != 0) {
		sqlbox_warnx(&box->cfg, "exec: bad frame size");
		free(parms);
		return SQLBOX_CODE_ERROR;
	}

	/*
	 * If we have no parameters, short-circuit into using sqlite3's
	 * "exec" function instead of the whole cycle of preparation,
	 * stepping, and freeing.
	 */

	if (parmsz == 0) {
		code = sqlbox_wrap_exec(box, db, 
			pst, (flags & SQLBOX_STMT_CONSTRAINT));
		if (code == SQLBOX_CODE_ERROR) {
			sqlbox_warnx(&box->cfg, 
				"%s: exec: sqlbox_wrap_exec", 
				db->src->fname);
			sqlbox_warnx(&box->cfg, "%s: exec: "
				"statement: %s", 
				db->src->fname, pst->stmt);
		}
	} else {
		if ((stmt = sqlbox_wrap_prep(box, db, pst)) == NULL) {
			sqlbox_warnx(&box->cfg, 
				"%s: exec: sqlbox_wrap_prep", 
				db->src->fname);
			sqlbox_warnx(&box->cfg, "%s: exec: "
				"statement: %s", 
				db->src->fname, pst->stmt);
			free(parms);
			return SQLBOX_CODE_ERROR;
		}

		if (!sqlbox_parm_bind
		    (box, db, pst, stmt, parms, parmsz)) {
			sqlbox_warnx(&box->cfg, 
				"%s: sqlbox_parm_bind",
				db->src->fname);
			sqlbox_warnx(&box->cfg, "%s: exec: "
				"statement: %s", 
				db->src->fname, pst->stmt);
			sqlbox_wrap_finalise(box, db, pst, stmt);
			free(parms);
			return SQLBOX_CODE_ERROR;
		}
		free(parms);

		code = sqlbox_wrap_step(box, db, pst, stmt, 
			&cols, (flags & SQLBOX_STMT_CONSTRAINT));
		if (code == SQLBOX_CODE_ERROR) {
			sqlbox_warnx(&box->cfg, 
				"%s: exec: sqlbox_wrap_step", 
				db->src->fname);
			sqlbox_warnx(&box->cfg, "%s: exec: "
				"statement: %s", 
				db->src->fname, pst->stmt);
		} else if (cols > 0) {
			sqlbox_warnx(&box->cfg, 
				"%s: exec: sqlbox_wrap_step: "
				"ignoring %zu columns", 
				db->src->fname, cols);
			sqlbox_warnx(&box->cfg, "%s: exec: "
				"statement: %s", 
				db->src->fname, pst->stmt);
		}
		sqlbox_wrap_finalise(box, db, pst, stmt);
	}

	return code;
}

int
sqlbox_op_exec_sync(struct sqlbox *box, const char *buf, size_t sz)
{
	enum sqlbox_code	 code;
	uint32_t		 ack;

	code = sqlbox_op_exec(box, buf, sz);
	if (code == SQLBOX_CODE_ERROR) {
		sqlbox_warnx(&box->cfg, "exec-sync: sqlbox_op_exec");
		return 0;
	}

	/* Synchronous version writes back the code. */

	ack = htole32(code);
	if (sqlbox_write(box, (char *)&ack, sizeof(uint32_t)))
		return 1;
	sqlbox_warnx(&box->cfg, "exec-sync: sqlbox_write");
	return 0;
}

int
sqlbox_op_exec_async(struct sqlbox *box, const char *buf, size_t sz)
{
	enum sqlbox_code	 code;

	code = sqlbox_op_exec(box, buf, sz);
	if (code == SQLBOX_CODE_ERROR) {
		sqlbox_warnx(&box->cfg, "exec-async: sqlbox_op_exec");
		return 0;
	}

	return 1;
}
