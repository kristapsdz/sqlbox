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

static struct sqlbox_db *
sqlbox_db_find(struct sqlbox *box, size_t id)
{
	struct sqlbox_db	*db;

	TAILQ_FOREACH(db, &box->dbq, entries)
		if (db->id == id)
			return db;

	sqlbox_warnx(&box->cfg, "cannot find source: %zu", id);
	return NULL;
}

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
	sqlbox_warnx(&box->cfg, "statement %zu denied to role %zu",
		idx, box->role);
	return 0;
}

/*
 * Return TRUE if the current role has the ability to transition to the
 * given role (or it's the same role), FALSE if otherwise.
 */
static int
sqlbox_rolecheck_role(struct sqlbox *box, size_t idx)
{
	size_t	 i;

	assert(box->cfg.roles.rolesz);
	if (box->role == idx) {
		sqlbox_warnx(&box->cfg, "changing into "
			"current role %zu (harmless)", idx);
		return 1;
	}
	for (i = 0; i < box->cfg.roles.roles[box->role].rolesz; i++)
		if (box->cfg.roles.roles[box->role].roles[i] == idx)
			return 1;
	sqlbox_warnx(&box->cfg, "role %zu denied to role %zu",
		idx, box->role);
	return 0;
}

/*
 * Return TRUE if the current role has the ability to open or close the
 * given close (or no roles are specified), FALSE if otherwise.
 */
static int
sqlbox_rolecheck_src(struct sqlbox *box, size_t idx)
{
	size_t	 i;

	if (box->cfg.roles.rolesz == 0)
		return 1;
	for (i = 0; i < box->cfg.roles.roles[box->role].srcsz; i++)
		if (box->cfg.roles.roles[box->role].srcs[i] == idx)
			return 1;
	sqlbox_warnx(&box->cfg, "source %zu denied to role %zu", 
		idx, box->role);
	return 0;
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
static int
sqlbox_read(struct sqlbox *box, char *buf, size_t sz)
{
	struct pollfd	 pfd = { .fd = box->fd, .events = POLLIN };
	ssize_t		 rsz;
	size_t		 tsz = 0;

	assert(sz > 0);

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
			break;
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
 * Write a buffer "buf" of length "sz" into a frame of type "op".
 * FIXME: for the time being, "sz" must fit in BUFSIZ less the
 * accounting at the beginning.
 * Return TRUE on success, FALSE on failure.
 */
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
	if (!sqlbox_write_frame(box, 
	    SQLBOX_OP_PING, (char *)&syn, sizeof(uint32_t)))
		return 0;
	if (!sqlbox_read(box, (char *)&ack, sizeof(uint32_t)))
		return 0;
	return ack == syn;
}

/*
 * Read an acknowledgement and write it back.
 * This just ping to see if we're alive.
 * Returns FALSE on failure, TRUE on success.
 */
static int
sqlbox_op_ping(struct sqlbox *box, const char *buf, size_t sz)
{

	if (sz == sizeof(uint32_t))
		return sqlbox_write(box, buf, sz);
	sqlbox_warnx(&box->cfg, "cannot "
		"ping: bad frame size: %zu", sz);
	return 0;
}

int
sqlbox_close(struct sqlbox *box, size_t src)
{
	uint32_t	 v = htonl(src);

	return sqlbox_write_frame(box,
		SQLBOX_OP_CLOSE, (char *)&v, sizeof(uint32_t));
}

/*
 * Close a database.
 * First check if the identifier is valid, then whether our role permits
 * closing databases.
 * Returns TRUE on success or FALSE on failure (depending on failure,
 * source may be removed).
 */
static int
sqlbox_op_close(struct sqlbox *box, const char *buf, size_t sz)
{
	struct sqlbox_db *db;
	int		  rc = 0;

	/* Check source exists and we can close it. */

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "cannot "
			"close: bad frame size: %zu", sz);
		return 0;
	}
	if ((db = sqlbox_db_find
	    (box, ntohl(*(uint32_t *)buf))) == NULL) {
		sqlbox_warnx(&box->cfg, 
			"cannot close: sqlbox_db_find");
		return 0;
	}
	if (!sqlbox_rolecheck_src(box, db->idx)) {
		sqlbox_warnx(&box->cfg, "%s: cannot close: "
			"sqlbox_rolecheck_src", db->src->fname);
		return 0;
	}

	/* 
	 * Make sure we're ready to close.
	 * For the time being, we simply check if we have any open
	 * statements.
	 * If we do, exit: our main exit handler will finalise these
	 * statements for us.
	 */

	if (!TAILQ_EMPTY(&db->stmtq)) {
		sqlbox_warnx(&box->cfg, "%s: cannot close: "
			"source %zu (id %zu) has unfinalised "
			"statements", db->src->fname, db->idx, 
			db->id);
		return 0;
	}

	/* 
	 * Remove from queue so we don't double close, but let the
	 * underlying close have us error out if it fails.
	 */

	TAILQ_REMOVE(&box->dbq, db, entries);
	if (sqlite3_close(db->db) != SQLITE_OK)
		sqlbox_warnx(&box->cfg, "%s: cannot close: %s", 
			db->src->fname, sqlite3_errmsg(db->db));
	else
		rc = 1;

	free(db);
	return rc;
}

