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
		.fparm = 10.0,
		.type = SQLBOX_PARM_FLOAT,
		.sz = sizeof(double)
	};
	char			 res_blob_array[64];
	char			 res_string_array[64];
	size_t			 sz;
	int			 c;

	c = sqlbox_parm_blob(&parm, res_blob_array, 0, &sz);
	if (c != -1)
		errx(EXIT_FAILURE, "sqlbox_parm_blob should fail");

	c = sqlbox_parm_string(&parm, res_string_array, 0, &sz);
	if (c != -1)
		errx(EXIT_FAILURE, "sqlbox_parm_string should fail");

	return EXIT_SUCCESS;
}
