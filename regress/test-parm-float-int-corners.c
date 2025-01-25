/*
 * Copyright (c) Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <inttypes.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../sqlbox.h"
#include "regress.h"

int
main(int argc, char *argv[])
{
	struct sqlbox_parm	 parm = {
		.fparm = DBL_MAX,
		.type = SQLBOX_PARM_FLOAT,
		.sz = sizeof(double)
	};
	int64_t			 res_int;
	int			 c;

	parm.fparm = DBL_MAX;
	c = sqlbox_parm_int(&parm, &res_int);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_int convert");
	if (res_int != INT64_MAX)
		errx(EXIT_FAILURE, "sqlbox_parm_int value");

	parm.fparm = -DBL_MAX;
	c = sqlbox_parm_int(&parm, &res_int);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_int convert");
	if (res_int != INT64_MIN)
		errx(EXIT_FAILURE, "sqlbox_parm_int value");

	/* Next legit value below maximum. */

	parm.fparm = nextafter(SQLBOX_DOUBLE_MAX_INT, 0);
	c = sqlbox_parm_int(&parm, &res_int);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_int convert");
	if (res_int == INT64_MAX)
		errx(EXIT_FAILURE, "sqlbox_parm_int value");

	parm.fparm = nextafter(SQLBOX_DOUBLE_MIN_INT, 0);
	c = sqlbox_parm_int(&parm, &res_int);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_int convert");
	if (res_int == INT64_MIN)
		errx(EXIT_FAILURE, "sqlbox_parm_int value");

	parm.fparm = NAN;
	c = sqlbox_parm_int(&parm, &res_int);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_int convert");
	if (res_int != 0)
		errx(EXIT_FAILURE, "sqlbox_parm_int value");

	parm.fparm = INFINITY;
	c = sqlbox_parm_int(&parm, &res_int);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_int convert");
	if (res_int != INT64_MAX)
		errx(EXIT_FAILURE, "sqlbox_parm_int value");

	parm.fparm = -INFINITY;
	c = sqlbox_parm_int(&parm, &res_int);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_int convert");
	if (res_int != INT64_MIN)
		errx(EXIT_FAILURE, "sqlbox_parm_int value");

	return EXIT_SUCCESS;
}
