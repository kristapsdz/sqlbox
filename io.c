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
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

/*
 * This is called by both the client and the server, so it can't contain
 * any specifities.
 * Simply performs a blocking write of the sized buffer, which must not
 * be zero-length.
 * Returns FALSE on failure, TRUE on success.
 */
int
sqlbox_write(struct sqlbox *box, const char *buf, size_t sz)
{
	struct pollfd	 pfd = { .fd = box->fd, .events = POLLOUT };
	ssize_t		 wsz;
	size_t		 tsz = 0;

	assert(sz > 0);

	sqlbox_debug(&box->cfg, "write: %zu", sz);

	for (;;) {
		if (poll(&pfd, 1, INFTIM) == -1) {
			sqlbox_warn(&box->cfg, "ppoll (write)");
			return 0;
		} else if ((pfd.revents & (POLLNVAL|POLLERR)))  {
			sqlbox_warnx(&box->cfg, 
				"ppoll (write): nval");
			return 0;
		} else if ((pfd.revents & POLLHUP)) {
			sqlbox_warnx(&box->cfg, 
				"ppoll (write): hangup");
			return 0;
		} else if (!(POLLOUT & pfd.revents)) {
			sqlbox_warnx(&box->cfg, 
				"ppoll (write): bad revent");
			return 0;
		}
		if ((wsz = write(pfd.fd, buf + tsz, sz - tsz)) == -1) {
			sqlbox_warn(&box->cfg, "write");
			return 0;
		} else if ((tsz += wsz) == sz)
			break;
	}

	return 1;
}

/*
 * Called by the client only, so it doesn't respond to end of file in
 * any but erroring out.
 * Performs a non-blocking read of the sized buffer, which must not be
 * zero-length.
 * Returns FALSE on failure, TRUE on success.
 */
int
sqlbox_read(struct sqlbox *box, char *buf, size_t sz)
{
	struct pollfd	 pfd = { .fd = box->fd, .events = POLLIN };
	ssize_t		 rsz;
	size_t		 tsz = 0;

	assert(sz > 0);

	sqlbox_debug(&box->cfg, "read: %zu", sz);

	for (;;) {
		if (poll(&pfd, 1, INFTIM) == -1) {
			sqlbox_warn(&box->cfg, "ppoll (read)");
			return 0;
		} else if ((pfd.revents & (POLLNVAL|POLLERR)))  {
			sqlbox_warnx(&box->cfg, 
				"ppoll (read): nval");
			return 0;
		} else if ((pfd.revents & POLLHUP)) {
			sqlbox_warnx(&box->cfg, 
				"ppoll (read): hangup");
			return 0;
		} else if (!(POLLIN & pfd.revents)) {
			sqlbox_warnx(&box->cfg, 
				"ppoll (read): bad revent");
			return 0;
		}

		if ((rsz = read(pfd.fd, buf + tsz, sz - tsz)) == -1) {
			sqlbox_warn(&box->cfg, "read");
			return 0;
		} else if ((tsz += rsz) == sz)
			break;
	}

	return 1;
}

/*
 * Read a single frame, which is of size at least the baseline frame.
 * The frame is set in "frame" and is of length "framesz", both of which
 * are initialised to NULL and 0, respectively.
 * Return <0 on failure, 0 on EOF without data, >0 on success.
 */
