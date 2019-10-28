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

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h> /* memset */

#include <sqlite3.h>

#include "sqlbox.h"
#include "extern.h"

struct	sqlbox_role_node {
	size_t		 parent; /* parent role */
	size_t		*srcs; /* available sources */
	size_t		 srcsz; /* length of srcs */
	size_t		*stmts; /* available statements */
	size_t		 stmtsz; /* length of stmts */
};

struct	sqlbox_role_hier {
	struct sqlbox_role_node	*roles; /* per-role perms */
	size_t		  	 sz; /* number of roles */
};

struct sqlbox_role_hier *
sqlbox_role_hier_alloc(size_t rolesz)
{
	struct sqlbox_role_hier	*p;
	size_t			 i;

	if ((p = calloc(1, sizeof(struct sqlbox_role_hier))) == NULL)
		return NULL;

	p->sz = rolesz;
	if (p->sz > 0 && (p->roles = calloc
	    (rolesz, sizeof(struct sqlbox_role_node))) == NULL) {
		free(p);
		return NULL;
	}
	for (i = 0; i < p->sz; i++)
		p->roles[i].parent = i;

	return p;
}

void
sqlbox_role_hier_free(struct sqlbox_role_hier *p)
{
	size_t	 i;

	if (p == NULL)
		return;

	for (i = 0; i < p->sz; i++) {
		free(p->roles[i].srcs);
		free(p->roles[i].stmts);
	}

	free(p->roles);
	free(p);
}

int
sqlbox_role_hier_stmt(struct sqlbox_role_hier *p, size_t role, size_t stmt)
{
	void	*pp;

	/* Bounds check. */

	if (role >= p->sz)
		return 0;

	/* 
	 * Reallocate and enqueue.
	 * Duplicates are ok: we'll discard them when we serialise
	 */

	pp = reallocarray(p->roles[role].stmts, 
		p->roles[role].stmtsz + 1, sizeof(size_t));
	if (pp == NULL)
		return 0;
	p->roles[role].stmts = pp;
	p->roles[role].stmts[p->roles[role].stmtsz++] = stmt;
	return 1;
}


int
sqlbox_role_hier_src(struct sqlbox_role_hier *p, size_t role, size_t src)
{
	void	*pp;

	/* Bounds check. */

	if (role >= p->sz)
		return 0;

	/* 
	 * Reallocate and enqueue.
	 * Duplicates are ok: we'll discard them when we serialise
	 */

	pp = reallocarray(p->roles[role].srcs, 
		p->roles[role].srcsz + 1, sizeof(size_t));
	if (pp == NULL)
		return 0;
	p->roles[role].srcs = pp;
	p->roles[role].srcs[p->roles[role].srcsz++] = src;
	return 1;
}

int
sqlbox_role_hier_child(struct sqlbox_role_hier *p, size_t parent, size_t child)
{
	size_t	 idx;

	/* Make sure not out of bounds or already set. */

	if (parent >= p->sz || child >= p->sz)
		return 0;
	if (p->roles[child].parent != child)
		return 0;

	/* Ignore self-reference. */

	if (child == parent)
		return 1;

	/* Make sure no ancestors are the self. */

	for (idx = parent; ; idx = p->roles[idx].parent)
		if (p->roles[idx].parent == child)
			return 0;
		else if (p->roles[idx].parent == idx)
			break;

	p->roles[child].parent = parent;
	return 1;
}

void
sqlbox_role_hier_write_free(struct sqlbox_roles *r)
{
	size_t	 i;

	if (r == NULL)
		return;

	for (i = 0; i < r->rolesz; i++) {
		free(r->roles[i].roles);
		free(r->roles[i].stmts);
		free(r->roles[i].srcs);
	}

	free(r->roles);
	memset(r, 0, sizeof(struct sqlbox_roles));
}

/*
 * A simple algorithm.
 * First, we're guaranteed that we have a set of DAGs.
 * For each node that has a parent, traverse upward, allowing the parent
 * to transition into the child.
 * After that, propogate statements and sources downward, augmenting any
 * descendents' permissions.
 */
int
sqlbox_role_hier_write(const struct sqlbox_role_hier *p, 
	struct sqlbox_roles *r)
{
	size_t	  i, j, k, idx, pidx;
	void	*pp;

	memset(r, 0, sizeof(struct sqlbox_roles));

	r->roles = calloc(p->sz, sizeof(struct sqlbox_role));
	if (r->roles == NULL)
		goto err;
	r->rolesz = p->sz;

	/* 
	 * Start by collecting the number of outbound roles we're going
	 * to have per parent.
	 * Each parent needs to have space for each of its descendents.
	 */

	for (i = 0; i < p->sz; i++) {
		idx = i;
		while (p->roles[idx].parent != idx) {
			r->roles[p->roles[idx].parent].rolesz++;
			idx = p->roles[idx].parent;
		}
	}

	/* 
	 * Now allocate space.
	 * Reset the number of roles because we're going to use it as
	 * where to index into the role array in the next step.
	 */

	for (i = 0; i < p->sz; i++) {
		if (r->roles[i].rolesz == 0)
			continue;
		r->roles[i].roles = calloc
			(r->roles[i].rolesz, sizeof(size_t));
		if (r->roles[i].roles == NULL)
			goto err;
		r->roles[i].rolesz = 0;
	}

	/* Assign children. */

	for (i = 0; i < p->sz; i++) {
		idx = i;
		while (p->roles[idx].parent != idx) {
			pidx = p->roles[idx].parent;
			r->roles[pidx].roles[r->roles[pidx].rolesz++] = i;
			idx = pidx;
		}
	}

	/* Now we accumulate statements. */

	for (i = 0; i < p->sz; i++) {
		for (idx = i; ; idx = p->roles[idx].parent) {

			/* Look for all parent statements in the child. */

			for (j = 0; j < p->roles[idx].stmtsz; j++) {
				for (k = 0; k < r->roles[i].stmtsz; k++)
					if (p->roles[idx].stmts[j] ==
					    r->roles[i].stmts[k])
						break;
				if (k < r->roles[i].stmtsz)
					continue;

				/* Not found: append. */

				pp = reallocarray(r->roles[i].stmts,
					r->roles[i].stmtsz + 1,
					sizeof(size_t));
				if (pp == NULL)
					goto err;
				r->roles[i].stmts = pp;
				r->roles[i].stmts[r->roles[i].stmtsz] =
					p->roles[idx].stmts[j];
				r->roles[i].stmtsz++;
			}

			/* Now sources. */

			for (j = 0; j < p->roles[idx].srcsz; j++) {
				for (k = 0; k < r->roles[i].srcsz; k++)
					if (p->roles[idx].srcs[j] ==
					    r->roles[i].srcs[k])
						break;
				if (k < r->roles[i].srcsz)
					continue;

				/* Not found: append. */

				pp = reallocarray(r->roles[i].srcs,
					r->roles[i].srcsz + 1,
					sizeof(size_t));
				if (pp == NULL)
					goto err;
				r->roles[i].srcs = pp;
				r->roles[i].srcs[r->roles[i].srcsz] =
					p->roles[idx].srcs[j];
				r->roles[i].srcsz++;
			}

			/* Stop when we're at a root. */

			if (p->roles[idx].parent == idx)
				break;
		}
	}

	return 1;

err:
	sqlbox_role_hier_write_free(r);
	return 0;
}
