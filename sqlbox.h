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
#ifndef SQLBOX_H
#define SQLBOX_H

#if !defined(__BEGIN_DECLS)
#  ifdef __cplusplus
#  define       __BEGIN_DECLS           extern "C" {
#  else
#  define       __BEGIN_DECLS
#  endif
#endif
#if !defined(__END_DECLS)
#  ifdef __cplusplus
#  define       __END_DECLS             }
#  else
#  define       __END_DECLS
#  endif
#endif

/*
 * Major version.
 */
#define	SQLBOX_VMAJOR	0

/*
 * Minor version.
 */
#define	SQLBOX_VMINOR	3

/*
 * Build version.
 */
#define	SQLBOX_VBUILD	5

/*
 * Stringification of version major, minor, and build.
 * I have on idea if this is necessary.
 */
#define NAME(s) NAME0(s)
#define NAME0(s) #s
#define NAME2(x,y,z) x ## . ## y ## . ## z
#define NAME1(x,y,z) NAME2(x,y,z)

/*
 * Version string of major.minor.build (as a literal string).
 */
#define	SQLBOX_VERSION \
	NAME(NAME1(SQLBOX_VMAJOR,SQLBOX_VMINOR,SQLBOX_VBUILD))

/*
 * Integral stamp of version.
 * Guaranteed to be increasing with build, minor, and major.
 * (Assumes build and minor never go over 100.)
 */
#define	SQLBOX_VSTAMP \
	((SQLBOX_VBUILD+1) + \
	 (SQLBOX_VMINOR+1)*100 + \
	 (SQLBOX_VMAJOR+1)*10000)

struct	sqlbox_role {
	size_t	*roles; /* roles we can access */
	size_t	 rolesz;  /* length of roles */
	size_t	*stmts; /* statements we can call */
	size_t	 stmtsz; /* length of stmts */
	size_t	*srcs; /* databases we can open/close */
	size_t	 srcsz; /* length of srcs */
};

struct	sqlbox_roles {
	struct sqlbox_role	*roles; /* all roles or NULL */
	size_t			 rolesz; /* no. roles or 0 */
	size_t			 defrole; /* default role or 0 */
};

/*
 * A prepared statement.
 * Right now this is just the text, but will eventually include return
 * and paramter types.
 */
struct	sqlbox_pstmt {
	char	*stmt; /* prepared statement */
};

/*
 * Set of all prepared statements.
 */
struct	sqlbox_pstmts {
	struct sqlbox_pstmt 	*stmts; /* all statements */
	size_t		 	 stmtsz; /* no. statements or 0 */
};

/*
 * A database source.
 */
struct	sqlbox_src {
	char		*fname; /* path to sqlite3 db */
#define	SQLBOX_SRC_RO	 0 /* open read-only */
#define	SQLBOX_SRC_RW	 1 /* open read-write */
#define	SQLBOX_SRC_RWC	 2 /* read-write-create */
	int		 mode; /* open mode */
};

/*
 * Set of all database sources.
 */
struct	sqlbox_srcs {
	struct sqlbox_src	*srcs; /* all sources or NULL */
	size_t			 srcsz; /* no. sources or 0 */
};

/*
 * How we pass messages to the frontend.
 * We first check if func is non-NULL, then func_short.
 * The reason for func_short is that it's used for things like warnx
 * that don't take any additional parameters.
 */
struct	sqlbox_msg {
	void	(*func)(const char *, void *);
	void	(*func_short)(const char *, ...);
	void	 *dat; /* passed to func */
};

struct	sqlbox_cfg {
	struct sqlbox_pstmts	stmts; /* statements */
	struct sqlbox_roles	roles; /* RBAC roles */
	struct sqlbox_srcs	srcs; /* databases */
	struct sqlbox_msg	msg; /* message system */
};

enum	sqlbox_boundt {
	SQLBOX_BOUND_INT,
	SQLBOX_BOUND_NULL,
	SQLBOX_BOUND_STRING,
};

struct	sqlbox_bound {
	union {
		int64_t		 iparm;
		const char	*sparm;
	};
	enum sqlbox_boundt	 type;
};

struct	sqlbox;

