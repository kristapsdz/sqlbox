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

typedef	int (*sqlbox_op)(struct sqlbox *, const char *, size_t);

static	const sqlbox_op ops[SQLBOX_OP__MAX] = {
	sqlbox_op_close, /* SQLBOX_OP_CLOSE */
	sqlbox_op_exec_async, /* SQLBOX_OP_EXEC_ASYNC */
	sqlbox_op_exec_sync, /* SQLBOX_OP_EXEC_SYNC */
	sqlbox_op_finalise, /* SQLBOX_OP_FINAL */
	sqlbox_op_lastid, /* SQLBOX_OP_LASTID */
	sqlbox_op_msg_set_dat, /* SQLBOX_OP_MSG_SET_DAT */
	sqlbox_op_open_async, /* SQLBOX_OP_OPEN_ASYNC */
	sqlbox_op_open_sync, /* SQLBOX_OP_OPEN_SYNC */
	sqlbox_op_ping, /* SQLBOX_OP_PING */
	sqlbox_op_prepare_bind_async, /* SQLBOX_OP_PREPARE_BIND_ASYNC */
	sqlbox_op_prepare_bind_sync, /* SQLBOX_OP_PREPARE_BIND_SYNC */
	sqlbox_op_rebind, /* SQLBOX_OP_REBIND */
	sqlbox_op_role, /* SQLBOX_OP_ROLE */
	sqlbox_op_step, /* SQLBOX_OP_STEP */
	sqlbox_op_trans_close, /* SQLBOX_OP_TRANS_CLOSE */
	sqlbox_op_trans_open, /* SQLBOX_OP_TRANS_OPEN */
};

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

struct sqlbox_stmt *
sqlbox_stmt_find(struct sqlbox *box, size_t id)
{
	struct sqlbox_stmt	*stmt;

	if (id == 0 && TAILQ_EMPTY(&box->stmtq)) {
		sqlbox_warnx(&box->cfg, "requesting "
			"last statement with no statements");
		return 0;
	} else if (id == 0)
		return TAILQ_LAST(&box->stmtq, sqlbox_stmtq);

	TAILQ_FOREACH(stmt, &box->stmtq, gentries)
		if (stmt->id == id)
			return stmt;

	sqlbox_warnx(&box->cfg, "cannot find statement: %zu", id);
	return NULL;
}


struct sqlbox_db *
sqlbox_db_find(struct sqlbox *box, size_t id)
{
	struct sqlbox_db	*db;

	if (id == 0 && TAILQ_EMPTY(&box->dbq)) {
		sqlbox_warnx(&box->cfg, "requesting "
			"last database with no databases");
		return 0;
	} else if (id == 0)
		return TAILQ_LAST(&box->dbq, sqlbox_dbq);

	TAILQ_FOREACH(db, &box->dbq, entries)
		if (db->id == id)
			return db;

	sqlbox_warnx(&box->cfg, "cannot find source: %zu", id);
	return NULL;
}

int
sqlbox_main_loop(struct sqlbox *box)
{
	size_t		 framesz, bufsz = 0;
	const char	*frame;
	enum sqlbox_op	 op;
	int		 c, rc = 0;
	char		*buf = NULL;

	for (;;) {
		c = sqlbox_read_frame
			(box, &buf, &bufsz, &frame, &framesz);
		if (c < 0) {
			sqlbox_warnx(&box->cfg, "sqlbox_read_frame");
			break;
		} else if (c == 0) {
			rc = 1;
			break;
		}

		if (framesz < sizeof(uint32_t)) {
			sqlbox_warnx(&box->cfg, "bad "
				"frame size: %zu", framesz);
			break;
		}

		op = (enum sqlbox_op)le32toh
			(*(const uint32_t *)frame);
		frame += sizeof(uint32_t);
		framesz -= sizeof(uint32_t);

		if (op >= SQLBOX_OP__MAX) {
			sqlbox_warnx(&box->cfg, "unknown op: %d", op);
			break;
		}

		if (!(ops[op])(box, frame, framesz)) {
			sqlbox_warnx(&box->cfg, "sqlbox_op(%d)", op);
			break;
		}
	}

	free(buf);
	return rc;
}

