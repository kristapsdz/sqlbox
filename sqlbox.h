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

enum	sqlbox_parmt {
	SQLBOX_PARM_BLOB = 0,
	SQLBOX_PARM_FLOAT = 1,
	SQLBOX_PARM_INT = 2,
	SQLBOX_PARM_NULL = 3,
	SQLBOX_PARM_STRING = 4,
};

/*
 * A prepared statement.
 * This is in a structure as future-proof of adding more information for
 * the prepared statement (types, etc.).
 */
struct	sqlbox_pstmt {
	char			*stmt; /* prepared statement */
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

/*
 * A single column either for setting parameters to bind or reading
 * results from stepping.
 * Data is all associated with a type and a length.
 * When binding, strings (sparm and SQLBOX_PARM_STRING) must have their
 * size (total byte length) set to zero to be auto-computed or otherwise
 * *must include* the NUL terminating character.
 * Binary data must have the size set.
 * Floats and integers ignore the size.
 */
struct	sqlbox_parm {
	union {
		double		 fparm; /* floats */
		int64_t		 iparm; /* integers */
		const char	*sparm; /* NUL-terminated UTF-8 */
		const void	*bparm; /* binary data */
	};
	enum sqlbox_parmt	 type;
	size_t			 sz; /* data length (bytes) */
};

enum	sqlbox_filtt {
	SQLBOX_FILT_IN,
	SQLBOX_FILT_OUT
};

/*
 * A filter for a particular prepared statement's return value.
 * The "filt" function accepts actual data from the database and in
 * some way filters it.
 * If it needs to allocate memory on the way, it's given a pointer into
 * which it can stash the data and a "free" function to free it up.
 */
struct	sqlbox_filt {
	size_t		  col; /* applicable columns */
	size_t		  stmt; /* applicable statement */
	enum sqlbox_filtt type; /* data in or data out */
	int 		(*filt)(struct sqlbox_parm *, void **);
	void 		(*free)(void *);
};

/*
 * A list of statement/return index scrambling functions.
 */
struct	sqlbox_filts {
	struct sqlbox_filt	*filts;
	size_t		 	 filtsz;
};

/*
 * Contains all data required for an sqlbox configuration.
 */
struct	sqlbox_cfg {
	struct sqlbox_pstmts	stmts; /* statements */
	struct sqlbox_roles	roles; /* RBAC roles */
	struct sqlbox_srcs	srcs; /* databases */
	struct sqlbox_filts	filts; /* filters */
	struct sqlbox_msg	msg; /* message system */
};

enum	sqlbox_code {
	SQLBOX_CODE_OK = 0, /* success */
	SQLBOX_CODE_CONSTRAINT = 1, /* constraint violation */
	SQLBOX_CODE_ERROR /* never returned */
};

/*
 * A possibly-empty result ("row") from stepping a prepared statement.
 * A result with zero columns indicates no more data will follow by
 * stepping the prepared statement.
 */
struct	sqlbox_parmset {
	struct sqlbox_parm 	*ps; /* columns of the resulting row */
	size_t		 	 psz; /* number of columns or zero */
	enum sqlbox_code	 code; /* return type */
};

struct	sqlbox;

__BEGIN_DECLS

struct sqlbox	*sqlbox_alloc(struct sqlbox_cfg *);
int		 sqlbox_close(struct sqlbox *, size_t);
const struct sqlbox_parmset
		*sqlbox_cstep(struct sqlbox *, size_t);
int		 sqlbox_exec_async(struct sqlbox *, size_t, size_t, 
			size_t, const struct sqlbox_parm *);
enum sqlbox_code sqlbox_exec(struct sqlbox *, size_t, size_t, 
			size_t, const struct sqlbox_parm *);
int		 sqlbox_finalise(struct sqlbox *, size_t);
void		 sqlbox_free(struct sqlbox *);
int		 sqlbox_lastid(struct sqlbox *, size_t, int64_t *);
size_t		 sqlbox_open(struct sqlbox *, size_t);
int		 sqlbox_open_async(struct sqlbox *, size_t);
int		 sqlbox_ping(struct sqlbox *);
size_t		 sqlbox_prepare_bind(struct sqlbox *, size_t,
			size_t, size_t, const struct sqlbox_parm *);
int		 sqlbox_prepare_bind_async(struct sqlbox *, size_t,
			size_t, size_t, const struct sqlbox_parm *);
int		 sqlbox_rebind(struct sqlbox *, size_t,
			size_t, const struct sqlbox_parm *);
int	 	 sqlbox_role(struct sqlbox *, size_t);
const struct sqlbox_parmset
		*sqlbox_step(struct sqlbox *, size_t);
int		 sqlbox_trans_immediate(struct sqlbox *, size_t, size_t);
int		 sqlbox_trans_deferred(struct sqlbox *, size_t, size_t);
int		 sqlbox_trans_exclusive(struct sqlbox *, size_t, size_t);
int		 sqlbox_trans_commit(struct sqlbox *, size_t, size_t);
int		 sqlbox_trans_rollback(struct sqlbox *, size_t, size_t);

#if 0
enum ksqlc	 ksql_bind_zblob(struct ksqlstmt *, size_t, size_t);
enum ksqlc	 ksql_exec(struct ksql *, const char *, size_t);

enum ksqlc	 ksql_stmt_reset(struct ksqlstmt *);

enum ksqlc	 ksql_trace(struct ksql *);
enum ksqlc	 ksql_untrace(struct ksql *);

__END_DECLS

#endif
#endif
