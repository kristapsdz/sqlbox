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
	size_t		 	 i, iters = 10000;
	sqlite3			*db;
	sqlite3_stmt		*stmt = NULL;
	int			 c;
	const char		*stmts[] = {
		"CREATE TABLE foo (col1 INT, col2 INT, col3 INT, col4 INT)",
		"INSERT INTO foo (col1, col2, col3, col4) VALUES (?,?,?,?)",
	};

	if (pledge("stdio rpath cpath wpath flock fattr proc", NULL) == -1)
		err(EXIT_FAILURE, "pledge");

	while ((c = getopt(argc, argv, "n:")) != -1)
		switch (c) {
		case 'n':
			iters = atoi(optarg);
			break;
		default:
			return EXIT_FAILURE;
		}

	printf(">>> %zu iterations\n", iters);

	for (i = 0; i < iters; i++) {
		if (sqlite3_open_v2(":memory:", &db, 
		    SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK)
			errx(EXIT_FAILURE, "sqlite3_open_v2");
		if (sqlite3_exec(db, stmts[0], NULL, NULL, NULL) != SQLITE_OK)
			errx(EXIT_FAILURE, "sqlite3_exec");
		stmt = NULL;
		if (sqlite3_prepare_v2(db, stmts[1], -1, &stmt, NULL) != SQLITE_OK)
			errx(EXIT_FAILURE, "sqlite3_prepare_v2");
		if (sqlite3_bind_int64(stmt, 1, arc4random()) != SQLITE_OK)
			errx(EXIT_FAILURE, "sqlite3_bind_int64");
		if (sqlite3_bind_int64(stmt, 2, arc4random()) != SQLITE_OK)
			errx(EXIT_FAILURE, "sqlite3_bind_int64");
		if (sqlite3_bind_int64(stmt, 3, arc4random()) != SQLITE_OK)
			errx(EXIT_FAILURE, "sqlite3_bind_int64");
		if (sqlite3_bind_int64(stmt, 4, arc4random()) != SQLITE_OK)
			errx(EXIT_FAILURE, "sqlite3_bind_int64");
		if (sqlite3_step(stmt) != SQLITE_DONE)
			errx(EXIT_FAILURE, "sqlite3_step");
		if (sqlite3_finalize(stmt))
			errx(EXIT_FAILURE, "sqlite3_finalize");
		if (sqlite3_close(db) != SQLITE_OK)
			errx(EXIT_FAILURE, "sqlite3_close");
	}

	puts("<<< done");
	return EXIT_SUCCESS;
}
