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

int
sqlbox_rebind(struct sqlbox *box, size_t id,
	size_t psz, const struct sqlbox_parm *ps)
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
			sqlbox_warnx(&box->cfg, "rebind: "
				"parameter %zu is malformed", i);
			return 0;
		}

	/*
	 * Grab client's last record.
	 * FIXME: kill server if we don't find it.
	 */

	if ((st = sqlbox_stmt_find(box, id)) == NULL) {
		sqlbox_warnx(&box->cfg, "rebind: sqlbox_stmt_find");
		return 0;
	} else if ((buf = calloc(bufsz, 1)) == NULL) {
		sqlbox_warn(&box->cfg, "rebind: calloc");
		return 0;
	}

	/* Skip the frame size til we get the packed parms. */

	pos = sizeof(uint32_t);

	/* Pack operation, source, statement, and parameters. */

	val = htole32(SQLBOX_OP_REBIND);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	val = htole32(id);
	memcpy(buf + pos, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	if (!sqlbox_parm_pack(box, psz, ps, &buf, &pos, &bufsz)) {
		sqlbox_warnx(&box->cfg, "rebind: sqlbox_parm_pack");
		free(buf);
		return 0;
	}

	/* Go back and do the frame size. */

	val = htole32(pos - 4);
	memcpy(buf, (char *)&val, sizeof(uint32_t));

	/* Write data, free our buffer. */

	if (!sqlbox_write(box, buf,
	    pos > SQLBOX_FRAME ? pos : SQLBOX_FRAME)) {
		sqlbox_warnx(&box->cfg, "rebind: sqlbox_write");
		free(buf);
		return 0;
	}
	free(buf);

	/* Remove any pending results. */

	sqlbox_res_clear(&st->res);
	return 1;
}

/*
 * Prepare and bind parameters to a statement in one step.
 * Return TRUE on success, FALSE on failure (nothing is allocated).
 */
int
sqlbox_op_rebind(struct sqlbox *box, const char *buf, size_t sz)
{
	size_t	 		 i, psz, parmsz;
	struct sqlbox_stmt	*st;
	int			 c;
	struct sqlbox_parm	*parms = NULL;

	/* Read the source identifier. */

	if (sz < sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "rebind: bad frame size");
		return 0;
	}
	if ((st = sqlbox_stmt_find
	    (box, le32toh(*(uint32_t *)buf))) == NULL) {
		sqlbox_warnx(&box->cfg, "rebind: sqlbox_stmt_find");
		return 0;
	}
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	/* 
	 * Don't check the return code, as this just returns whatever
	 * was there last.
	 */
	sqlbox_debug(&box->cfg, "sqlite3_reset: %s, %s",
		st->db->src->fname, st->pstmt->stmt);
	sqlite3_reset(st->stmt);

	sqlbox_debug(&box->cfg, "sqlite3_clear_bindings: %s, %s",
		st->db->src->fname, st->pstmt->stmt);
	if (sqlite3_clear_bindings(st->stmt) != SQLITE_OK) {
		sqlbox_warnx(&box->cfg, "%s: sqlite3_clear_bindings: %s", 
			st->db->src->fname, sqlite3_errmsg(st->db->db));
		sqlbox_warnx(&box->cfg, "%s: rebind statement: %s", 
			st->db->src->fname, st->pstmt->stmt);
		return 0;
	}

	/* Now the parameters. */

	psz = sqlbox_parm_unpack(box, &parms, &parmsz, buf, sz);
	if (psz == 0) {
		sqlbox_warnx(&box->cfg, "%s: rebind: "
			"sqlbox_parm_unpack", st->db->src->fname);
		sqlbox_warnx(&box->cfg, "%s: rebind statement: %s", 
			st->db->src->fname, st->pstmt->stmt);
		free(parms);
		return 0;
	}
	sz -= psz;
	buf += psz;
	if (sz != 0) {
		sqlbox_warnx(&box->cfg, "rebind: bad frame size");
		free(parms);
		return 0;
	}

	/* 
	 * Bind parameters.
	 * Note that we mark the strings as SQLITE_TRANSIENT because
	 * we're probably going to lose the buffer during the next read
	 * and so it needs to be stored.
	 */

	for (i = 0; i < (size_t)parmsz; i++) {
		switch (parms[i].type) {
		case SQLBOX_PARM_BLOB:
			c = sqlite3_bind_blob(st->stmt, i + 1,
				parms[i].bparm, parms[i].sz, 
				SQLITE_TRANSIENT);
			sqlbox_debug(&box->cfg, "sqlite3_bind_blob[%zu]: "
				"%s, %s (%zu bytes)", i, 
				st->db->src->fname, 
				st->pstmt->stmt, parms[i].sz - 1);
			break;
		case SQLBOX_PARM_FLOAT:
			c = sqlite3_bind_double
				(st->stmt, i + 1, parms[i].fparm);
			sqlbox_debug(&box->cfg, 
				"sqlite3_bind_double[%zu]: %s, %s", i,
				st->db->src->fname, st->pstmt->stmt);
			break;
		case SQLBOX_PARM_INT:
			c = sqlite3_bind_int64
				(st->stmt, i + 1, parms[i].iparm);
			sqlbox_debug(&box->cfg,
				"sqlite3_bind_int64[%zu]: %s, %s", i, 
				st->db->src->fname, st->pstmt->stmt);
			break;
		case SQLBOX_PARM_NULL:
			c = sqlite3_bind_null(st->stmt, i + 1);
			sqlbox_debug(&box->cfg, 
				"sqlite3_bind_null[%zu]: %s, %s", i, 
				st->db->src->fname, st->pstmt->stmt);
			break;
		case SQLBOX_PARM_STRING:
			c = sqlite3_bind_text(st->stmt, i + 1,
				parms[i].sparm, parms[i].sz - 1, 
				SQLITE_TRANSIENT);
			sqlbox_debug(&box->cfg, 
				"sqlite3_bind_text[%zu]: %s, %s "
				"(%zu bytes)", i, st->db->src->fname, 
				st->pstmt->stmt, parms[i].sz - 1);
			break;
		default:
			sqlbox_warnx(&box->cfg, "%s: rebind: "
				"bad type: %d", st->db->src->fname, 
				parms[i].type);
			sqlbox_warnx(&box->cfg, "%s: rebind: "
				"statement: %s", st->db->src->fname, 
				st->pstmt->stmt);
			free(parms);
			return 0;
		}
		if (c != SQLITE_OK) {
			sqlbox_warnx(&box->cfg, "%s: rebind: %s", 
				st->db->src->fname, 
				sqlite3_errmsg(st->db->db));
			sqlbox_warnx(&box->cfg, "%s: rebind: "
				"statement: %s", 
				st->db->src->fname, st->pstmt->stmt);
			free(parms);
			return 0;
		}
	}

	free(parms);
	
	/* Now get ready for new stepping. */

	sqlbox_res_clear(&st->res);
	return 1;
}

