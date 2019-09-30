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

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

int
sqlbox_lastid(struct sqlbox *box, size_t id, int64_t *res)
{
	uint32_t	 v = htole32(id);
	int64_t		 ack;

	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_LASTID, (char *)&v, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "lastid: sqlbox_write_frame");
		return 0;
	}
	if (!sqlbox_read(box, (char *)&ack, sizeof(int64_t))) {
		sqlbox_warnx(&box->cfg, "lastid: sqlbox_read");
		return 0;
	}
	*res = le64toh(ack);
	return 1;
}

int
sqlbox_op_lastid(struct sqlbox *box, const char *buf, size_t sz)
{
	struct sqlbox_db	*db;
	int64_t			 ack;

	/* Look up the statement in our global list. */

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "lastid: "
			"bad frame size: %zu", sz);
		return 0;
	}
	if ((db = sqlbox_db_find
	    (box, le32toh(*(uint32_t *)buf))) == NULL) {
		sqlbox_warnx(&box->cfg, "lastid: sqlbox_db_find");
		return 0;
	}

	sqlbox_debug(&box->cfg, "sqlite3_last_insert_rowid: %s", 
		db->src->fname);
	ack = htole64(sqlite3_last_insert_rowid(db->db));

	if (!sqlbox_write(box, (char *)&ack, sizeof(int64_t))) {
		sqlbox_warnx(&box->cfg, "lastid: sqlbox_write");
		return 0;
	}
	return 1;
}
