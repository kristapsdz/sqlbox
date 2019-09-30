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

enum	transt {
	SQLBOX_TRANS_DEFERRED = 0,
	SQLBOX_TRANS_IMMEDIATE = 1,
	SQLBOX_TRANS_EXCLUSIVE = 2,
	SQLBOX_TRANS_COMMIT = 3,
	SQLBOX_TRANS_ROLLBACK = 4,
	SQLBOX_TRANS__MAX
};

static	const char *const transts[SQLBOX_TRANS__MAX] = {
	"BEGIN DEFERRED TRANSACTION", /* SQLBOX_TRANS_DEFERRED */
	"BEGIN IMMEDIATE TRANSACTION", /* SQLBOX_TRANS_IMMEDIATE */
	"BEGIN EXCLUSIVE TRANSACTION", /* SQLBOX_TRANS_EXCLUSIVE */
	"COMMIT TRANSACTION", /* SQLBOX_TRANS_COMMIT */
	"ROLLBACK TRANSACTION", /* SQLBOX_TRANS_ROLLBACK */
};

/*
 * Serialise and send a transaction-open statement of "type" (which must
 * be a closing type) on source "srcid" identified by "tid".
 * Return TRUE on success, FALSE on failure.
 */
static int
sqlbox_trans_close(struct sqlbox *box,
	size_t srcid, size_t tid, enum transt type)
{
	char	 buf[sizeof(uint32_t) * 3];
	uint32_t v;

	/* 
	 * Don't check any values for errors: we'll do all of that in
	 * the child process and bail out if they're bad.
	 */

	v = htole32(srcid);
	memcpy(buf, &v, sizeof(uint32_t));
	v = htole32(tid);
	memcpy(buf + sizeof(uint32_t), &v, sizeof(uint32_t));
	assert(type >= SQLBOX_TRANS_COMMIT);
	assert(type < SQLBOX_TRANS__MAX);
	v = htole32(type);
	memcpy(buf + sizeof(uint32_t) * 2, &v, sizeof(uint32_t));
	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_TRANS_CLOSE, buf, sizeof(buf))) {
		sqlbox_warnx(&box->cfg, "trans-close: sqlbox_write_frame");
		return 0;
	}
	return 1;
}

/*
 * Serialise and send a transaction-open statement of "type" (which must
 * be an opening type) on source "srcid" identified by "tid".
 * Return TRUE on success, FALSE on failure.
 */
static int
sqlbox_trans_open(struct sqlbox *box, 
	size_t srcid, size_t tid, enum transt type)
{
	char	 buf[sizeof(uint32_t) * 3];
	uint32_t v;

	/* 
	 * Don't check any values for errors: we'll do all of that in
	 * the child process and bail out if they're bad.
	 */

	v = htole32(srcid);
	memcpy(buf, &v, sizeof(uint32_t));
	v = htole32(tid);
	memcpy(buf + sizeof(uint32_t), &v, sizeof(uint32_t));
	assert(type < SQLBOX_TRANS__MAX);
	v = htole32(type);
	memcpy(buf + sizeof(uint32_t) * 2, &v, sizeof(uint32_t));
	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_TRANS_OPEN, buf, sizeof(buf))) {
		sqlbox_warnx(&box->cfg, "trans-open: sqlbox_write_frame");
		return 0;
	}
	return 1;
}

int
sqlbox_trans_immediate(struct sqlbox *box, size_t srcid, size_t tid)
{

	return sqlbox_trans_open(box, 
		srcid, tid, SQLBOX_TRANS_IMMEDIATE);
}

int
sqlbox_trans_exclusive(struct sqlbox *box, size_t srcid, size_t tid)
{

	return sqlbox_trans_open(box, 
		srcid, tid, SQLBOX_TRANS_EXCLUSIVE);
}

int
sqlbox_trans_deferred(struct sqlbox *box, size_t srcid, size_t tid)
{

	return sqlbox_trans_open(box, 
		srcid, tid, SQLBOX_TRANS_DEFERRED);
}

int
sqlbox_trans_rollback(struct sqlbox *box, size_t srcid, size_t tid)
{

	return sqlbox_trans_close(box,
		srcid, tid, SQLBOX_TRANS_ROLLBACK);
}

int
sqlbox_trans_commit(struct sqlbox *box, size_t srcid, size_t tid)
{

	return sqlbox_trans_close(box,
		srcid, tid, SQLBOX_TRANS_ROLLBACK);
}

