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

/*
 * Clear all allocated resources in an sqlbox.
 * Of focus are the communication channels between client and server
 * (both ways), the list of open databases and their statements (server
 * only), and the transmission buffer (both).
 * Does nothing if "p" is NULL.
 */
static void
sqlbox_clear(struct sqlbox *box)
{
	struct sqlbox_db 	*db;
	struct sqlbox_stmt	*stmt;

	if (box == NULL)
		return;
	if (box->fd != -1)
		close(box->fd);

	TAILQ_FOREACH(db, &box->dbq, entries) {
		sqlbox_warnx(&box->cfg, "%s: source %zu "
			"still open on exit", 
			db->src->fname, db->idx);
		while ((stmt = TAILQ_FIRST(&db->stmtq)) != NULL) {
			sqlbox_warnx(&box->cfg, "%s: stmt %zu "
				"source %zu not finalised on exit", 
				db->src->fname, stmt->idx, db->idx);
			sqlbox_warnx(&box->cfg, "%s: statement: %s",
				db->src->fname, stmt->pstmt->stmt);
			sqlite3_finalize(stmt->stmt);
			TAILQ_REMOVE(&db->stmtq, stmt, entries);
			TAILQ_REMOVE(&box->stmtq, stmt, gentries);
			free(stmt);
		}
		sqlite3_close(db->db);
	}

	assert(TAILQ_EMPTY(&box->stmtq));
}

void
sqlbox_free(struct sqlbox *box)
{

	sqlbox_clear(box);
	free(box);
}

/*
 * Verify internal consistency.
 * Returns FALSE on failure, TRUE on success.
 */
static int
sqlbox_cfg_vrfy(const struct sqlbox_cfg *cfg)
{
	size_t	 i, j;

	if (cfg == NULL)
		return 1;

	/* We mustn't have a NULL filename. */

	for (i = 0; i < cfg->srcs.srcsz; i++)
		if (cfg->srcs.srcs[i].fname == NULL) {
			sqlbox_warnx(cfg, "source %zu "
				"has NULL filename", i);
			return 0;
		}

	/* We mustn't have a NULL statement. */

	for (i = 0; i < cfg->stmts.stmtsz; i++)
		if (cfg->stmts.stmts[i].stmt == NULL) {
			sqlbox_warnx(cfg, "statement %zu "
				"has NULL statement", i);
			return 0;
		}

	/* The default role, if specified, must be valid. */

	if (cfg->roles.defrole &&
	    cfg->roles.defrole >= cfg->roles.rolesz) {
		sqlbox_warnx(cfg,
			"referencing invalid default role "
			"%zu (have %zu)", cfg->roles.defrole,
			cfg->roles.rolesz);
		return 0;
	}

	/* 
	 * Make sure that the statements, sources, and transitional
	 * roles available to each role are valid.
	 */

	for (i = 0; i < cfg->roles.rolesz; i++) {
		for (j = 0; j < cfg->roles.roles[i].rolesz; j++)
			if (cfg->roles.roles[i].roles[j] >= 
			    cfg->roles.rolesz) {
				sqlbox_warnx(cfg,
					"role %zu references invalid "
					"role %zu (have %zu)", i,
					cfg->roles.roles[i].roles[j],
					cfg->roles.rolesz);
				return 0;
			}
		for (j = 0; j < cfg->roles.roles[i].stmtsz; j++)
			if (cfg->roles.roles[i].stmts[j] >= 
			    cfg->stmts.stmtsz) {
				sqlbox_warnx(cfg,
					"role %zu references invalid "
					"stmt %zu (have %zu)", i,
					cfg->roles.roles[i].stmts[j],
					cfg->stmts.stmtsz);
				return 0;
			}
		for (j = 0; j < cfg->roles.roles[i].srcsz; j++)
			if (cfg->roles.roles[i].srcs[j] >= 
			    cfg->srcs.srcsz) {
				sqlbox_warnx(cfg,
					"role %zu references invalid "
					"source %zu (have %zu)", i,
					cfg->roles.roles[i].srcs[j],
					cfg->srcs.srcsz);
				return 0;
			}
	}

	return 1;
}

/*
 * Zero and initialise the memory required by an sqlbox.
 * Call sqlbox_clear() regardless of the return value.
 * Returns FALSE on memory allocation failure and TRUE otherwise.
 */
