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
#include "../config.h"

#if HAVE_ERR
# include <err.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "../sqlbox.h"
#include "regress.h"

int
main(int argc, char *argv[])
{
	struct sqlbox_parm	 parm = {
		.sparm = (char *)"10",
		.type = SQLBOX_PARM_STRING,
		.sz = 3
	};
	void			*res_blob;
	char			 res_blob_array[64];
	char			*res_string;
	char			 res_string_array[64];
	int64_t			 res_int;
	double			 res_double;
	size_t			 sz;
	int			 c;

	c = sqlbox_parm_blob(&parm,
		res_blob_array, sizeof(res_blob_array), &sz);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_blob convert");
	if (sz != 3)
		errx(EXIT_FAILURE, "sqlbox_parm_blob size");
	if (memcmp(res_blob_array, "10", 3))
		errx(EXIT_FAILURE, "sqlbox_parm_blob value");

	c = sqlbox_parm_blob_alloc(&parm, &res_blob, &sz);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_blob_array convert");
	if (sz != 3)
		errx(EXIT_FAILURE, "sqlbox_parm_blob_array size");
	if (memcmp(res_blob, "10", 3))
		errx(EXIT_FAILURE, "sqlbox_parm_blob value");
	free(res_blob);

	c = sqlbox_parm_float(&parm, &res_double);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_float convert");
	if (res_double != 10.0)
		errx(EXIT_FAILURE, "sqlbox_parm_float value");

	c = sqlbox_parm_int(&parm, &res_int);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_int convert");
	if (res_int != 10)
		errx(EXIT_FAILURE, "sqlbox_parm_int value");

	c = sqlbox_parm_string(&parm,
		res_string_array, sizeof(res_string_array), &sz);
	if (c != 0)
		errx(EXIT_FAILURE, "sqlbox_parm_string convert");
	if (sz != 2)
		errx(EXIT_FAILURE, "sqlbox_parm_string size");
	if (strcmp(res_string_array, "10"))
		errx(EXIT_FAILURE, "sqlbox_parm_string value");

	c = sqlbox_parm_string_alloc(&parm, &res_string, &sz);
	if (c != 0)
		errx(EXIT_FAILURE, "sqlbox_parm_string_array convert");
	if (sz != 2)
		errx(EXIT_FAILURE, "sqlbox_parm_string_array size");
	if (strcmp(res_string, "10"))
		errx(EXIT_FAILURE, "sqlbox_parm_string_array value");
	free(res_string);

	return EXIT_SUCCESS;
}