size_t
sqlbox_open(struct sqlbox *box, size_t src)
{
	uint32_t	 v = htonl(src), ack;

	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_OPEN, (char *)&v, sizeof(uint32_t)))
		return 0;
	if (!sqlbox_read(box, (char *)&ack, sizeof(uint32_t)))
		return 0;
	return (size_t)ntohl(ack);
}

/*
 * Attempt to open a database.
 * First check if the index is valid, then whether our role permits
 * opening new databases.
 * Then do the open itself.
 * On success, writes back the unique identifier of the database.
 * Returns TRUE on success, FALSE on failure (nothing is allocated).
 */
static int
sqlbox_op_open(struct sqlbox *box, const char *buf, size_t sz)
{
	size_t		  idx, attempt = 0;
	int		  fl;
	const char	 *fn;
	struct sqlbox_db *db;
	uint32_t	  ack;

	/* Check source exists and we have permission for it. */

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "cannot "
			"open: bad frame size: %zu", sz);
		return 0;
	}
	idx = ntohl(*(uint32_t *)buf);
	if (idx >= box->cfg.srcs.srcsz) {
		sqlbox_warnx(&box->cfg, "cannot open: invalid source "
			"%zu (have %zu)", idx, box->cfg.srcs.srcsz);
		return 0;
	}
	assert(idx < box->cfg.srcs.srcsz);
	fn = box->cfg.srcs.srcs[idx].fname;
	if (!sqlbox_rolecheck_src(box, idx)) {
		sqlbox_warnx(&box->cfg, "%s: cannot open: "
			"sqlbox_rolecheck_src", fn);
		return 0;
	}

	/* Allocate and prepare for open. */

	if ((db = calloc(1, sizeof(struct sqlbox_db))) == NULL) {
		sqlbox_warn(&box->cfg, "calloc");
		return 0;
	}
	TAILQ_INIT(&db->stmtq);
	db->id = ++box->lastid;
	db->src = &box->cfg.srcs.srcs[idx];
	db->idx = idx;

	if (db->src->mode == SQLBOX_SRC_RO)
		fl = SQLITE_OPEN_READONLY;
	else if (db->src->mode == SQLBOX_SRC_RW)
		fl = SQLITE_OPEN_READWRITE;
	else 
		fl = SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE;
	
	/*
	 * We can legit be asked to wait for a while for opening
	 * especially if the source is stressed.
	 * Use our usual backing-off algorithm but keep trying ad
	 * infinitum.
	 * If we error out, be sure to free all resources.
	 */
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
			sqlbox_warnx(&box->cfg, "%s: cannot open: "
				"%s", fn, sqlite3_errmsg(db->db));
			sqlite3_close(db->db);
		} else
			sqlbox_warnx(&box->cfg, 
				"%s: sqlite3_open_v2", fn);
		free(db);
		return 0;
	}

	/* Add to list of available sources and write back id. */

	TAILQ_INSERT_TAIL(&box->dbq, db, entries);
	ack = htonl(db->id);
	return sqlbox_write(box, (char *)&ack, sizeof(uint32_t));
}

