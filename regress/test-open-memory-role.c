
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

#ifndef nitems
# define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

int
main(int argc, char *argv[])
{
	struct sqlbox		*p;
	struct sqlbox_cfg	 cfg;
	struct sqlbox_src	 srcs[] = {
		{ .fname = (char *)":memory:",
		  .mode = SQLBOX_SRC_RWC }
	};
	struct sqlbox_role	 roles[] = {
		{ .rolesz = 0,
		  .stmtsz = 0,
		  .srcs = (size_t[]){ 0 },
		  .srcsz = 1 }
	};

	memset(&cfg, 0, sizeof(struct sqlbox_cfg));
	cfg.srcs.srcs = srcs;
	cfg.srcs.srcsz = nitems(srcs);
	cfg.roles.roles = roles;
	cfg.roles.rolesz = nitems(roles);
	cfg.roles.defrole = 0;
	cfg.msg.func_short = warnx;

	if ((p = sqlbox_alloc(&cfg)) == NULL)
		return EXIT_FAILURE;
	if (!sqlbox_open(p, 0))
		return EXIT_FAILURE;
	if (!sqlbox_ping(p))
		return EXIT_FAILURE;

	sqlbox_free(p);
	return EXIT_SUCCESS;
}
