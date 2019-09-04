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
void
sqlbox_sleep(size_t attempt)
{
	useconds_t	us;

	us = attempt > 100 ? 10000 :  /* 1/100 second */
	     attempt > 10  ? 100000 : /* 1/10 second */
	     250000;                  /* 1/4 second */

	/* FIXME: don't use random(). */
	usleep(us * (double)(random() / (double)RAND_MAX));
}

struct sqlbox_db *
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

int
sqlbox_step(struct sqlbox *box, size_t stmtid)
{
	uint32_t	 v = htonl(stmtid);
	const char	*frame;
	size_t		 framesz;

	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_STEP, (char *)&v, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "sqlbox_write_frame");
		return 0;
	}

	if (sqlbox_read_frame(box, &frame, &framesz) <= 0) {
		sqlbox_warnx(&box->cfg, "sqlbox_read_frame");
		return 0;
	}

	/* TODO. */

	return 1;
}

/*
 * Prepare and bind parameters to a statement in one step.
 * Return TRUE on success, FALSE on failure (nothing is allocated).
 */
static int
sqlbox_op_step(struct sqlbox *box, const char *buf, size_t sz)
{
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
sqlbox_main_loop(struct sqlbox *box)
{
	size_t		 framesz;
	const char	*frame;
	enum sqlbox_op	 op;
	int		 c;

	for (;;) {
		c = sqlbox_read_frame(box, &frame, &framesz);
		if (c < 0) {
			sqlbox_warnx(&box->cfg, "sqlbox_read_frame");
			return 0;
		} else if (c == 0)
			break;

		if (framesz < sizeof(uint32_t)) {
			sqlbox_warnx(&box->cfg, "bad "
				"frame size: %zu", framesz);
			return 0;
		}

		op = (enum sqlbox_op)ntohl
			(*(const uint32_t *)frame);
		frame += sizeof(uint32_t);
		framesz -= sizeof(uint32_t);

		sqlbox_debug(&box->cfg, "%s: size: %zu, op: %d", 
			__func__, framesz, op);

		switch (op) {
		case SQLBOX_OP_CLOSE:
			if (sqlbox_op_close(box, frame, framesz))
				break;
			sqlbox_warnx(&box->cfg, "sqlbox_op_close");
			return 0;
		case SQLBOX_OP_OPEN:
			if (sqlbox_op_open(box, frame, framesz))
				break;
			sqlbox_warnx(&box->cfg, "sqlbox_op_open");
			return 0;
		case SQLBOX_OP_PING:
			if (sqlbox_op_ping(box, frame, framesz))
				break;
			sqlbox_warnx(&box->cfg, "sqlbox_op_ping");
			return 0;
		case SQLBOX_OP_PREPARE_BIND:
			if (sqlbox_op_prepare_bind(box, frame, framesz))
				break;
			sqlbox_warnx(&box->cfg, "sqlbox_op_prepare_bind");
			return 0;
		case SQLBOX_OP_ROLE:
			if (sqlbox_op_role(box, frame, framesz))
				break;
			sqlbox_warnx(&box->cfg, "sqlbox_op_role");
			return 0;
		default:
			sqlbox_warnx(&box->cfg, "unknown op: %d", op);
			return 0;
		}
	}

	return 1;
}

