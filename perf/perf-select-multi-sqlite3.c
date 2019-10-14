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
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

int
main(int argc, char *argv[])
{
	size_t		 	 i, rows = 10000;
	sqlite3			*db;
	sqlite3_stmt		*stmt = NULL;
	int			 c;
	int64_t			 val;
	const char		*stmts[] = {
		"CREATE TABLE foo (col INT)",
		"INSERT INTO foo WITH RECURSIVE cte(x) AS "
			"(SELECT random() UNION ALL"
			" SELECT random() FROM cte LIMIT ?"
		 	") SELECT abs(x) FROM cte;",
		"SELECT col FROM foo"
	};

	if (pledge("stdio rpath cpath wpath flock fattr proc", NULL) == -1)
		err(EXIT_FAILURE, "pledge");

	while ((c = getopt(argc, argv, "n:")) != -1)
		switch (c) {
		case 'n':
			rows = atoi(optarg);
			break;
		default:
			return EXIT_FAILURE;
		}

	printf(">>> %zu insertions\n", rows);

	if (sqlite3_open_v2(":memory:", &db, 
	    SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK)
		errx(EXIT_FAILURE, "sqlite3_open_v2");
	if (sqlite3_exec(db, stmts[0], NULL, NULL, NULL) != SQLITE_OK)
		errx(EXIT_FAILURE, "sqlite3_exec");

	if (sqlite3_prepare_v2(db, stmts[1], -1, &stmt, NULL) != SQLITE_OK)
		errx(EXIT_FAILURE, "sqlite3_prepare_v2");
	if (sqlite3_bind_int64(stmt, 1, rows) != SQLITE_OK)
		errx(EXIT_FAILURE, "sqlite3_bind_int64");
	if (sqlite3_step(stmt) != SQLITE_DONE)
		errx(EXIT_FAILURE, "sqlite3_step");
	if (sqlite3_finalize(stmt))
		errx(EXIT_FAILURE, "sqlite3_finalize");

	if (sqlite3_prepare_v2(db, stmts[2], -1, &stmt, NULL) != SQLITE_OK)
		errx(EXIT_FAILURE, "sqlite3_prepare_v2");

	for (i = 0; i < rows; i++) {
		if (sqlite3_step(stmt) != SQLITE_ROW)
			errx(EXIT_FAILURE, "sqlite3_step");
		val = sqlite3_column_int64(stmt, 0);
		if (val < 0)
			errx(EXIT_FAILURE, "negative!?");
	}

	if (sqlite3_step(stmt) != SQLITE_DONE)
		errx(EXIT_FAILURE, "sqlite3_step");
	if (sqlite3_finalize(stmt))
		errx(EXIT_FAILURE, "sqlite3_finalize");
	if (sqlite3_close(db) != SQLITE_OK)
		errx(EXIT_FAILURE, "sqlite3_close");

	puts("<<< done");
	return EXIT_SUCCESS;
}
