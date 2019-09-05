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
sqlbox_bound_pack(struct sqlbox *box, size_t parmsz,
	const struct sqlbox_bound *parms, 
	char **buf, size_t *offs, size_t *bufsz)
{
	size_t	 framesz, i;
	void	*pp;
	uint32_t tmp;
	int64_t	 val;

	/* Start with types and payload size. */

	framesz = parmsz * sizeof(uint32_t);

	for (i = 0; i < parmsz; i++)
		switch (parms[i].type) {
		case SQLBOX_BOUND_INT: /* value */
			framesz += sizeof(int64_t); /* int */
			break;
		case SQLBOX_BOUND_NULL: /* nothing */
			break;
		case SQLBOX_BOUND_STRING: /* length+data+NUL */
			framesz += sizeof(uint32_t);
			framesz += strlen(parms[i].sparm) + 1;
			break;
		default:
			return 0;
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

	tmp = htonl(parmsz);
	memcpy(*buf + *offs, (char *)&tmp, sizeof(uint32_t));
	*offs += sizeof(uint32_t);

	/* Now all parameter data. */

	for (i = 0; i < parmsz; i++) {
		tmp = htonl(parms[i].type);
		memcpy(*buf + *offs, (char *)&tmp, sizeof(uint32_t));
		*offs += sizeof(uint32_t);

		switch (parms[i].type) {
		case SQLBOX_BOUND_INT:
			val = htobe64(parms[i].iparm);
			memcpy(*buf + *offs, 
				(char *)&val, sizeof(int64_t));
			*offs += sizeof(int64_t);
			break;
		case SQLBOX_BOUND_NULL:
			break;
		case SQLBOX_BOUND_STRING:
			/*
			 * Note that we include the NUL terminator in
			 * the encoded data to make it easier for
			 * unpacking without needing to terminate.
			 */
			tmp = strlen(parms[i].sparm);
			val = htonl(tmp);
			memcpy(*buf + *offs, 
				(char *)&val, sizeof(uint32_t));
			*offs += sizeof(uint32_t);
			memcpy(*buf + *offs, parms[i].sparm, tmp + 1);
			*offs += tmp + 1;
			break;
		default:
			abort();
		}
	}

	return 1;
}

/*
 * Unpack a set of sqlbox_bound from the buffer.
 * Returns TRUE on success, FALSE on failure.
 * On failure, no memory is allocated into the result pointers.
 */
int
sqlbox_bound_unpack(struct sqlbox *box, size_t *parmsz,
	struct sqlbox_bound **parms, const char *buf, size_t bufsz)
{
	size_t	 i, len;

	if (bufsz < sizeof(uint32_t))
		goto badframe;

	/* Read prologue: param size. */

	*parmsz = ntohl(*(const uint32_t *)buf);
	buf += sizeof(uint32_t);
	bufsz -= sizeof(uint32_t);

	/* Allocate stored parameters. */

	*parms = calloc(*parmsz, sizeof(struct sqlbox_bound));
	if (*parms == NULL) {
		sqlbox_warn(&box->cfg, "calloc");
		*parmsz = 0;
		return 0;
	}

	/* 
	 * Copy out into our parameters.
	 * Note that we don't copy out strings: we index directly into
	 * the array for that.
	 */

	for (i = 0; i < *parmsz; i++) {
		if (bufsz < sizeof(uint32_t))
			goto badframe;

		(*parms)[i].type = ntohl(*(uint32_t *)buf);
		buf += sizeof(uint32_t);
		bufsz -= sizeof(uint32_t);

		switch ((*parms)[i].type) {
		case SQLBOX_BOUND_INT:
			if (bufsz < sizeof(int64_t))
				goto badframe;
			(*parms)[i].iparm = be64toh(*(int64_t *)buf);
			buf += sizeof(int64_t);
			bufsz -= sizeof(int64_t);
			break;
		case SQLBOX_BOUND_NULL:
			break;
		case SQLBOX_BOUND_STRING:
			/*
			 * Remember that we encode the string with the
			 * NUL terminator that's not in the length part.
			 */
			if (bufsz < sizeof(uint32_t))
				goto badframe;
			len = ntohl(*(uint32_t *)buf) + 1;
			buf += sizeof(uint32_t);
			bufsz -= sizeof(uint32_t);
			if (bufsz < len)
				goto badframe;
			(*parms)[i].sparm = buf;
			buf += len;
			bufsz -= len;
			break;
		default:
			sqlbox_warnx(&box->cfg, "bad bound type: %d",
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
