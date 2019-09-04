#ifndef EXTERN_H
#define EXTERN_H

enum	sqlbox_op {
	SQLBOX_OP_CLOSE,
	SQLBOX_OP_OPEN,
	SQLBOX_OP_PING,
	SQLBOX_OP_PREPARE_BIND,
	SQLBOX_OP_ROLE,
	SQLBOX_OP_STEP,
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
	TAILQ_ENTRY(sqlbox_stmt) entries;
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
	const struct sqlbox_src	*src; /* source */
	TAILQ_ENTRY(sqlbox_db)	 entries;
};

TAILQ_HEAD(sqlbox_dbq, sqlbox_db);

struct	sqlbox {
	struct sqlbox_cfg 	 cfg; /* configuration */
	size_t			 role; /* current role */
	struct sqlbox_stmtq	 stmtq; /* free list */
	struct sqlbox_dbq	 dbq /* all databases */;
	int		  	 fd; /* comm channel or -1 */
	size_t			 lastid; /* last db id */
	pid_t		  	 pid; /* child or (pid_t)-1 */
	char			*buf; /* send buffer */
	size_t		  	 bufsz; /* send buffer size */
};

void	 sqlbox_sleep(size_t);
struct sqlbox_db *sqlbox_db_find(struct sqlbox *, size_t);
void	 sqlbox_warn(const struct sqlbox_cfg *, const char *, ...)
		__attribute__((format(printf, 2, 3)));
void	 sqlbox_warnx(const struct sqlbox_cfg *, const char *, ...)
		__attribute__((format(printf, 2, 3)));
void	 sqlbox_debug(const struct sqlbox_cfg *, const char *, ...)
		__attribute__((format(printf, 2, 3)));
int	 sqlbox_main_loop(struct sqlbox *);

int	 sqlbox_read(struct sqlbox *, char *, size_t);
int	 sqlbox_read_frame(struct sqlbox *, const char **, size_t *);
int	 sqlbox_write(struct sqlbox *, const char *, size_t);
int	 sqlbox_write_frame(struct sqlbox *,
		enum sqlbox_op, const char *, size_t);

int	 sqlbox_bound_pack(struct sqlbox *, size_t, const struct sqlbox_bound *, char **, size_t *, size_t *);
int	 sqlbox_bound_unpack(struct sqlbox *, size_t *, struct sqlbox_bound **, const char *, size_t);

int	 sqlbox_op_close(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_open(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_ping(struct sqlbox *, const char *, size_t);
int	 sqlbox_op_prepare_bind(struct sqlbox *, const char *, size_t);

#endif /* !EXTERN_H */
