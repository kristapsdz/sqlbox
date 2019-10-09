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
#include <endian.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

int
sqlbox_parm_pack(struct sqlbox *box, size_t parmsz,
	const struct sqlbox_parm *parms, 
	char **buf, size_t *offs, size_t *bufsz)
{
	size_t	 framesz, i, sz;
	void	*pp;
	uint32_t tmp;
	int64_t	 val;

	/* Start with types and payload size. */

	framesz = sizeof(uint32_t) + parmsz * sizeof(uint32_t);

	for (i = 0; i < parmsz; i++) {
		switch (parms[i].type) {
		case SQLBOX_PARM_NULL: /* nothing */
			break;
		case SQLBOX_PARM_FLOAT:
			framesz += sizeof(double);
			break;
		case SQLBOX_PARM_INT:
			framesz += sizeof(int64_t);
			break;
		case SQLBOX_PARM_BLOB: /* length+data */
			framesz += sizeof(uint32_t);
			framesz += parms[i].sz;
			break;
		case SQLBOX_PARM_STRING:
			framesz += sizeof(uint32_t);
			framesz += parms[i].sz == 0 ? 
				strlen(parms[i].sparm) + 1 : parms[i].sz;
			break;
		default:
			return 0;
		}
	}

	/* Allocate our write buffer. */

	if (*offs + framesz > *bufsz) {
		if ((pp = realloc(*buf, *offs + framesz)) == NULL) {
			sqlbox_warn(&box->cfg, "realloc");
			return 0;
		}
		*buf = pp;
		*bufsz = *offs + framesz;
	}

	/* Prologue: param size. */

	tmp = htole32(parmsz);
	memcpy(*buf + *offs, (char *)&tmp, sizeof(uint32_t));
	*offs += sizeof(uint32_t);

	/* Now all parameter data. */

	for (i = 0; i < parmsz; i++) {
		tmp = htole32(parms[i].type);
		memcpy(*buf + *offs, (char *)&tmp, sizeof(uint32_t));
		*offs += sizeof(uint32_t);
		switch (parms[i].type) {
		case SQLBOX_PARM_FLOAT:
			memcpy(*buf + *offs, 
				(char *)&parms[i].fparm, sizeof(double));
			*offs += sizeof(double);
			break;
		case SQLBOX_PARM_INT:
			val = htole64(parms[i].iparm);
			memcpy(*buf + *offs, 
				(char *)&val, sizeof(int64_t));
			*offs += sizeof(int64_t);
			break;
		case SQLBOX_PARM_NULL:
			break;
		case SQLBOX_PARM_BLOB:
			val = htole32(parms[i].sz);
			memcpy(*buf + *offs, 
				(char *)&val, sizeof(uint32_t));
			*offs += sizeof(uint32_t);
			memcpy(*buf + *offs, parms[i].bparm, parms[i].sz);
			*offs += parms[i].sz;
			break;
		case SQLBOX_PARM_STRING:
			sz = parms[i].sz == 0 ? 
				strlen(parms[i].sparm) + 1 : parms[i].sz;
			assert(sz > 0);
			val = htole32(sz);
			memcpy(*buf + *offs, 
				(char *)&val, sizeof(uint32_t));
			*offs += sizeof(uint32_t);
			memcpy(*buf + *offs, parms[i].sparm, sz);
			*offs += sz;
			break;
		default:
			abort();
		}
	}

	return 1;
}

/* 
 * Bind parameters in "parms" to a statement "stmt".
 * We mark the strings as SQLITE_TRANSIENT because we're probably going
 * to lose the buffer during the next read and so it needs to be stored.
 * Returns TRUE on success, FALSE on failure.
 */
int
sqlbox_parm_bind(struct sqlbox *box, struct sqlbox_db *db,
	const struct sqlbox_pstmt *pst, sqlite3_stmt *stmt, 
	const struct sqlbox_parm *parms, size_t parmsz)
{
	size_t	 i;
	int	 c;

	for (i = 0; i < parmsz; i++) {
		switch (parms[i].type) {
		case SQLBOX_PARM_BLOB:
			sqlbox_debug(&box->cfg, 
				"%s: sqlite3_bind_blob[%zu]: "
				"%s (%zu B)", db->src->fname,
				i, pst->stmt, parms[i].sz);
			c = sqlite3_bind_blob(stmt, i + 1,
				parms[i].bparm, parms[i].sz, 
				SQLITE_TRANSIENT);
			break;
		case SQLBOX_PARM_FLOAT:
			sqlbox_debug(&box->cfg, 
				"%s: sqlite3_bind_double[%zu]: "
				"%s", db->src->fname, i, pst->stmt);
			c = sqlite3_bind_double
				(stmt, i + 1, parms[i].fparm);
			break;
		case SQLBOX_PARM_INT:
			sqlbox_debug(&box->cfg, 
				"%s: sqlite3_bind_int64[%zu]: "
				"%s", db->src->fname, i, pst->stmt);
			c = sqlite3_bind_int64
				(stmt, i + 1, parms[i].iparm);
			break;
		case SQLBOX_PARM_NULL:
			sqlbox_debug(&box->cfg, 
				"%s: sqlite3_bind_null[%zu]: "
				"%s", db->src->fname, i, pst->stmt);
			c = sqlite3_bind_null(stmt, i + 1);
			break;
		case SQLBOX_PARM_STRING:
			sqlbox_debug(&box->cfg, 
				"%s: sqlite3_bind_text[%zu]: "
				"%s (%zu B)", db->src->fname, i,
				pst->stmt, parms[i].sz - 1);
			c = sqlite3_bind_text(stmt, i + 1,
				parms[i].sparm, parms[i].sz - 1, 
				SQLITE_TRANSIENT);
			break;
		default:
			sqlbox_warnx(&box->cfg, 
				"%s: sqlbox_parm_bind[%zu]: "
				"bad type: %d", db->src->fname, 
				i, parms[i].type);
			sqlbox_warnx(&box->cfg, 
				"%s: sqlbox_parm_bind[%zu]: "
				"statement: %s", db->src->fname, 
				i, pst->stmt);
			return 0;
		}
		if (c != SQLITE_OK) {
			sqlbox_warnx(&box->cfg, 
				"%s: sqlbox_parm_bind[%zu]: %s", 
				db->src->fname, i, 
				sqlite3_errmsg(db->db));
			sqlbox_warnx(&box->cfg, 
				"%s: sqlbox_parm_bind[%zu]: "
				"statement: %s", db->src->fname, 
				i, pst->stmt);
			return 0;
		}
	}

	return 1;
}