int
sqlbox_op_trans_open(struct sqlbox *box, const char *buf, size_t sz)
{
	struct sqlbox_db	*db;
	size_t			 id, attempt = 0;
	enum transt		 type;

	if (sz != sizeof(uint32_t) * 3) {
		sqlbox_warnx(&box->cfg, "trans-open: "
			"bad frame size: %zu", sz);
		return 0;
	}

	/* Look up database and check no pending transaction. */

	if ((db = sqlbox_db_find
	    (box, le32toh(*(uint32_t *)buf))) == NULL) {
		sqlbox_warnx(&box->cfg, "trans-open: sqlbox_db_find");
		return 0;
	} else if (db->trans) {
		sqlbox_warnx(&box->cfg, "%s: trans-open: "
			"transaction %zu already open",
			db->src->fname, db->trans);
		return 0;
	}

	/* Verify transaction identifier. */

	id = le32toh(*(uint32_t *)(buf + sizeof(uint32_t)));
	if (id == 0) {
		sqlbox_warnx(&box->cfg, "%s: trans-open: "
			"transaction is zero", db->src->fname);
		return 0;
	}

	/* Verify transaction type. */

	type = (enum transt)le32toh
		(*(uint32_t *)(buf + sizeof(uint32_t) * 2));
	if (type >= SQLBOX_TRANS_COMMIT) {
		sqlbox_warnx(&box->cfg, "trans-open: invalid type");
		return 0;
	}

again:
	sqlbox_debug(&box->cfg, "sqlite3_exec: %s, %s",
		db->src->fname, transts[type]);
	switch (sqlite3_exec(db->db, transts[type], NULL, NULL, NULL)) {
	case SQLITE_BUSY:
	case SQLITE_LOCKED:
	case SQLITE_PROTOCOL:
		sqlbox_sleep(attempt++);
		goto again;
	case SQLITE_OK:
		break;
	default:
		sqlbox_warnx(&box->cfg, "%s: trans-open: %s", 
			db->src->fname, sqlite3_errmsg(db->db));
		return 0;
	}

	db->trans = id;
	return 1;
}

int
sqlbox_op_trans_close(struct sqlbox *box, const char *buf, size_t sz)
{
	struct sqlbox_db	*db;
	size_t			 id, attempt = 0;
	enum transt		 type;

	if (sz != sizeof(uint32_t) * 3) {
		sqlbox_warnx(&box->cfg, "trans-close: "
			"bad frame size: %zu", sz);
		return 0;
	}

	/* Look up database and check no pending transaction. */

	if ((db = sqlbox_db_find
	    (box, le32toh(*(uint32_t *)buf))) == NULL) {
		sqlbox_warnx(&box->cfg, "trans-close: sqlbox_db_find");
		return 0;
	} else if (db->trans == 0) {
		sqlbox_warnx(&box->cfg, "%s: trans-close: "
			"no transaction", db->src->fname);
		return 0;
	}

	/* Verify transaction identifier. */

	id = le32toh(*(uint32_t *)(buf + sizeof(uint32_t)));
	if (id == 0) {
		sqlbox_warnx(&box->cfg, "%s: trans-close: "
			"transaction is zero", db->src->fname);
		return 0;
	} else if (db->trans != id) {
		sqlbox_warnx(&box->cfg, "%s: trans-close: "
			"wrong transaction %zu (have %zu)", 
			db->src->fname, id, db->trans);
		return 0;
	}

	/* Verify transaction type. */

	type = (enum transt)le32toh
		(*(uint32_t *)(buf + sizeof(uint32_t) * 2));
	if (type < SQLBOX_TRANS_COMMIT || type >= SQLBOX_TRANS__MAX) {
		sqlbox_warnx(&box->cfg, "trans-close: invalid type");
		return 0;
	}

again:
	sqlbox_debug(&box->cfg, "sqlite3_exec: %s, %s",
		db->src->fname, transts[type]);
	switch (sqlite3_exec(db->db, transts[type], NULL, NULL, NULL)) {
	case SQLITE_BUSY:
	case SQLITE_LOCKED:
	case SQLITE_PROTOCOL:
		sqlbox_sleep(attempt++);
		goto again;
	case SQLITE_OK:
		break;
	default:
		sqlbox_warnx(&box->cfg, "%s: trans-close: %s", 
			db->src->fname, sqlite3_errmsg(db->db));
		return 0;
	}

	db->trans = 0;
	return 1;
}
