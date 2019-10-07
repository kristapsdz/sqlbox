.SUFFIXES: .png .dat

include Makefile.configure

VMAJOR	!= grep 'define	SQLBOX_VMAJOR' sqlbox.h | cut -f3
VMINOR	!= grep 'define	SQLBOX_VMINOR' sqlbox.h | cut -f3
VBUILD	!= grep 'define	SQLBOX_VBUILD' sqlbox.h | cut -f3
VERSION	:= $(VMAJOR).$(VMINOR).$(VBUILD)
#TESTS	 = test-filter-out-float \
#	   test-filter-out-int \
#	   test-filter-out-string
TESTS	 = test-alloc-bad-defrole \
	   test-alloc-bad-role \
	   test-alloc-bad-src \
	   test-alloc-bad-stmt \
	   test-alloc-defrole \
	   test-alloc-empty-stmt \
	   test-alloc-null-source \
	   test-alloc-null-stmt \
	   test-alloc-role \
	   test-alloc-src \
	   test-alloc-stmt \
	   test-close \
	   test-close-bad-id \
	   test-close-bad-role \
	   test-close-bad-zero-id \
	   test-close-twice \
	   test-close-twice-zero-id \
	   test-close-zero-id \
	   test-cstep \
	   test-exec-async-bad-id \
	   test-exec-async-bad-src \
	   test-exec-async-bad-zero-id \
	   test-exec-async-constraint \
	   test-exec-bad-id \
	   test-exec-bad-src \
	   test-exec-bad-zero-id \
	   test-exec-constraint \
	   test-exec-create-insert \
	   test-exec-select \
	   test-exec-zero-id \
	   test-finalise \
	   test-finalise-bad-stmt \
	   test-finalise-bad-zero-id \
	   test-finalise-twice \
	   test-finalise-twice-zero-id \
	   test-finalise-zero-id \
	   test-lastid-bad-src \
	   test-lastid-bad-zero-id \
	   test-lastid-insert-explicit \
	   test-lastid-insert-implicit \
	   test-lastid-noinserts \
	   test-lastid-zero-id \
	   test-open-async-bad-src \
	   test-open-async-memory \
	   test-open-bad-not-exist \
	   test-open-bad-role \
	   test-open-bad-src \
	   test-open-file-create \
	   test-open-memory \
	   test-open-memory-role \
	   test-open-not-found \
	   test-open-twice \
	   test-ping \
	   test-ping-fail \
	   test-prepare_bind-async \
	   test-prepare_bind-async-bad-src \
	   test-prepare_bind-bad-src \
	   test-prepare_bind-bad-stmt \
	   test-prepare_bind-bad-zero-id \
	   test-prepare_bind-noparms \
	   test-prepare_bind-zero-id \
	   test-rebind \
	   test-rebind-after-finalise \
	   test-rebind-bad-id \
	   test-rebind-bad-zero-id \
	   test-rebind-zero-id \
	   test-role-bad-role \
	   test-role-bad-transition \
	   test-role-norole \
	   test-role-transition \
	   test-role-transition-self \
	   test-step-bad-stmt \
	   test-step-double-exec \
	   test-step-constraint \
	   test-step-constraint-code \
	   test-step-create \
	   test-step-create-insert-select \
	   test-step-create-insert-selectmulti \
	   test-step-create-insert-selectmulticol \
	   test-step-float-explicit-length \
	   test-step-float-many \
	   test-step-float-maxvalue \
	   test-step-int-explicit-length \
	   test-step-int-many \
	   test-step-int-maxvalue \
	   test-step-int-maxnegvalue \
	   test-step-string-explicit-length \
	   test-step-string-implicit-length \
	   test-step-string-missing-nul \
	   test-step-string-long \
	   test-step-string-long-implicit \
	   test-step-string-long-multi \
	   test-step-zero-id \
	   test-trans-close-bad-id \
	   test-trans-close-bad-src \
	   test-trans-close-reopen \
	   test-trans-close-twice \
	   test-trans-open \
	   test-trans-open-zero-id \
	   test-trans-open-bad-close \
	   test-trans-open-bad-id \
	   test-trans-open-bad-src \
	   test-trans-open-bad-zero-id \
	   test-trans-open-nested \
	   test-trans-open-same-id-diff-src
OBJS	 = alloc.o \
	   close.o \
	   exec.o \
	   finalise.o \
	   io.o \
	   lastid.o \
	   main.o \
	   open.o \
	   parm.o \
	   ping.o \
	   prepare_bind.o \
	   rebind.o \
	   role.o \
	   sqlite3.o \
	   step.o \
	   transaction.o \
	   warn.o
PERFPNGS = perf-full-cycle.png \
	   perf-prep-insert-final.png \
	   perf-rebind.png
PERFS	 = perf-full-cycle-ksql \
	   perf-full-cycle-sqlbox \
	   perf-full-cycle-sqlite3 \
	   perf-prep-insert-final-ksql \
	   perf-prep-insert-final-sqlbox \
	   perf-prep-insert-final-sqlite3 \
	   perf-rebind-ksql \
	   perf-rebind-sqlbox \
	   perf-rebind-sqlite3
LDFLAGS	+= -L/usr/local/lib
CPPFLAGS += -I/usr/local/include

all: libsqlbox.a

perf: $(PERFPNGS)

libsqlbox.a: $(OBJS) compats.o
	$(AR) rs $@ $(OBJS) compats.o

compats.o $(OBJS) $(TESTS): config.h

$(OBJS): sqlbox.h extern.h

$(TESTS): libsqlbox.a regress/regress.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ regress/$*.c compats.o $(LDFLAGS) libsqlbox.a -lsqlite3 -lm

$(PERFPNGS): perf.gnuplot

.for test in $(TESTS)
${test}: regress/${test}.c
.endfor

.for perf in perf-prep-insert-final perf-rebind perf-full-cycle
${perf}.dat: ${perf}-ksql ${perf}-sqlbox ${perf}-sqlite3 perf.sh
	sh ./perf.sh ${perf} >$@

${perf}-ksql: perf/${perf}-ksql.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ perf/${perf}-ksql.c $(LDFLAGS) -lksql -lsqlite3 -lm

${perf}-sqlbox: perf/${perf}-sqlbox.c libsqlbox.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ perf/${perf}-sqlbox.c $(LDFLAGS) libsqlbox.a -lsqlite3 -lm

${perf}-sqlite3: perf/${perf}-sqlite3.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ perf/${perf}-sqlite3.c $(LDFLAGS) -lsqlite3 -lm
.endfor

clean:
	rm -f libsqlbox.a compats.o $(OBJS) $(TESTS) $(PERFS)
	rm -f $(PERFS) $(PERFPNGS) *.dat

distclean: clean
	rm -f config.h config.log Makefile.configure

regress: $(TESTS)
	@for f in $(TESTS) ; do \
		set +e ; \
		echo -n "$$f... "; \
		./$$f 2>/dev/null; \
		if [ $$? -ne 0 ]; then \
			echo "\033[31mfail\033[0m"; \
			break ; \
		else \
			echo "\033[32mok\033[0m"; \
		fi; \
		set -e ; \
	done

regress_all: $(TESTS)
	@for f in $(TESTS) ; do \
		set +e ; \
		echo -n "$$f... "; \
		./$$f 2>/dev/null; \
		if [ $$? -ne 0 ]; then \
			echo "\033[31mfail\033[0m"; \
		else \
			echo "\033[32mok\033[0m"; \
		fi; \
		set -e ; \
	done

.dat.png:
	gnuplot -c perf.gnuplot $< $@
