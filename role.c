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
#if HAVE_ENDIAN_H
# include <endian.h>
#else
# include <sys/endian.h>
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

/*
 * Return TRUE if the current role has the ability to transition to the
 * given role (or it's the same role), FALSE if otherwise.
 */
static int
sqlbox_rolecheck_role(struct sqlbox *box, size_t idx)
{
	size_t	 i;

	assert(box->cfg.roles.rolesz);
	if (box->role == idx) {
		sqlbox_warnx(&box->cfg, "role: changing into "
			"current role %zu (harmless)", idx);
		return 1;
	}
	for (i = 0; i < box->cfg.roles.roles[box->role].rolesz; i++)
		if (box->cfg.roles.roles[box->role].roles[i] == idx)
			return 1;
	sqlbox_warnx(&box->cfg, "role: role %zu denied to role %zu",
		idx, box->role);
	return 0;
}

int
sqlbox_role(struct sqlbox *box, size_t role)
{
	uint32_t	 v = htole32(role);

	if (!sqlbox_write_frame
	    (box, SQLBOX_OP_ROLE, (char *)&v, sizeof(uint32_t))) {
		sqlbox_warnx(&box->cfg, "role: sqlbox_write_frame");
		return 0;
	}
	return 1;
}

/*
 * Change our role.
 * First validate the requested role and its transition, then perform
 * the actual transition.
 * Returns TRUE on success (the role was changed), FALSE otherwise.
 */
int
sqlbox_op_role(struct sqlbox *box, const char *buf, size_t sz)
{
	size_t	 role;

	/* Verify roles and transition. */

	if (sz != sizeof(uint32_t)) {
		sqlbox_warnx(&box->cfg, "role: bad frame size: %zu", sz);
		return 0;
	} else if (box->cfg.roles.rolesz == 0) {
		sqlbox_warnx(&box->cfg, "role: no roles configured");
		return 0;
	}

	role = le32toh(*(uint32_t *)buf);
	if (role > box->cfg.roles.rolesz) {
		sqlbox_warnx(&box->cfg, "role: invalid role %zu "
			"(have %zu)", role, box->cfg.roles.rolesz);
		return 0;
	} else if (!sqlbox_rolecheck_role(box, role)) {
		sqlbox_warnx(&box->cfg, "role: "
			"sqlbox_rolecheck_role");
		return 0;
	}

	sqlbox_debug(&box->cfg, "role: transition "
		"%zu -> %zu", box->role, role);
	box->role = role;
	return 1;
}

