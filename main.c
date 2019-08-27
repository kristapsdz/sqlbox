
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
#include <sys/socket.h>

#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#if !HAVE_SOCK_NONBLOCK
# include <fcntl.h>
#endif
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
 * This is a way for us to sleep between connection attempts.
 * To reduce lock contention, our sleep will be random.
 */
static void
sqlbox_sleep(size_t attempt)
{
	useconds_t	us;

	us = attempt > 100 ? 10000 :  /* 1/100 second */
	     attempt > 10  ? 100000 : /* 1/10 second */
	     250000;                  /* 1/4 second */

	usleep(us * (double)(random() / (double)RAND_MAX));
}

/*
 * This is called by both the client and the server, so it can't contain
 * any specifities.
 * Simply performs a blocking write of the sized buffer, which must not
 * be zero-length.
 * Returns FALSE on failure, TRUE on success.
 */
static int
sqlbox_write(struct sqlbox *box, const char *buf, size_t sz)
{
	struct pollfd	 pfd = { .fd = box->fd, .events = POLLOUT };
	ssize_t		 wsz;
	size_t		 tsz = 0;

	assert(sz > 0);

	for (;;) {
		if (poll(&pfd, 1, INFTIM) == -1) {
			sqlbox_warn(&box->cfg, "ppoll");
			return 0;
		} else if ((pfd.revents & (POLLNVAL|POLLERR)))  {
			sqlbox_warnx(&box->cfg, "ppoll: nval (here)");
			return 0;
		} else if ((pfd.revents & POLLHUP)) {
			sqlbox_warnx(&box->cfg, "ppoll: hangup");
			return 0;
		} else if (!(POLLOUT & pfd.revents)) {
			sqlbox_warnx(&box->cfg, "ppoll: bad revent");
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
static int
sqlbox_read(struct sqlbox *box, char *buf, size_t sz)
{
	struct pollfd	 pfd = { .fd = box->fd, .events = POLLIN };
	ssize_t		 rsz;
	size_t		 tsz = 0;

	assert(sz > 0);

	for (;;) {
		if (poll(&pfd, 1, INFTIM) == -1) {
			sqlbox_warn(&box->cfg, "ppoll");
			return 0;
		} else if ((pfd.revents & (POLLNVAL|POLLERR)))  {
			sqlbox_warnx(&box->cfg, "ppoll: nval (no here)");
			return 0;
		} else if ((pfd.revents & POLLHUP)) {
			sqlbox_warnx(&box->cfg, "ppoll: hangup");
			break;
		} else if (!(POLLIN & pfd.revents)) {
			sqlbox_warnx(&box->cfg, "ppoll: bad revent");
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

static int
sqlbox_write_frame(struct sqlbox *box,
	enum sqlbox_op op, const char *buf, size_t sz)
{
	char		 frame[BUFSIZ];
	uint32_t	 tmp;

	tmp = htonl(op);
	memcpy(frame, &tmp, sizeof(uint32_t));
	tmp = htonl(sz);
	memcpy(frame + 4, &tmp, sizeof(uint32_t));

	assert(sz <= BUFSIZ - 8);
	memcpy(frame + 8, buf, sz);

	return sqlbox_write(box, frame, sizeof(frame));
}

int
sqlbox_ping(struct sqlbox *box)
{
	uint32_t	 syn, ack;

	syn = arc4random();
	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_PING, (char *)&syn, sizeof(uint32_t)))
		return 0;
	if (!sqlbox_read(box, (char *)&ack, sizeof(uint32_t)))
		return 0;
	return ack == syn;
}

/*
 * Read a 4-byte acknowledgement and write it back.
 * This just ping to see if we're alive.
 * Returns FALSE on failure, TRUE on success.
 */
static int
sqlbox_op_ping(struct sqlbox *box, const char *buf, size_t sz)
{

	if (sz == sizeof(uint32_t))
		return sqlbox_write(box, buf, sz);

	sqlbox_warnx(&box->cfg, "%s: "
		"bad frame size: %zu", __func__, sz);
	return 0;
}

int
sqlbox_open(struct sqlbox *box, size_t src)
{
	uint32_t	 v = htonl(src);

	return sqlbox_write_frame(box, 
		SQLBOX_OP_OPEN, (char *)&v, sizeof(uint32_t));
}

/*
 * Attempt to open a database.
 * First check if the index is valid, then whether our role permits
 * opening new databases.
 * Finally, 
 */
static int
sqlbox_op_open(struct sqlbox *box, const char *buf, size_t sz)
{
	size_t		  i, idx, attempt = 0;
	int		  fl;
	const char	 *fn;
	struct sqlbox_db *db;

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "%s: "
			"bad frame size: %zu", __func__, sz);
		return 0;
	}

	/* Check source exists. */

	idx = ntohl(*(uint32_t *)buf);
	if (idx >= box->cfg.srcs.srcsz) {
		sqlbox_warnx(&box->cfg, "%s: invalid source "
			"index %zu (have %zu)", __func__, idx, 
			box->cfg.srcs.srcsz);
		return 0;
	}

	/* Check open permissions for role. */

	if (box->cfg.roles.rolesz) {
		for (i = 0; i < box->cfg.roles.roles[box->role].srcsz; i++)
			if (box->cfg.roles.roles[box->role].srcs[i] == idx)
				break;
		if (i == box->cfg.roles.roles[box->role].srcsz) {
			sqlbox_warnx(&box->cfg, "%s: source index %zu "
				"denied to current role %zu", __func__, 
				idx, box->role);
			return 0;
		}
	}

	if ((db = calloc(1, sizeof(struct sqlbox_db))) == NULL) {
		sqlbox_warn(&box->cfg, "calloc");
		return 0;
	}
	TAILQ_INIT(&db->stmtq);
	db->idx = idx;

	fn = box->cfg.srcs.srcs[idx].fname;
	if (box->cfg.srcs.srcs[idx].mode == SQLBOX_SRC_RO)
		fl = SQLITE_OPEN_READONLY;
	else if (box->cfg.srcs.srcs[idx].mode == SQLBOX_SRC_RW)
		fl = SQLITE_OPEN_READWRITE;
	else 
		fl = SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE;
again:
	switch (sqlite3_open_v2(fn, &db->db, fl, NULL)) {
	case SQLITE_BUSY:
	case SQLITE_LOCKED:
	case SQLITE_PROTOCOL:
		sqlite3_close(db->db);
		db->db = NULL;
		sqlbox_sleep(attempt++);
		goto again;
	case SQLITE_OK:
		break;
	default:
		if (db->db != NULL) {
			sqlbox_warnx(&box->cfg, "%s: %s", 
				sqlite3_errmsg(db->db), fn);
			sqlite3_close(db->db);
		} else
			sqlbox_warnx(&box->cfg, 
				"sqlite3_open_v2: %s", fn);
		free(db);
		return 0;
	}

	return 1;
}

int
sqlbox_role(struct sqlbox *box, size_t role)
{
	uint32_t	 v = htonl(role);

	return sqlbox_write_frame(box, 
		SQLBOX_OP_ROLE, (char *)&v, sizeof(uint32_t));
}

static int
sqlbox_op_role(struct sqlbox *box, const char *buf, size_t sz)
{
	size_t		i, role;

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "%s: "
			"bad frame size: %zu", __func__, sz);
		return 0;
	} else if (box->cfg.roles.rolesz == 0) {
		sqlbox_warnx(&box->cfg, "%s: no roles", __func__);
		return 0;
	}

	role = ntohl(*(uint32_t *)buf);
	
	if (role > box->cfg.roles.rolesz) {
		sqlbox_warnx(&box->cfg, "%s: invalid role "
			"%zu (have %zu)", __func__, role, 
			box->cfg.roles.rolesz);
		return 0;
	}

	/* Transition into current role is a noop. */

	if (role == box->role) {
		sqlbox_warnx(&box->cfg, "%s: transition "
			"into current role %zu (harmless)", 
			__func__, role);
		return 1;
	}

	/* Look up whether the role transition exists. */

	for (i = 0; i < box->cfg.roles.roles[box->role].rolesz; i++)
		if (box->cfg.roles.roles[box->role].roles[i] == role)
			break;

	if (i == box->cfg.roles.roles[box->role].rolesz) {
		sqlbox_warnx(&box->cfg, "%s: role transition "
			"from %zu into %zu not allowed", __func__, 
			box->role, role);
		return 0;
	}

	box->role = role;
	return 1;
}

