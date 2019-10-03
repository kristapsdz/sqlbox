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
#ifndef EXTERN_H
#define EXTERN_H

/*
 * This is an important value.
 * It's the size of the "baseline" frame for transferring data and
 * operations.
 * It should be less than the block size for socketpair() but big enough
 * to hold an average payload of data (parameters to bind, results).
 */
#define	SQLBOX_FRAME	1024

enum	sqlbox_op {
	SQLBOX_OP_CLOSE,
	SQLBOX_OP_EXEC_ASYNC,
	SQLBOX_OP_EXEC_SYNC,
	SQLBOX_OP_FINAL,
	SQLBOX_OP_LASTID,
	SQLBOX_OP_OPEN_ASYNC,
	SQLBOX_OP_OPEN_SYNC,
	SQLBOX_OP_PING,
	SQLBOX_OP_PREPARE_BIND_ASYNC,
	SQLBOX_OP_PREPARE_BIND_SYNC,
	SQLBOX_OP_REBIND,
	SQLBOX_OP_ROLE,
	SQLBOX_OP_STEP,
	SQLBOX_OP_TRANS_CLOSE,
	SQLBOX_OP_TRANS_OPEN,
	SQLBOX_OP__MAX
};

/*
 * When the client calls sqlbox_step(3), zero or more results may be
 * transferred from the server.
 * We hold these in these buffers.
 */
struct	sqlbox_res {
	char			*buf; /* backing buffer */
	size_t			 bufsz; /* length of buffer */
	ssize_t			 psz;
	struct sqlbox_parmset	 set; /* parsed values */
};

/*
 * A statement.
 */
struct	sqlbox_stmt {
	sqlite3_stmt		*stmt; /* statement */
	size_t			 idx; /* statement idx */
	size_t			 id; /* statement identifier */
	const struct sqlbox_pstmt *pstmt; /* prepared statement */
	struct sqlbox_db	*db; /* source */
	struct sqlbox_res	 res; /* results, if any */
	TAILQ_ENTRY(sqlbox_stmt) entries; /* per-database */
	TAILQ_ENTRY(sqlbox_stmt) gentries; /* global */
};

TAILQ_HEAD(sqlbox_stmtq, sqlbox_stmt);

/*
 * A database connection.
 * There can be any number of these simultaneously in existence.
 * Each connection contains a set of possibly-open statements.
 */
struct	sqlbox_db {
	sqlite3			*db; /* database or NULL */
	struct sqlbox_stmtq	 stmtq; /* used list */
	size_t			 id; /* source identifier */
	size_t			 idx; /* source idx */
	size_t		 	 trans; /* if >0, exp. transaction */
	const struct sqlbox_src	*src; /* source */
	TAILQ_ENTRY(sqlbox_db)	 entries;
};

TAILQ_HEAD(sqlbox_dbq, sqlbox_db);

struct	sqlbox {
	struct sqlbox_cfg 	 cfg; /* configuration */
	size_t			 role; /* current role */
	struct sqlbox_dbq	 dbq; /* all databases */
	struct sqlbox_stmtq	 stmtq; /* all statements */
	int		  	 fd; /* comm channel or -1 */
	size_t			 lastid; /* last db id */
	pid_t		  	 pid; /* child or (pid_t)-1 */
};

void	 sqlbox_sleep(size_t);
struct sqlbox_db *sqlbox_db_find(struct sqlbox *, size_t);
struct sqlbox_stmt *sqlbox_stmt_find(struct sqlbox *, size_t);
void	 sqlbox_warn(const struct sqlbox_cfg *, const char *, ...)
		__attribute__((format(printf, 2, 3)));
void	 sqlbox_warnx(const struct sqlbox_cfg *, const char *, ...)
		__attribute__((format(printf, 2, 3)));
void	 sqlbox_debug(const struct sqlbox_cfg *, const char *, ...)
		__attribute__((format(printf, 2, 3)));
int	 sqlbox_main_loop(struct sqlbox *);

int	 sqlbox_read(struct sqlbox *, char *, size_t);
int	 sqlbox_read_frame(struct sqlbox *, char **, size_t *, const char **, size_t *);
int	 sqlbox_write(struct sqlbox *, const char *, size_t);
int	 sqlbox_write_frame(struct sqlbox *,
		enum sqlbox_op, const char *, size_t);

int	 sqlbox_parm_pack(struct sqlbox *, size_t, const struct sqlbox_parm *, char **, size_t *, size_t *);
int	 sqlbox_parm_unpack(struct sqlbox *, struct sqlbox_parm **, ssize_t *, const char *, size_t);

int	 sqlbox_op_close(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_exec_async(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_exec_sync(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_finalise(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_lastid(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_open_async(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_open_sync(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_ping(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_prepare_bind_async(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_prepare_bind_sync(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_rebind(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_role(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_step(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_trans_close(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_trans_open(struct sqlbox *, const char *, size_t);

void	 sqlbox_stmt_free(struct sqlbox_stmt *);

#endif /* !EXTERN_H */
