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
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h> /* HUGE_VAL */
#include <stdio.h> /* asprintf */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

/*
 * Make sure that the value of "framesz" aligns on "algn" border.
 * If it does not, pad it upward.
 */
static void
sqlbox_parm_pack_align(struct sqlbox *box, size_t *framesz, size_t algn)
{

	if ((*framesz % algn) != 0)
		*framesz += algn - (*framesz % algn);
}

/*
 * Pack the "parmsz" parameters in "parm" into "buf", which is currently
 * filled to "offs" and with total size "bufsz".
 * The written buffer is aligned on a 4-byte boundary.
 */
int
sqlbox_parm_pack(struct sqlbox *box, size_t parmsz,
	const struct sqlbox_parm *parms, 
	char **buf, size_t *offs, size_t *bufsz)
{
	size_t	 framesz, i, sz;
	void	*pp;
	uint32_t tmp;
	uint64_t val;

	/* Start with 8-byte padding and number of frames. */

	framesz = 8 - (*offs % 8);
	framesz += sizeof(uint32_t);

	/*
	 * Each parameter must be 4-byte aligned.
	 * This might be violated by PARM_BLOB or PARM_STRING, both of
	 * which introduce arbitrary byte counts.
	 */

	for (i = 0; i < parmsz; i++) {
		sqlbox_parm_pack_align(box, &framesz, 4);
		framesz += sizeof(uint32_t);
		switch (parms[i].type) {
		case SQLBOX_PARM_NULL: /* nothing */
			break;
		case SQLBOX_PARM_FLOAT:
			sqlbox_parm_pack_align(box, &framesz, 8);
			framesz += sizeof(double);
			break;
		case SQLBOX_PARM_INT:
			sqlbox_parm_pack_align(box, &framesz, 8);
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

	/* End on a 4-byte boundary. */

	sqlbox_parm_pack_align(box, &framesz, 4);

	/* Allocate our write buffer. */

	if (*offs + framesz > *bufsz) {
		if ((pp = realloc(*buf, *offs + framesz)) == NULL) {
			sqlbox_warn(&box->cfg, "realloc");
			return 0;
		}
		memset(pp + *bufsz, 0, (*offs + framesz) - *bufsz);
		*buf = pp;
		*bufsz = *offs + framesz;
	}

	/* Prologue: 8-byte padding and param size. */

	sqlbox_parm_pack_align(box, offs, 8);
	tmp = htole32(parmsz);
	memcpy(*buf + *offs, (char *)&tmp, sizeof(uint32_t));
	*offs += sizeof(uint32_t);

	/* 
	 * Now all parameter data.
	 * Each parameter aligns on a 4-byte boundary.
	 */

	for (i = 0; i < parmsz; i++) {
		sqlbox_parm_pack_align(box, offs, 4);
		tmp = htole32(parms[i].type);
		memcpy(*buf + *offs, (char *)&tmp, sizeof(uint32_t));
		*offs += sizeof(uint32_t);
		switch (parms[i].type) {
		case SQLBOX_PARM_FLOAT:
			sqlbox_parm_pack_align(box, offs, 8);
			memcpy(*buf + *offs, 
				(char *)&parms[i].fparm, sizeof(double));
			*offs += sizeof(double);
			break;
		case SQLBOX_PARM_INT:
			sqlbox_parm_pack_align(box, offs, 8);
			val = htole64(parms[i].iparm);
			memcpy(*buf + *offs, (char *)&val, sizeof(int64_t));
			*offs += sizeof(int64_t);
			break;
		case SQLBOX_PARM_NULL:
			break;
		case SQLBOX_PARM_BLOB:
			tmp = htole32(parms[i].sz);
			memcpy(*buf + *offs, 
				(char *)&tmp, sizeof(uint32_t));
			*offs += sizeof(uint32_t);
			memcpy(*buf + *offs, parms[i].bparm, parms[i].sz);
			*offs += parms[i].sz;
			break;
		case SQLBOX_PARM_STRING:
			sz = parms[i].sz == 0 ? 
				strlen(parms[i].sparm) + 1 : parms[i].sz;
			assert(sz > 0);
			tmp = htole32(sz);
			memcpy(*buf + *offs, 
				(char *)&tmp, sizeof(uint32_t));
			*offs += sizeof(uint32_t);
			memcpy(*buf + *offs, parms[i].sparm, sz);
			*offs += sz;
			break;
		default:
			abort();
		}
	}

	/* Epilogue is a 4-byte boundary. */

	sqlbox_parm_pack_align(box, offs, 4);
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
 * Align the buffer "buf" of size "bufsz" on "algn" byte boundary.
 * Returns zero if there's enough space in "bufsz" for the padding or
 * non-zero if there was (including no padding).
 */
static int
sqlbox_parm_unpack_align(struct sqlbox *box,
	const char **buf, size_t *bufsz, size_t algn)
{
	size_t	 offs;
	if (((uintptr_t)*buf % algn) == 0)
		return 1;

	offs = algn - ((uintptr_t)*buf % algn);
	if (*bufsz < offs)
		return 0;

	*buf += offs;
	*bufsz += offs;
	return 1;
}

/*
 * Unpack a set of sqlbox_parm from the buffer.
 * Returns zero on failure or the number of bytes processed on success.
 * On failure, no [new] memory is allocated into the result pointers.
 */
size_t
sqlbox_parm_unpack(struct sqlbox *box, struct sqlbox_parm **parms,
	size_t *parmsz, const char *buf, size_t bufsz)
{
	size_t	 	 i = 0, len;
	const char	*start = buf;

	*parms = NULL;
	*parmsz = 0;

	/* Start by 8-byte padding. */

	if (!sqlbox_parm_unpack_align(box, &buf, &bufsz, 8))
		goto badframe;

	/* Read prologue: param size. */

	if (bufsz < sizeof(uint32_t))
		goto badframe;
	*parmsz = le32toh(*(const uint32_t *)buf);
	buf += sizeof(uint32_t);
	bufsz -= sizeof(uint32_t);

	if (*parmsz == 0) {
		/* Final padding. */
		if (!sqlbox_parm_unpack_align(box, &buf, &bufsz, 4))
			goto badframe;
		assert(buf > start);
		return (size_t)(buf - start);
	}

	/* Allocate stored parameters (or not, if none). */

	*parms = calloc(*parmsz, sizeof(struct sqlbox_parm));
	if (*parms == NULL) {
		sqlbox_warn(&box->cfg, "calloc");
		return 0;
	}

	/* 
	 * Copy out into our parameters.
	 * Note that we don't copy out strings: we index directly into
	 * the array for that.
	 */

	for (i = 0; i < *parmsz; i++) {
		if (!sqlbox_parm_unpack_align(box, &buf, &bufsz, 4))
			goto badframe;
		if (bufsz < sizeof(uint32_t))
			goto badframe;
		(*parms)[i].type = le32toh(*(uint32_t *)buf);
		buf += sizeof(uint32_t);
		bufsz -= sizeof(uint32_t);
		switch ((*parms)[i].type) {
		case SQLBOX_PARM_FLOAT:
			if (!sqlbox_parm_unpack_align(box, &buf, &bufsz, 8))
				goto badframe;
			if (bufsz < sizeof(double))
				goto badframe;
			(*parms)[i].sz = sizeof(double);
			(*parms)[i].fparm = *(double *)buf;
			buf += sizeof(double);
			bufsz -= sizeof(double);
			break;
		case SQLBOX_PARM_INT:
			if (!sqlbox_parm_unpack_align(box, &buf, &bufsz, 8))
				goto badframe;
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

	/* Read past any 4-byte padding. */

	if (!sqlbox_parm_unpack_align(box, &buf, &bufsz, 4))
		goto badframe;

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

int
sqlbox_parm_int(const struct sqlbox_parm *p, int64_t *v)
{
	long long	 lval;
	char		*ep;

	switch (p->type) {
	case SQLBOX_PARM_INT:
		*v = p->iparm;
		break;
	case SQLBOX_PARM_FLOAT:
		*v = (int64_t)p->fparm;
		break;
	case SQLBOX_PARM_STRING:
		errno = 0;
		lval = strtoll(p->sparm, &ep, 10);
		if (p->sparm[0] == '\0' || *ep != '\0')
			return -1;
		if (errno == ERANGE && 
		    (lval == LLONG_MAX || lval == LLONG_MIN))
			return -1;
		*v = lval;
		break;
	case SQLBOX_PARM_NULL:
	case SQLBOX_PARM_BLOB:
		return -1;
	}

	return p->type == SQLBOX_PARM_INT ? 0 : 1;
}

int
sqlbox_parm_float(const struct sqlbox_parm *p, double *v)
{
	char	*ep;

	switch (p->type) {
	case SQLBOX_PARM_INT:
		*v = (double)p->iparm;
		break;
	case SQLBOX_PARM_FLOAT:
		*v = p->fparm;
		break;
	case SQLBOX_PARM_STRING:
		errno = 0;
		*v = strtod(p->sparm, &ep);
		if (p->sparm[0] == '\0' || *ep != '\0')
			return -1;
		if (errno == ERANGE && 
		    (*v == HUGE_VAL || *v == -HUGE_VAL))
			return -1;
		break;
	case SQLBOX_PARM_NULL:
	case SQLBOX_PARM_BLOB:
		return -1;
	}

	return p->type == SQLBOX_PARM_FLOAT ? 0 : 1;
}

int
sqlbox_parm_string(const struct sqlbox_parm *p, char *v, size_t vsz, size_t *outsz)
{
	int	 c;
	size_t	 tmp;

	if (vsz == 0)
		return -1;
	if (outsz == NULL)
		outsz = &tmp;

	switch (p->type) {
	case SQLBOX_PARM_INT:
		if ((c = snprintf(v, vsz, "%" PRId64, p->iparm)) < 0)
			return -1;
		*outsz = c;
		break;
	case SQLBOX_PARM_FLOAT:
		if ((c = snprintf(v, vsz, "%lf", p->fparm)) < 0)
			return -1;
		*outsz = c;
		break;
	case SQLBOX_PARM_STRING:
		*outsz = strlcpy(v, p->sparm, vsz);
		break;
	case SQLBOX_PARM_NULL:
	case SQLBOX_PARM_BLOB:
		return -1;
	}

	return p->type == SQLBOX_PARM_STRING ? 0 : 1;
}

int
sqlbox_parm_string_alloc(const struct sqlbox_parm *p, char **v, size_t *outsz)
{
	int	 c;
	size_t	 tmp;

	if (outsz == NULL)
		outsz = &tmp;

	switch (p->type) {
	case SQLBOX_PARM_INT:
		if ((c = asprintf(v, "%" PRId64, p->iparm)) < 0)
			return -1;
		*outsz = c;
		break;
	case SQLBOX_PARM_FLOAT:
		if ((c = asprintf(v, "%lf", p->fparm)) < 0)
			return -1;
		*outsz = c;
		break;
	case SQLBOX_PARM_STRING:
		if ((*v = strdup(p->sparm)) == NULL)
			return -1;
		*outsz = strlen(*v);
		break;
	case SQLBOX_PARM_NULL:
	case SQLBOX_PARM_BLOB:
		return -1;
	}

	return p->type == SQLBOX_PARM_STRING ? 0 : 1;
}

int
sqlbox_parm_blob(const struct sqlbox_parm *p, void *v, size_t vsz, size_t *outsz)
{
	size_t	 nsz;

	if (vsz == 0)
		return -1;

	nsz = p->sz > vsz ? vsz : p->sz;

	switch (p->type) {
	case SQLBOX_PARM_INT:
		memcpy(v, &p->iparm, nsz);
		*outsz = nsz;
		break;
	case SQLBOX_PARM_FLOAT:
		memcpy(v, &p->fparm, nsz);
		*outsz = nsz;
		break;
	case SQLBOX_PARM_STRING:
		*outsz = strlcpy(v, p->sparm, vsz) + 1;
		break;
	case SQLBOX_PARM_NULL:
		return -1;
	case SQLBOX_PARM_BLOB:
		memcpy(v, p->bparm, nsz);
		*outsz = nsz;
		break;
	}

	return p->type == SQLBOX_PARM_BLOB ? 0 : 1;
}

int
sqlbox_parm_blob_alloc(const struct sqlbox_parm *p, void **v, size_t *outsz)
{

	switch (p->type) {
	case SQLBOX_PARM_INT:
		if ((*v = malloc(sizeof(int64_t))) == NULL)
			return -1;
		*outsz = sizeof(int64_t);
		memcpy(*v, &p->iparm, *outsz);
		break;
	case SQLBOX_PARM_FLOAT:
		if ((*v = malloc(sizeof(double))) == NULL)
			return -1;
		*outsz = sizeof(double);
		memcpy(*v, &p->fparm, *outsz);
		break;
	case SQLBOX_PARM_STRING:
		if ((*v = strdup(p->sparm)) == NULL)
			return -1;
		*outsz = strlen(*v) + 1;
		break;
	case SQLBOX_PARM_NULL:
		return -1;
	case SQLBOX_PARM_BLOB:
		if (p->sz) {
			if ((*v = malloc(p->sz)) == NULL)
				return -1;
			memcpy(*v, p->bparm, p->sz);
		}
		*outsz = p->sz;
		break;
	}

	return p->type == SQLBOX_PARM_BLOB ? 0 : 1;
}
