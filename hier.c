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
#include <sys/socket.h>

#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <errno.h>
#if !HAVE_SOCK_NONBLOCK
# include <fcntl.h>
#endif
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

struct	sqlbox_role_hier {
	size_t		*parents;
	size_t		 sz;
};

struct sqlbox_role_hier *
sqlbox_role_hier_alloc(size_t rolesz)
{
	struct sqlbox_role_hier	*p;
	size_t			 i;

	if ((p = calloc(1, sizeof(struct sqlbox_role_hier))) == NULL)
		return NULL;

	p->sz = rolesz;
	if (p->sz > 0 && 
	    (p->parents = calloc(rolesz, sizeof(size_t))) == NULL) {
		free(p);
		return NULL;
	}
	for (i = 0; i < p->sz; i++)
		p->parents[i] = i;

	return p;
}

void
sqlbox_role_hier_free(struct sqlbox_role_hier *p)
{

	if (p == NULL)
		return;
	free(p->parents);
	free(p);
}

int
sqlbox_role_hier_child(struct sqlbox_role_hier *p, size_t parent, size_t child)
{
	size_t	 idx;

	/* Make sure not out of bounds or already set. */

	if (parent >= p->sz || child >= p->sz)
		return 0;
	if (p->parents[child] != child)
		return 0;

	/* Ignore self-reference. */

	if (child == parent)
		return 1;

	/* Make sure no ancestors are the self. */

	for (idx = parent; ; idx = p->parents[idx])
		if (p->parents[idx] == child)
			return 0;
		else if (p->parents[idx] == idx)
			break;

	p->parents[child] = parent;
	return 1;
}

int
sqlbox_role_hier_write(const struct sqlbox_role_hier *p, struct sqlbox_role *roles)
{
	size_t	 i, idx, pidx;

	/*
	 * A simple algorithm.
	 * First, we're guaranteed that we have a set of DAGs.
	 * For each node that has a parent, traverse upward, allowing
	 * the parent to transition into the child.
	 */

	for (i = 0; i < p->sz; i++) {
		free(roles[i].roles);
		roles[i].rolesz = 0;
	}

	/* 
	 * Start by collecting the number of outbound roles we're going
	 * to have per parent.
	 */

	for (i = 0; i < p->sz; i++) {
		idx = i;
		while (p->parents[idx] != idx) {
			roles[p->parents[idx]].rolesz++;
			idx = p->parents[idx];
		}
	}

	/* 
	 * Now allocate space.
	 * Reset the number of roles because we're going to use it as
	 * where to index into the role array in the next step.
	 */

	for (i = 0; i < p->sz; i++) {
		if (roles[i].rolesz == 0)
			continue;
		roles[i].roles = calloc(roles[i].rolesz, sizeof(size_t));
		if (roles[i].roles == NULL)
			return 0;
		roles[i].rolesz = 0;
	}

	/* Assign children. */

	for (i = 0; i < p->sz; i++) {
		idx = i;
		while (p->parents[idx] != idx) {
			pidx = p->parents[idx];
			roles[pidx].roles[roles[pidx].rolesz++] = idx;
			idx = pidx;
		}
	}

	return 1;
}
