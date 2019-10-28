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
	struct sqlbox_role_hier	*hier;
	struct sqlbox_roles	 roles;
	size_t			 i, j;

	if ((hier = sqlbox_role_hier_alloc(6)) == NULL)
		err(EXIT_FAILURE, "sqlbox_role_hier_alloc");

	if (!sqlbox_role_hier_child(hier, 0, 1))
		errx(EXIT_FAILURE, "sqlbox_role_hier_child");
	if (!sqlbox_role_hier_child(hier, 0, 2))
		errx(EXIT_FAILURE, "sqlbox_role_hier_child");
	if (!sqlbox_role_hier_child(hier, 2, 3))
		errx(EXIT_FAILURE, "sqlbox_role_hier_child");
	if (!sqlbox_role_hier_child(hier, 2, 4))
		errx(EXIT_FAILURE, "sqlbox_role_hier_child");
	if (!sqlbox_role_hier_child(hier, 4, 5))
		errx(EXIT_FAILURE, "sqlbox_role_hier_child");
	if (!sqlbox_role_hier_write(hier, &roles))
		errx(EXIT_FAILURE, "sqlbox_role_hier_write");

	if (roles.roles[0].rolesz != 5)
		errx(EXIT_FAILURE, "wrong role sizes");

	for (i = 1; i <= 5; i++) {
		for (j = 0; j < roles.roles[0].rolesz; j++) 
			if (roles.roles[0].roles[j] == i)
				break;
		if (j == roles.roles[0].rolesz)
			errx(EXIT_FAILURE, "role not specified");
	}

	if (roles.roles[1].rolesz != 0)
		errx(EXIT_FAILURE, "wrong role sizes");
	if (roles.roles[2].rolesz != 3)
		errx(EXIT_FAILURE, "wrong role sizes");

	for (i = 3; i <= 5; i++) {
		for (j = 0; j < roles.roles[0].rolesz; j++) 
			if (roles.roles[0].roles[j] == i)
				break;
		if (j == roles.roles[0].rolesz)
			errx(EXIT_FAILURE, "role not specified");
	}

	if (roles.roles[3].rolesz != 0)
		errx(EXIT_FAILURE, "wrong role sizes");
	if (roles.roles[4].rolesz != 1)
		errx(EXIT_FAILURE, "wrong role sizes");
	if (roles.roles[4].roles[0] != 5)
		errx(EXIT_FAILURE, "wrong roles");

	sqlbox_role_hier_write_free(&roles);
	sqlbox_role_hier_free(hier);
	return EXIT_SUCCESS;
}