size_t
sqlbox_prepare_bind(struct sqlbox *box, size_t srcid,
	size_t pstmt, size_t parmsz, const struct sqlbox_bound *parms)
{
	char	*frame;
	size_t	 framesz = 0, i, pos = 0, tmp;
	uint32_t val;

	framesz = parmsz * sizeof(uint32_t) + /* types */
		3 * sizeof(uint32_t); /* prologue */

	/* Compute size required for each parameter. */

	for (i = 0; i < parmsz; i++)
		switch (parms[i].type) {
		case SQLBOX_BOUND_INT: /* value */
			framesz += sizeof(uint32_t); /* int */
			break;
		case SQLBOX_BOUND_NULL: /* nothing */
			break;
		case SQLBOX_BOUND_STRING: /* length+data */
			framesz += sizeof(uint32_t);
			framesz += strlen(parms[i].sparm);
			break;
		default:
			return 0;
		}

	/* 
	 * Allocate our write buffer.
	 * FIXME: it's not necessary to redo this every time: we might
	 * just want to keep one buffer and reuse it all the time.
	 */

	if ((frame = malloc(framesz)) == NULL)
		return 0;

	/* Prologue: source, statement, param size. */

	val = htonl(srcid);
	memcpy(frame + 0, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	val = htonl(pstmt);
	memcpy(frame + 4, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	val = htonl(parmsz);
	memcpy(frame + 8, (char *)&val, sizeof(uint32_t));
	pos += sizeof(uint32_t);

	/* Now all parameter data. */

	for (i = 0; i < parmsz; i++) {
		val = htonl(parms[i].type);
		memcpy(frame + pos, (char *)&val, sizeof(uint32_t));
		pos += sizeof(uint32_t);
		switch (parms[i].type) {
		case SQLBOX_BOUND_INT:
			val = htonl(parms[i].iparm);
			memcpy(frame + pos, 
				(char *)&val, sizeof(uint32_t));
			pos += sizeof(uint32_t);
			break;
		case SQLBOX_BOUND_NULL:
			break;
		case SQLBOX_BOUND_STRING:
			tmp = strlen(parms[i].sparm);
			val = htonl(tmp);
			memcpy(frame + pos, 
				(char *)&val, sizeof(uint32_t));
			pos += sizeof(uint32_t);
			memcpy(frame + pos, parms[i].sparm, tmp);
			pos += tmp;
			break;
		default:
			abort();
		}
	}

	/* Write data, free our buffer, read the statement id. */

	if (!sqlbox_write_frame(box, 
	    SQLBOX_OP_PREPARE_BIND, frame, framesz)) {
		free(frame);
		return 0;
	}
	free(frame);

	if (!sqlbox_read(box, (char *)&val, sizeof(uint32_t)))
		return 0;
	return (size_t)ntohl(val);
}

/*
 * Prepare and bind parameters to a statement in one step.
 * Return TRUE on success, FALSE on failure (nothing is allocated).
 */
static int
sqlbox_op_prepare_bind(struct sqlbox *box, const char *buf, size_t sz)
{
	size_t	 		 i, idx, attempt = 0, parmsz;
	enum sqlbox_boundt	 type;
	struct sqlbox_db	*db;
	sqlite3_stmt		*stmt = NULL;
	struct sqlbox_stmt	*st;
	struct sqlbox_pstmt	*pst;
	int			 c;
	uint32_t		 ack, val;

	/* Read the source identifier. */

	if (sz < sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "cannot prepare-"
			"bind: bad frame size: %zu", sz);
		return 0;
	}
	if ((db = sqlbox_db_find
	    (box, ntohl(*(uint32_t *)buf))) == NULL) {
		sqlbox_warnx(&box->cfg, 
			"cannot close: sqlbox_db_find");
		return 0;
	}
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	/* Read and validate the statement identifier. */

	if (sz < sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "cannot prepare-"
			"bind: bad frame size: %zu", sz);
		return 0;
	}
	idx = ntohl(*(uint32_t *)buf);
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	if (idx >= box->cfg.stmts.stmtsz) {
		sqlbox_warnx(&box->cfg, "%s: cannot prepare-bind: "
			"bad statement %zu", db->src->fname, idx);
		return 0;
	} else if (!sqlbox_rolecheck_stmt(box, idx)) {
		sqlbox_warnx(&box->cfg, "%s: cannot prepare-bind: "
			"sqlbox_rolecheck_stmt", db->src->fname);
		return 0;
	}
	pst = &box->cfg.stmts.stmts[idx];

	/* Now the number of parameters. */

	if (sz < sizeof(uint32_t))
		goto badframe;
	parmsz = ntohl(*(uint32_t *)buf);
	buf += sizeof(uint32_t);
	sz -= sizeof(uint32_t);

	/* 
	 * Actually prepare the statement first.
	 * In the usual way we sleep if SQLite gives us a busy, locked,
	 * or weird protocol error.
	 * All other errors are real errorrs.
	 */
again:
	stmt = NULL;
	c = sqlite3_prepare_v2(db->db, pst->stmt, -1, &stmt, NULL);

	switch (c) {
	case SQLITE_BUSY:
	case SQLITE_LOCKED:
	case SQLITE_PROTOCOL:
		sqlbox_sleep(attempt++);
		goto again;
	case SQLITE_OK:
		break;
	default:
		sqlbox_warnx(&box->cfg, "%s: cannot prepare-"
			"bind: %s", db->src->fname, 
			sqlite3_errmsg(db->db));
		sqlbox_warnx(&box->cfg, "%s: statement: %s",
			db->src->fname, pst->stmt);
		if (stmt != NULL)
			sqlite3_finalize(stmt);
		return 0;
	}

	/* Now bind parameters while we read them. */

	for (i = 0; i < parmsz; i++) {
		if (sz < sizeof(uint32_t))
			goto badframe;
		type = (enum sqlbox_boundt)
			ntohl(*(uint32_t *)buf);
		buf += sizeof(uint32_t);
		sz -= sizeof(uint32_t);

		switch (type) {
		case SQLBOX_BOUND_INT:
			if (sz < sizeof(uint32_t))
				goto badframe;
			c = sqlite3_bind_int64(stmt, i + 1,
				ntohl(*(uint32_t *)buf));
			buf += sizeof(uint32_t);
			sz -= sizeof(uint32_t);
			break;
		case SQLBOX_BOUND_NULL:
			c = sqlite3_bind_null(stmt, i + 1);
			break;
		case SQLBOX_BOUND_STRING:
			if (sz < sizeof(uint32_t))
				goto badframe;
			val = ntohl(*(uint32_t *)buf);
			buf += sizeof(uint32_t);
			sz -= sizeof(uint32_t);
			if (sz < val)
				goto badframe;
			c = sqlite3_bind_text16(stmt, i + 1,
				buf, val, SQLITE_TRANSIENT);
			buf += val;
			sz -= val;
			break;
		default:
			sqlbox_warnx(&box->cfg, "cannot prepare-"
				"bind: bad type: %d", type);
			sqlbox_warnx(&box->cfg, "%s: statement: %s",
				db->src->fname, stmt);
			sqlite3_finalize(stmt);
			return 0;
		}
	}

	/* 
	 * Success!
	 * Allocate and queue up, then send our identifier back to the
	 * client just as with the open.
	 */

	if ((st = calloc(1, sizeof(struct sqlbox_stmt))) == NULL) {
		sqlbox_warn(&box->cfg, "calloc");
		sqlite3_finalize(stmt);
		return 0;
	}

	st->stmt = stmt;
	st->pstmt = pst;
	st->idx = idx;
	st->db = db;
	st->id = ++box->lastid;
	TAILQ_INSERT_TAIL(&db->stmtq, st, entries);

	ack = htonl(db->id);
	return sqlbox_write(box, (char *)&ack, sizeof(uint32_t));

badframe:
	sqlbox_warnx(&box->cfg, "cannot "
		"prepare-bind: bad frame size: %zu", sz);
	sqlbox_warnx(&box->cfg, "%s: statement: %s",
		db->src->fname, pst->stmt);
	if (stmt != NULL)
		sqlite3_finalize(stmt);
	return 0;
}