/*
 * Unpack a set of sqlbox_parm from the buffer.
 * Returns zero on failure or the number of bytes processed on success.
 * On failure, no [new] memory is allocated into the result pointers.
 */
size_t
sqlbox_parm_unpack(struct sqlbox *box, struct sqlbox_parm **parms, 
	ssize_t *parmsz, const char *buf, size_t bufsz)
{
	size_t	 	 i = 0, len, psz;
	const char	*start = buf;

	if (bufsz < sizeof(uint32_t))
		goto badframe;

	/* Read prologue: param size. */

	psz = le32toh(*(const uint32_t *)buf);
	buf += sizeof(uint32_t);
	bufsz -= sizeof(uint32_t);

	/* 
	 * Allocate stored parameters.
	 * If parmsz < 0, that means that we haven't allocated anything
	 * so we can set the first allocation size.
	 * If parmsz == 0, then we've already run the query.
	 * Subsequent allocation sizes *must* match the first, even if
	 * it's zero.
	 */

	if (*parmsz == 0) {
		sqlbox_warnx(&box->cfg, "parameters were already zero");
		return 0;
	} else if (*parmsz < 0 && psz > 0) {
		*parms = calloc(psz, sizeof(struct sqlbox_parm));
		if (*parms == NULL) {
			sqlbox_warn(&box->cfg, "calloc");
			return 0;
		}
		*parmsz = psz;
	} else if (*parmsz < 0) {
		*parms = NULL;
		*parmsz = 0;
	} else if (psz && (size_t)*parmsz != psz) {
		sqlbox_warnx(&box->cfg, "parameter length mismatch "
			"(have %zu, %zu in parameters)", *parmsz, psz);
		return 0;
	} else if (psz == 0)
		*parmsz = 0;

	/* 
	 * Copy out into our parameters.
	 * Note that we don't copy out strings: we index directly into
	 * the array for that.
	 */

	assert(*parmsz >= 0);
	for (i = 0; i < (size_t)*parmsz; i++) {
		if (bufsz < sizeof(uint32_t))
			goto badframe;
		(*parms)[i].type = le32toh(*(uint32_t *)buf);
		buf += sizeof(uint32_t);
		bufsz -= sizeof(uint32_t);
		switch ((*parms)[i].type) {
		case SQLBOX_PARM_FLOAT:
			if (bufsz < sizeof(double))
				goto badframe;
			(*parms)[i].sz = sizeof(double);
			(*parms)[i].fparm = *(double *)buf;
			buf += sizeof(double);
			bufsz -= sizeof(double);
			break;
		case SQLBOX_PARM_INT:
			if (bufsz < sizeof(int64_t))
				goto badframe;
			(*parms)[i].sz = sizeof(int64_t);
			(*parms)[i].iparm = le64toh(*(int64_t *)buf);
			buf += sizeof(int64_t);
			bufsz -= sizeof(int64_t);
			break;
		case SQLBOX_PARM_NULL:
			(*parms)[i].sz = 0;
			break;
		case SQLBOX_PARM_BLOB:
			if (bufsz < sizeof(uint32_t))
				goto badframe;
			len = le32toh(*(uint32_t *)buf);
			buf += sizeof(uint32_t);
			bufsz -= sizeof(uint32_t);
			if (bufsz < len)
				goto badframe;
			(*parms)[i].bparm = buf;
			(*parms)[i].sz = len;
			buf += len;
			bufsz -= len;
			break;
		case SQLBOX_PARM_STRING:
			if (bufsz < sizeof(uint32_t))
				goto badframe;
			len = le32toh(*(uint32_t *)buf);
			if (len == 0) {
				sqlbox_warnx(&box->cfg, "unpacking "
					"parameter %zu: string "
					"malformed", i);
				goto err;
			}
			buf += sizeof(uint32_t);
			bufsz -= sizeof(uint32_t);
			if (bufsz < len)
				goto badframe;
			(*parms)[i].sparm = buf;
			(*parms)[i].sz = len;
			if (buf[len - 1] != '\0') {
				sqlbox_warnx(&box->cfg, "unpacking "
					"parameter %zu: string "
					"malformed", i);
				goto err;
			}
			buf += len;
			bufsz -= len;
			break;
		default:
			sqlbox_warnx(&box->cfg, "unpacking parameter "
				"%zu: unknown type: %d", i, 
				(*parms)[i].type);
			goto err;
		}
	}

	assert(buf > start);
	return (size_t)(buf - start);
badframe:
	sqlbox_warnx(&box->cfg, "unpacking "
		"parameter %zu: invalid frame size", i);
err:
	free(*parms);
	*parms = NULL;
	*parmsz = 0;
	return 0;
}
