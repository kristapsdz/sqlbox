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
#include <inttypes.h>
#include <float.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../sqlbox.h"
#include "regress.h"

#define	DBL_MAX_STR \
	"1797693134862315708145274237317043567980705675258449965989174768" \
	"0315726078002853876058955863276687817154045895351438246423432132" \
	"6889464182768467546703537516986049910576551282076245490090389328" \
	"9440758685084551339423045832369032229481658085593321233482747978" \
	"26204144723168738177180919299881250404026184124858368."

int
main(int argc, char *argv[])
{
	struct sqlbox_parm	 parm = {
		.fparm = DBL_MAX,
		.type = SQLBOX_PARM_FLOAT,
		.sz = sizeof(double)
	};
	void			*res_blob;
	char			 res_blob_array[512];
	char			*res_string;
	char			 res_string_array[512];
	int64_t			 res_int;
	double			 res_double;
	size_t			 sz;
	int			 c;

	c = sqlbox_parm_blob(&parm,
		res_blob_array, sizeof(res_blob_array), &sz);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_blob convert");
	if (sz != sizeof(double))
		errx(EXIT_FAILURE, "sqlbox_parm_blob size");
	if (*(double *)res_blob_array != DBL_MAX)
		errx(EXIT_FAILURE, "sqlbox_parm_blob value");

	c = sqlbox_parm_blob_alloc(&parm, &res_blob, &sz);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_blob_array convert");
	if (sz != sizeof(double))
		errx(EXIT_FAILURE, "sqlbox_parm_blob_array size");
	if (*(double *)res_blob != DBL_MAX)
		errx(EXIT_FAILURE, "sqlbox_parm_blob_array value");
	free(res_blob);

	c = sqlbox_parm_float(&parm, &res_double);
	if (c != 0)
		errx(EXIT_FAILURE, "sqlbox_parm_float convert");
	if (res_double != DBL_MAX)
		errx(EXIT_FAILURE, "sqlbox_parm_float value");

	c = sqlbox_parm_int(&parm, &res_int);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_int convert");
	if (res_int != INT64_MAX)
		errx(EXIT_FAILURE, "sqlbox_parm_int value");

	c = sqlbox_parm_string(&parm,
		res_string_array, sizeof(res_string_array), &sz);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_string convert");
	if (sz < strlen(DBL_MAX_STR))
		errx(EXIT_FAILURE, "sqlbox_parm_string size");
	if (strncmp(res_string_array, DBL_MAX_STR, strlen(DBL_MAX_STR)))
		errx(EXIT_FAILURE, "sqlbox_parm_string value");

	c = sqlbox_parm_string_alloc(&parm, &res_string, &sz);
	if (c != 1)
		errx(EXIT_FAILURE, "sqlbox_parm_string_array convert");
	if (sz < strlen(DBL_MAX_STR))
		errx(EXIT_FAILURE, "sqlbox_parm_string size");
	if (strncmp(res_string, DBL_MAX_STR, strlen(DBL_MAX_STR)))
		errx(EXIT_FAILURE, "sqlbox_parm_string value");
	free(res_string);

	return EXIT_SUCCESS;
}