int
sqlbox_read_frame(struct sqlbox *box, char **buf, 
	size_t *bufsz, const char **frame, size_t *framesz)
{
	struct pollfd	 pfd = { .fd = box->fd, .events = POLLIN };
	ssize_t		 rsz;
	size_t		 sz = 0, bsz;
	void		*pp;

	*frame = NULL;
	*framesz = 0;

	/* 
	 * We want to read at least a frame size of data.
	 * Frame sizes are SQLBOX_FRAME bytes.
	 */
	
	bsz = SQLBOX_FRAME;
	if (*bufsz == 0) {
		assert(*buf == NULL);
		if ((*buf = malloc(bsz)) == NULL) {
			sqlbox_warn(&box->cfg, "malloc");
			return -1;
		}
		*bufsz = bsz;
	}
	assert(*bufsz >= bsz);

	/*
	 * Start by reading the frame basis, which is always of size
	 * SQLBOX_FRAME bytes.
	 * This will also contain the real size of the frame, which, if
	 * greater than 1020 bytes, will involve the reading of
	 * subsequent frames.
	 */

	sqlbox_debug(&box->cfg, "read(frame): %zu", bsz);

	while (sz < bsz) {
		if (poll(&pfd, 1, INFTIM) == -1) {
			sqlbox_warn(&box->cfg, "ppoll");
			return -1;
		} else if ((pfd.revents & (POLLNVAL|POLLERR)))  {
			sqlbox_warnx(&box->cfg, "ppoll: nval");
			return -1;
		} else if ((pfd.revents & POLLHUP) && 
		           !(pfd.revents & POLLIN)) {
			sqlbox_warnx(&box->cfg, "ppoll: hup");
			break;
		} else if (!(POLLIN & pfd.revents)) {
			sqlbox_warnx(&box->cfg, "ppoll: bad event");
			return -1;
		}

		rsz = read(pfd.fd, *buf + sz, bsz - sz);
		if (rsz == -1) {
			sqlbox_warn(&box->cfg, "read");
			return -1;
		} else if (rsz == 0 && sz == 0) {
			return 0;
		}

		/* 
		 * Make sure we've read the entire initial frame.
		 * The whole point is that it should come in one packet,
		 * so warn here if it doesn't.
		 */

		if ((sz += rsz) < bsz)
			sqlbox_warnx(&box->cfg, "read: frame basis "
				"fragmented (%zd B < %zu B)", rsz, bsz);
	}
	if (sz < bsz) {
		sqlbox_warnx(&box->cfg, "read: eof with "
			"unfinished frame(%zu B < %zu B)", sz, bsz);
		return -1;
	}

	/* 
	 * Reallocate the extended buffer, if necessary.
	 * Remember that the frame size does NOT include the size of the
	 * frame size integer.
	 */

	*framesz = le32toh(*(uint32_t *)*buf);
	bsz = *framesz + sizeof(uint32_t);

	sqlbox_debug(&box->cfg, "read(frame): framesz: %zu", *framesz);

	if (bsz > *bufsz) {
		if ((pp = realloc(*buf, bsz)) == NULL) {
			sqlbox_warn(&box->cfg, "realloc");
			return -1;
		}
		*buf = pp;
		*bufsz = bsz;
	}
	*frame = *buf + sizeof(uint32_t);

	/* Everything was in the first frame. */

	if (bsz <= SQLBOX_FRAME)
		return 1;

	/* Now read the rest of the frame. */

	sqlbox_debug(&box->cfg, "read(frame, continuance): %zu", bsz);

	while (sz < bsz) {
		if (poll(&pfd, 1, INFTIM) == -1) {
			sqlbox_warn(&box->cfg, "ppoll");
			return -1;
		} else if ((pfd.revents & (POLLNVAL|POLLERR)))  {
			sqlbox_warnx(&box->cfg, "ppoll: nval");
			return -1;
		} else if ((pfd.revents & POLLHUP) && 
		           !(pfd.revents & POLLIN)) {
			sqlbox_warnx(&box->cfg, "ppoll: hup");
			break;
		} else if (!(POLLIN & pfd.revents)) {
			sqlbox_warnx(&box->cfg, "ppoll: bad event");
			return -1;
		}

		rsz = read(pfd.fd, *buf + sz, bsz - sz);
		if (rsz == -1) {
			sqlbox_warn(&box->cfg, "read");
			return -1;
		} else if (rsz == 0) {
			sqlbox_warnx(&box->cfg, "read: eof");
			return -1;
		}
		sz += rsz;
	}

	return 1;
}

/*
 * Write a buffer "buf" of length "sz" into a frame of type "op".
 * FIXME: for the time being, "sz" must fit in SQLBOX_FRAME - 8 bytes
 * (the latter for 2 32-bit integers).
 * Return TRUE on success, FALSE on failure.
 */
int
sqlbox_write_frame(struct sqlbox *box,
	enum sqlbox_op op, const char *buf, size_t sz)
{
	char		 frame[SQLBOX_FRAME];
	uint32_t	 tmp;

	memset(frame, 0, sizeof(frame));

	/* Account for operation... */

	tmp = htole32(sz + sizeof(uint32_t));
	memcpy(frame, &tmp, sizeof(uint32_t));

	tmp = htole32(op);
	memcpy(frame + sizeof(uint32_t), &tmp, sizeof(uint32_t));

	assert(sz <= SQLBOX_FRAME - sizeof(uint32_t) * 2);
	memcpy(frame + sizeof(uint32_t) * 2, buf, sz);

	return sqlbox_write(box, frame, sizeof(frame));
}