int
sqlbox_main_loop(struct sqlbox *p)
{
	struct pollfd	 pfd;
	ssize_t		 rsz;
	size_t		 sz;
	enum sqlbox_op	 op;
	char		 buf[BUFSIZ];

	/* Handle all signals we can. */

	pfd.fd = p->fd;
	pfd.events = POLLIN;
	sz = 0;

	for (;;) {
		if (poll(&pfd, 1, INFTIM) == -1) {
			sqlbox_warn(&p->cfg, "ppoll");
			return 0;
		}

		/*
		 * Error out if we have bad descriptor values, if we
		 * received a HUP (without getting an EOF marker), or we
		 * otherwise don't have a read event.
		 */
		
		if ((pfd.revents & (POLLNVAL|POLLERR)))  {
			sqlbox_warnx(&p->cfg, "ppoll: nval (last here)");
			return 0;
		} else if ((pfd.revents & POLLHUP) && 
		           !(pfd.revents & POLLIN)) {
			sqlbox_warnx(&p->cfg, "ppoll: hangup");
			break;
		} else if (!(POLLIN & pfd.revents)) {
			sqlbox_warnx(&p->cfg, "ppoll: unknown revent");
			return 0;
		}

		/* 
		 * Read into the initial buffer.
		 * Warn if the buffer isn't bad enough, as we should
		 * always be able to read directly into the buffer.
		 */

		if ((rsz = read(pfd.fd, buf + sz, BUFSIZ - sz)) == -1) {
			sqlbox_warn(&p->cfg, "read");
			return 0;
		} else if (rsz == 0)
			break;

		/* Read entire frame. */

		if ((sz += rsz) < BUFSIZ) {
			sqlbox_warnx(&p->cfg, "read: frame "
				"size too large: fragmented frame");
			continue;
		}

		op = (enum sqlbox_op)ntohl(*(uint32_t *)buf);
		sz = ntohl(*(uint32_t *)(buf + 4));

		/* 
		 * TODO: handle longer messages, which is possible
		 * because we need to handle arbitrary strings at times,
		 * e.g., when opening new databases.
		 */

		assert(sz <= BUFSIZ - 8);

		switch (op) {
		case SQLBOX_OP_OPEN:
			if (sqlbox_op_open(p, buf + 8, sz))
				break;
			sqlbox_warnx(&p->cfg, "sqlbox_op_open");
			return 0;
		case SQLBOX_OP_PING:
			if (sqlbox_op_ping(p, buf + 8, sz))
				break;
			sqlbox_warnx(&p->cfg, "sqlbox_op_ping");
			return 0;
		case SQLBOX_OP_ROLE:
			if (sqlbox_op_role(p, buf + 8, sz))
				break;
			sqlbox_warnx(&p->cfg, "sqlbox_op_role");
			return 0;
		default:
			sqlbox_warnx(&p->cfg, "unknown op: %d", op);
			return 0;
		}

		sz = 0;
	}

	return 1;
}