int
sqlbox_role(struct sqlbox *box, size_t role)
{
	uint32_t	 v = htonl(role);

	return sqlbox_write_frame(box, 
		SQLBOX_OP_ROLE, (char *)&v, sizeof(uint32_t));
}

/*
 * Change our role.
 * First validate the requested role and its transition, then perform
 * the actual transition.
 * Returns TRUE on success (the role was changed), FALSE otherwise.
 */
static int
sqlbox_op_role(struct sqlbox *box, const char *buf, size_t sz)
{
	size_t	 role;

	/* Verify roles and transition. */

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "cannot change "
			"role: bad frame size: %zu", sz);
		return 0;
	} else if (box->cfg.roles.rolesz == 0) {
		sqlbox_warnx(&box->cfg, "cannot change "
			"role: no roles configured");
		return 0;
	}
	role = ntohl(*(uint32_t *)buf);
	if (role > box->cfg.roles.rolesz) {
		sqlbox_warnx(&box->cfg, "cannot change role: "
			"invalid role %zu (have %zu)", role, 
			box->cfg.roles.rolesz);
		return 0;
	} else if (!sqlbox_rolecheck_role(box, role)) {
		sqlbox_warnx(&box->cfg, "cannot change role: "
			"sqlbox_rolecheck_role");
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
			sqlbox_warn(&p->cfg, "ppoll (main)");
			return 0;
		}

		/*
		 * Error out if we have bad descriptor values, if we
		 * received a HUP (without getting an EOF marker), or we
		 * otherwise don't have a read event.
		 */
		
		if ((pfd.revents & (POLLNVAL|POLLERR)))  {
			sqlbox_warnx(&p->cfg, 
				"ppoll (main): nval");
			return 0;
		} else if ((pfd.revents & POLLHUP) && 
		           !(pfd.revents & POLLIN)) {
			sqlbox_warnx(&p->cfg, 
				"ppoll (main): hangup");
			break;
		} else if (!(POLLIN & pfd.revents)) {
			sqlbox_warnx(&p->cfg, 
				"ppoll (main): bad event");
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
		case SQLBOX_OP_CLOSE:
			if (sqlbox_op_close(p, buf + 8, sz))
				break;
			sqlbox_warnx(&p->cfg, "sqlbox_op_close");
			return 0;
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
		case SQLBOX_OP_PREPARE_BIND:
			if (sqlbox_op_prepare_bind(p, buf + 8, sz))
				break;
			sqlbox_warnx(&p->cfg, "sqlbox_op_prepare_bind");
			return 0;
		case SQLBOX_OP_ROLE:
			if (sqlbox_op_role(p, buf + 8, sz))
				break;
			sqlbox_warnx(&p->cfg, "sqlbox_op_role");
			return 0;
		default:
			sqlbox_warnx(&p->cfg, 
				"ppoll (main): unknown op: %d", op);
			return 0;
		}

		sz = 0;
	}

	return 1;
}