__BEGIN_DECLS

struct sqlbox	*sqlbox_alloc(struct sqlbox_cfg *);
void		 sqlbox_free(struct sqlbox *);

int		 sqlbox_close(struct sqlbox *, size_t);
size_t		 sqlbox_open(struct sqlbox *, size_t);
int		 sqlbox_ping(struct sqlbox *);
int	 	 sqlbox_role(struct sqlbox *, size_t);
size_t		 sqlbox_prepare_bind(struct sqlbox *, size_t,
			size_t, size_t, const struct sqlbox_bound *);
int		 sqlbox_finalise(struct sqlbox *, size_t);
int		 sqlbox_step(struct sqlbox *, size_t);

#if 0
enum ksqlc	 ksql_bind_blob(struct ksqlstmt *, 
			size_t, const void *, size_t);
enum ksqlc	 ksql_bind_double(struct ksqlstmt *, size_t, double);
enum ksqlc	 ksql_bind_int(struct ksqlstmt *, size_t, int64_t);
enum ksqlc	 ksql_bind_null(struct ksqlstmt *, size_t);
enum ksqlc	 ksql_bind_str(struct ksqlstmt *, size_t, const char *);
enum ksqlc	 ksql_bind_zblob(struct ksqlstmt *, size_t, size_t);
enum ksqlc	 ksql_exec(struct ksql *, const char *, size_t);
enum ksqlc	 ksql_lastid(struct ksql *, int64_t *);
enum ksqlc	 ksql_result_blob(struct ksqlstmt *, const void **, size_t *, size_t);
enum ksqlc	 ksql_result_blob_alloc(struct ksqlstmt *, void **, size_t *, size_t);
enum ksqlc	 ksql_result_bytes(struct ksqlstmt *, size_t *, size_t);
enum ksqlc	 ksql_result_double(struct ksqlstmt *, double *, size_t);
enum ksqlc	 ksql_result_int(struct ksqlstmt *, int64_t *, size_t);
enum ksqlc	 ksql_result_isnull(struct ksqlstmt *, int *, size_t);
enum ksqlc	 ksql_result_str(struct ksqlstmt *, const char **, size_t);
enum ksqlc	 ksql_result_str_alloc(struct ksqlstmt *, char **, size_t);
void		 ksql_role(struct ksql *, size_t);
enum ksqlc	 ksql_stmt_alloc(struct ksql *, 
			struct ksqlstmt **, const char *, size_t);

enum ksqlc	 ksql_stmt_step(struct ksqlstmt *);
enum ksqlc	 ksql_stmt_cstep(struct ksqlstmt *);
enum ksqlc	 ksql_stmt_reset(struct ksqlstmt *);
enum ksqlc	 ksql_stmt_free(struct ksqlstmt *);

const void	*ksql_stmt_blob(struct ksqlstmt *, size_t)
			__attribute__((deprecated));
size_t		 ksql_stmt_bytes(struct ksqlstmt *, size_t)
			__attribute__((deprecated));
double		 ksql_stmt_double(struct ksqlstmt *, size_t)
			__attribute__((deprecated));
int64_t		 ksql_stmt_int(struct ksqlstmt *, size_t)
			__attribute__((deprecated));
int		 ksql_stmt_isnull(struct ksqlstmt *, size_t)
			__attribute__((deprecated));
const char	*ksql_stmt_str(struct ksqlstmt *, size_t)
			__attribute__((deprecated));

enum ksqlc	 ksql_trans_commit(struct ksql *, size_t);
enum ksqlc	 ksql_trans_exclopen(struct ksql *, size_t);
enum ksqlc	 ksql_trans_open(struct ksql *, size_t);
enum ksqlc	 ksql_trans_rollback(struct ksql *, size_t);
enum ksqlc	 ksql_trans_singleopen(struct ksql *, size_t);

enum ksqlc	 ksql_trace(struct ksql *);
enum ksqlc	 ksql_untrace(struct ksql *);

void		 ksqlitedbmsg(void *, int, int, const char *, const char *);
void		 ksqlitemsg(void *, enum ksqlc, const char *, const char *);

__END_DECLS

#endif
#endif
