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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

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
	sqlbox_warnx(&box->cfg, "open: source %zu "
		"denied to role %zu", idx, box->role);
	return 0;
}

size_t
sqlbox_open(struct sqlbox *box, size_t src)
{
	uint32_t	 v = htole32(src), ack;

	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_OPEN_SYNC, (char *)&v, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "open: sqlbox_write_frame");
		return 0;
	}
	if (!sqlbox_read(box, (char *)&ack, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "open: sqlbox_read");
		return 0;
	}
	return (size_t)le32toh(ack);
}

int
sqlbox_open_async(struct sqlbox *box, size_t src)
{
	uint32_t	 v = htole32(src);

	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_OPEN_ASYNC, (char *)&v, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "open: sqlbox_write_frame");
		return 0;
	}
	return 1;
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
sqlbox_op_open(struct sqlbox *box, const char *buf, size_t sz, int sync)
{
	size_t		  idx, attempt = 0;
	int		  fl;
	const char	 *fn;
	struct sqlbox_db *db;
	uint32_t	  ack;

	/* Check source exists and we have permission for it. */

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "open: "
			"bad frame size: %zu", sz);
		return 0;
	}
	idx = le32toh(*(uint32_t *)buf);
	if (idx >= box->cfg.srcs.srcsz) {
		sqlbox_warnx(&box->cfg, "open: invalid source "
			"%zu (have %zu)", idx, box->cfg.srcs.srcsz);
		return 0;
	}
	assert(idx < box->cfg.srcs.srcsz);
	fn = box->cfg.srcs.srcs[idx].fname;
	if (!sqlbox_rolecheck_src(box, idx)) {
		sqlbox_warnx(&box->cfg, "%s: open: "
			"sqlbox_rolecheck_src", fn);
		return 0;
	}

	/* Allocate and prepare for open. */

	if ((db = calloc(1, sizeof(struct sqlbox_db))) == NULL) {
		sqlbox_warn(&box->cfg, "open: calloc");
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
	sqlbox_debug(&box->cfg, "sqlite3_open_v2: %s", fn);
	switch (sqlite3_open_v2(fn, &db->db, fl, NULL)) {
	case SQLITE_BUSY:
	case SQLITE_LOCKED:
	case SQLITE_PROTOCOL:
		sqlbox_debug(&box->cfg, "sqlite3_close: %s", fn);
		sqlite3_close(db->db);
		db->db = NULL;
		sqlbox_sleep(attempt++);
		goto again;
	case SQLITE_OK:
		break;
	default:
		if (db->db != NULL) {
			sqlbox_warnx(&box->cfg, "%s: open: %s", 
				fn, sqlite3_errmsg(db->db));
			sqlbox_debug(&box->cfg,
				"sqlite3_close: %s", fn);
			sqlite3_close(db->db);
		} else
			sqlbox_warnx(&box->cfg, "%s: open: "
				"sqlite3_open_v2", fn);
		free(db);
		return 0;
	}

	/* Add to list of available sources and write back id. */

	TAILQ_INSERT_TAIL(&box->dbq, db, entries);
	ack = htole32(db->id);

	if (sync && !sqlbox_write(box, (char *)&ack, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "%s: open: sqlbox_write", fn);
		TAILQ_REMOVE(&box->dbq, db, entries);
		sqlbox_debug(&box->cfg, "sqlite3_close: %s", fn);
		sqlite3_close(db->db);
		free(db);
		return 0;
	}
	return 1;
}

int
sqlbox_op_open_sync(struct sqlbox *box, const char *buf, size_t sz)
{

	return sqlbox_op_open(box, buf, sz, 1);
}

int
sqlbox_op_open_async(struct sqlbox *box, const char *buf, size_t sz)
{

	return sqlbox_op_open(box, buf, sz, 0);
}
