#ifndef EXTERN_H
#define EXTERN_H

enum	sqlbox_op {
	SQLBOX_OP_ROLE = 0,
	SQLBOX_OP_OPEN,
	SQLBOX_OP_PING,
};

/*
 * A statement.
 */
struct	sqlbox_stmt {
	sqlite3_stmt		*stmt; /* statement */
	size_t			 idx;
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
	size_t			 idx; /* source index */
	TAILQ_ENTRY(sqlbox_db)	 entries;
};

TAILQ_HEAD(sqlbox_dbq, sqlbox_db);

struct	sqlbox {
	struct sqlbox_cfg 	 cfg; /* configuration */
	size_t			 role; /* current role */
	struct sqlbox_stmtq	 stmtq; /* free list */
	struct sqlbox_dbq	 dbq /* all databases */;
	int		  	 fd; /* comm channel or -1 */
	pid_t		  	 pid; /* child or (pid_t)-1 */
};

void	 sqlbox_warn(const struct sqlbox_cfg *, const char *, ...);
void	 sqlbox_warnx(const struct sqlbox_cfg *, const char *, ...);
int	 sqlbox_main_loop(struct sqlbox *);

#endif /* !EXTERN_H */