static int
sqlbox_init(struct sqlbox *box,
	const struct sqlbox_cfg *cfg, int fd, pid_t pid)
{

	memset(box, 0, sizeof(struct sqlbox));

	if (cfg != NULL)
		box->cfg = *cfg;

	/* Start in default role (no effect if roles disabled). */

	box->role = box->cfg.roles.defrole;
	box->fd = fd;
	box->pid = pid;

	TAILQ_INIT(&box->dbq);
	TAILQ_INIT(&box->stmtq);
	return 1;
}

/*
 * Given an open socket pair, fork our protected child process and begin
 * waiting for instructions.
 * Don't touch "cfg" if we fail: let the caller handle that.
 * If any descriptors in "fds" are closed, they're reassigned to -1.
 * The caller should close any remaining descriptors on failure.
 */
static struct sqlbox *
sqlbox_alloc_fd(struct sqlbox_cfg *cfg, int fds[2])
{
	struct sqlbox	 box;
	struct sqlbox	*p;
	pid_t		 pid;
	int		 rc;

	if (!sqlbox_cfg_vrfy(cfg)) {
		sqlbox_warnx(cfg, "sqlbox_cfg_vrfy");
		return NULL;
	} else if ((pid = fork()) == -1) {
		sqlbox_warn(cfg, "fork");
		return NULL;
	}

	/*
	 * Make sure that the parent doesn't free "cfg" on failure
	 * because the caller will do this.
	 */

	if (pid > 0) {
		if ((p = calloc(1, sizeof(struct sqlbox))) == NULL) {
			sqlbox_warn(cfg, "calloc");
			return NULL;
		} else if (close(fds[0]) == -1) {
			sqlbox_warn(cfg, "close");
			free(p);
			return NULL;
		}
		close(fds[0]);
		fds[0] = -1;
		if (!sqlbox_init(p, cfg, fds[1], pid)) {
			free(p);
			p = NULL;
		}
		return p;
	}

	/*
	 * From here on out, we're in the child process so we don't
	 * return to the parent caller.
	 * We can now do whatever we want with "cfg" because we manage
	 * the memory for it.
	 */

	close(fds[1]);
	fds[1] = -1;
	if (!sqlbox_init(&box, cfg, fds[0], (pid_t)-1)) {
		sqlbox_clear(&box);
		_exit(EXIT_FAILURE);
	}

#if !HAVE_ARC4RANDOM
	srandom(getpid());
#endif
#if HAVE_PLEDGE
	if (pledge("stdio rpath cpath "
	           "wpath flock fattr", NULL) == -1) {
		sqlbox_warn(cfg, "pledge");
		_exit(EXIT_FAILURE);
	}
#endif

	if (!(rc = sqlbox_main_loop(&box)))
		sqlbox_warnx(cfg, "sqlbox_main_loop");

	sqlbox_clear(&box);
	_exit(rc ? EXIT_SUCCESS : EXIT_FAILURE);
}

struct sqlbox *
sqlbox_alloc(struct sqlbox_cfg *cfg)
{
	int	 	 fl = SOCK_STREAM;
	int		 fd[2];
	struct sqlbox	*p;

#if HAVE_SOCK_NONBLOCK
	fl |= SOCK_NONBLOCK;
#endif
	if (socketpair(AF_UNIX, fl, 0, fd) == -1) {
		sqlbox_warn(cfg, "socketpair");
		return NULL;
	}

#if !HAVE_SOCK_NONBLOCK
	if ((fl = fcntl(fd[0], F_GETFL, 0)) == -1 ||
	    fcntl(fd[0], F_SETFL, fl | O_NONBLOCK) == -1 ||
	    (fl = fcntl(fd[1], F_GETFL, 0)) == -1 ||
	    fctl(fd[1], F_SETFL, fl | O_NONBLOCK) == -1) {
		sqlbox_warn(cfg, "fcntl");
		close(fd[0]);
		close(fd[1]);
		return NULL;
	}
#endif
	if ((p = sqlbox_alloc_fd(cfg, fd)) == NULL) {
		sqlbox_warnx(cfg, "sqlbox_alloc_fd");
		if (fd[0] != -1)
			close(fd[0]);
		if (fd[1] != -1)
			close(fd[1]);
	}

	return p;
}
