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

	framesz = parmsz * sizeof(uint32_t);

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
		case SQLBOX_PARM_STRING:
			framesz += sizeof(uint32_t);
			framesz += parms[i].sz;
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
 * Unpack a set of sqlbox_parm from the buffer.
 * Returns TRUE on success, FALSE on failure.
 * On failure, no [new] memory is allocated into the result pointers.
 */
int
sqlbox_parm_unpack(struct sqlbox *box, struct sqlbox_parm **parms, 
	ssize_t *parmsz, const char *buf, size_t bufsz)
{
	size_t	 i, len, psz;

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
			buf += sizeof(uint32_t);
			bufsz -= sizeof(uint32_t);
			if (bufsz < len)
				goto badframe;
			(*parms)[i].sparm = buf;
			(*parms)[i].sz = len;
			buf += len;
			bufsz -= len;
			break;
		default:
			sqlbox_warnx(&box->cfg, "bad parm type: %d",
				(*parms)[i].type);
			free(*parms);
			*parms = NULL;
			*parmsz = 0;
			return 0;
		}
	}

	return 1;
badframe:
	sqlbox_warnx(&box->cfg, "unpacking "
		"parameters: invalid frame size");
	free(*parms);
	*parms = NULL;
	*parmsz = 0;
	return 0;
}
