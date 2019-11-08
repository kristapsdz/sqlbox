.SUFFIXES: .png .dat .dot .svg .3.xml .3 .3.html

include Makefile.configure

VMAJOR		!= grep 'define	SQLBOX_VMAJOR' sqlbox.h | cut -f3
VMINOR		!= grep 'define	SQLBOX_VMINOR' sqlbox.h | cut -f3
VBUILD		!= grep 'define	SQLBOX_VBUILD' sqlbox.h | cut -f3
VERSION		:= $(VMAJOR).$(VMINOR).$(VBUILD)
TESTS		 = test-alloc-bad-defrole \
		   test-alloc-bad-filt-stmt \
		   test-alloc-bad-role \
		   test-alloc-bad-src \
		   test-alloc-bad-stmt \
		   test-alloc-defrole \
		   test-alloc-empty-stmt \
		   test-alloc-null-filt \
		   test-alloc-null-source \
		   test-alloc-null-stmt \
		   test-alloc-role \
		   test-alloc-src \
		   test-alloc-stmt \
		   test-cexec \
		   test-cexec-noparms \
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
		   test-exec-constraint-noparms \
		   test-exec-create-insert \
		   test-exec-create-insert-noparms \
		   test-exec-select \
		   test-exec-zero-id \
		   test-filter-gen-out-fail \
		   test-filter-gen-out-float \
		   test-filter-gen-out-int \
		   test-filter-gen-out-string \
		   test-finalise \
		   test-finalise-bad-stmt \
		   test-finalise-bad-zero-id \
		   test-finalise-twice \
		   test-finalise-twice-zero-id \
		   test-finalise-zero-id \
		   test-hier-bad-defrole \
		   test-hier-child-loop \
		   test-hier-child-readd \
		   test-hier-empty \
		   test-hier-empty-gen \
		   test-hier-invalid-child \
		   test-hier-invalid-parent \
		   test-hier-self-reference \
		   test-hier-simple \
		   test-hier-simple2 \
		   test-hier-simple3 \
		   test-hier-sink-bad-child \
		   test-hier-sink-bad-child-post \
		   test-hier-sink-bad-index \
		   test-hier-sink-bad-parent \
		   test-hier-sink-bad-parent-post \
		   test-hier-sink-empty \
		   test-hier-sink-readd \
		   test-hier-sink-simple \
		   test-hier-start-bad-child \
		   test-hier-start-bad-index \
		   test-hier-start-bad-parent \
		   test-hier-start-empty \
		   test-hier-start-readd \
		   test-hier-start-simple \
		   test-hier-start-sink \
		   test-hier-stmts \
		   test-hier-stmts-readd \
		   test-hier-stmts-readd2 \
		   test-lastid-bad-src \
		   test-lastid-bad-zero-id \
		   test-lastid-insert-explicit \
		   test-lastid-insert-implicit \
		   test-lastid-noinserts \
		   test-lastid-zero-id \
		   test-msg_set_dat \
		   test-msg_set_dat-null \
		   test-open-async-bad-src \
		   test-open-async-memory \
		   test-open-bad-not-exist \
		   test-open-bad-role \
		   test-open-bad-src \
		   test-open-file-create \
		   test-open-foreignkey \
		   test-open-memory \
		   test-open-memory-role \
		   test-open-nested \
		   test-open-not-found \
		   test-open-twice \
		   test-parm-blob-bad \
		   test-parm-float \
		   test-parm-float-bad \
		   test-parm-float-maxvalues \
		   test-parm-int-bad \
		   test-parm-int \
		   test-parm-int-long \
		   test-parm-int-maxvalues \
		   test-parm-string \
		   test-ping \
		   test-ping-fail \
		   test-prepare_bind-async \
		   test-prepare_bind-async-bad-src \
		   test-prepare_bind-bad-src \
		   test-prepare_bind-bad-stmt \
		   test-prepare_bind-bad-zero-id \
		   test-prepare_bind-nested \
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
		   test-step-float-inf \
		   test-step-float-many \
		   test-step-float-maxvalue \
		   test-step-float-nan \
		   test-step-int-explicit-length \
		   test-step-int-many \
		   test-step-int-maxvalue \
		   test-step-int-maxnegvalue \
		   test-step-multi \
		   test-step-multi-many \
		   test-step-multi-many-twice \
		   test-step-multi-none \
		   test-step-multi-none2 \
		   test-step-multi-none-twice \
		   test-step-multi-twice \
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
OBJS		 = alloc.o \
		   close.o \
		   exec.o \
		   finalise.o \
		   hier.o \
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
MANS		 = man/sqlbox.3 \
		   man/sqlbox_alloc.3 \
		   man/sqlbox_close.3 \
		   man/sqlbox_exec.3 \
		   man/sqlbox_finalise.3 \
		   man/sqlbox_free.3 \
		   man/sqlbox_msg_set_dat.3 \
		   man/sqlbox_open.3 \
		   man/sqlbox_parm_int.3 \
		   man/sqlbox_ping.3 \
		   man/sqlbox_prepare_bind.3 \
		   man/sqlbox_rebind.3 \
		   man/sqlbox_role.3 \
		   man/sqlbox_role_hier_alloc.3 \
		   man/sqlbox_role_hier_child.3 \
		   man/sqlbox_role_hier_free.3 \
		   man/sqlbox_role_hier_gen.3 \
		   man/sqlbox_role_hier_gen_free.3 \
		   man/sqlbox_role_hier_sink.3 \
		   man/sqlbox_role_hier_start.3 \
		   man/sqlbox_role_hier_stmt.3 \
		   man/sqlbox_step.3 \
		   man/sqlbox_trans_commit.3 \
		   man/sqlbox_trans_immediate.3
PERFPNGS	 = perf-full-cycle.png \
		   perf-prep-insert-final.png \
		   perf-rebind.png \
		   perf-select.png \
		   perf-select-multi.png
PERFS		 = perf-full-cycle-ksql \
		   perf-full-cycle-sqlbox \
		   perf-full-cycle-sqlite3 \
		   perf-prep-insert-final-ksql \
		   perf-prep-insert-final-sqlbox \
		   perf-prep-insert-final-sqlite3 \
		   perf-rebind-ksql \
		   perf-rebind-sqlbox \
		   perf-rebind-sqlite3 \
		   perf-select-ksql \
		   perf-select-sqlbox \
		   perf-select-sqlite3 \
		   perf-select-multi-ksql \
		   perf-select-multi-sqlbox \
		   perf-select-multi-sqlite3
# On OpenBSD, you usually need these.
#LDFLAGS	+= -L/usr/local/lib
#CPPFLAGS	+= -I/usr/local/include
# On OpenBSD 6.6 and above, you need this.
#LDADD		+= -lpthread
VGR		 = valgrind
VGROPTS		 = -q --track-origins=yes --leak-check=full \
		   --show-reachable=yes --trace-children=yes \
		   --leak-resolution=high
WWWDIR		 = /var/www/vhosts/kristaps.bsd.lv/htdocs/sqlbox
.for mans in $(MANS)
MANXMLS		+= ${mans}.xml
MANHTMLS	+= ${mans}.html
.endfor

all: libsqlbox.a

allperf: $(PERFS)

www: index.html atom.xml perf index.svg sqlbox.tar.gz.sha512 $(MANXMLS) $(MANHTMLS)

perf: $(PERFPNGS)

sqlbox.tar.gz.sha512: sqlbox.tar.gz
	sha512 sqlbox.tar.gz >$@

sqlbox.tar.gz: Makefile
	rm -rf .dist
	mkdir -p .dist/sqlbox-$(VERSION)
	mkdir -p .dist/sqlbox-$(VERSION)/{regress,man,perf}
	install -m 0644 Makefile *.c extern.h sqlbox.h .dist/sqlbox-$(VERSION)
	install -m 0644 regress/*.[ch] .dist/sqlbox-$(VERSION)/regress
	install -m 0644 perf/*.[ch] .dist/sqlbox-$(VERSION)/perf
	install -m 0644 man/*.3 .dist/sqlbox-$(VERSION)/man
	install -m 0755 configure .dist/sqlbox-$(VERSION)
	( cd .dist/ && tar zcf ../$@ ./ )
	rm -rf .dist/

installwww:
	mkdir -p $(WWWDIR)/snapshots
	mkdir -p $(WWWDIR)/man
	install -m 0444 sqlbox.tar.gz $(WWWDIR)/snapshots/sqlbox-$(VERSION).tar.gz
	install -m 0444 sqlbox.tar.gz.sha512 $(WWWDIR)/snapshots/sqlbox-$(VERSION).tar.gz.sha512
	install -m 0444 sqlbox.tar.gz sqlbox.tar.gz.sha512 $(WWWDIR)/snapshots
	install -m 0444 atom.xml *.html *.png *.svg *.css $(WWWDIR)
	install -m 0444 $(MANHTMLS) $(WWWDIR)/man

distcheck: sqlbox.tar.gz.sha512
	mandoc -Tlint -Wwarning $(MANS)
	newest=`grep "<h1>" versions.xml | head -n1 | sed 's![ 	]*!!g'` ; \
	       [ "$$newest" == "<h1>$(VERSION)</h1>" ] || \
		{ echo "Version $(VERSION) not newest in versions.xml" 1>&2 ; exit 1 ; }
	rm -rf .distcheck
	sha512 -C sqlbox.tar.gz.sha512 sqlbox.tar.gz
	mkdir -p .distcheck
	tar -zvxpf sqlbox.tar.gz -C .distcheck
	( cd .distcheck/sqlbox-$(VERSION) && ./configure PREFIX=prefix \
		CPPFLAGS="$(CPPFLAGS)" LDFLAGS="$(LDFLAGS)" LDADD="$(LDADD)" )
	( cd .distcheck/sqlbox-$(VERSION) && make )
	( cd .distcheck/sqlbox-$(VERSION) && make install )
	rm -rf .distcheck

install: all
	mkdir -p $(DESTDIR)$(LIBDIR)
	mkdir -p $(DESTDIR)$(INCLUDEDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man3
	$(INSTALL_LIB) libsqlbox.a $(DESTDIR)$(LIBDIR)
	$(INSTALL_DATA) sqlbox.h $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL_DATA) man/*.3 $(DESTDIR)$(MANDIR)/man3

libsqlbox.a: $(OBJS) compats.o
	$(AR) rs $@ $(OBJS) compats.o

compats.o $(OBJS) $(TESTS): config.h

$(OBJS): sqlbox.h extern.h

$(TESTS): libsqlbox.a regress/regress.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ regress/$*.c compats.o $(LDFLAGS) libsqlbox.a -lsqlite3 -lm $(LDADD)

$(PERFPNGS): perf.gnuplot

.for test in $(TESTS)
${test}: regress/${test}.c
.endfor

.for perf in perf-prep-insert-final perf-rebind perf-full-cycle perf-select-multi perf-select
#
# Reenable this if you want to regenerate the performance data files.
# It will take... quite some time.
#
#${perf}.dat: ${perf}-ksql ${perf}-sqlbox ${perf}-sqlite3 perf.sh
#	sh ./perf.sh ${perf} >$@

${perf}-ksql: perf/${perf}-ksql.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ perf/${perf}-ksql.c $(LDFLAGS) -lksql -lsqlite3 -lm $(LDADD)

${perf}-sqlbox: perf/${perf}-sqlbox.c libsqlbox.a
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ perf/${perf}-sqlbox.c $(LDFLAGS) libsqlbox.a -lsqlite3 -lm $(LDADD)

${perf}-sqlite3: perf/${perf}-sqlite3.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ perf/${perf}-sqlite3.c $(LDFLAGS) -lsqlite3 -lm $(LDADD)
.endfor

clean:
	rm -f libsqlbox.a compats.o $(OBJS) $(TESTS) $(PERFS)
	rm -f $(PERFS) $(PERFPNGS) index.html index.svg sqlbox.tar.gz sqlbox.tar.gz.sha512 atom.xml
	rm -f $(MANXMLS) $(MANHTMLS)

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

valgrind: $(TESTS) $(PERFS)
	@for f in $(TESTS); do \
		set +e ; \
		echo "$$f... \033[33mrunning\033[0m"; \
		$(VGR) $(VGROPTS) ./$$f 2>&1 | sed -e '/^[^=]/d' -e 's!^==\(.*\)!\x1B[31m==\1\x1B[0m!' ; \
		if [ $$? -ne 0 ]; then \
			echo "$$f... \033[31mfail\033[0m"; \
		else \
			echo "$$f... \033[32mok\033[0m"; \
		fi; \
		set -e ; \
	done
	@for f in $(PERFS); do \
		set +e ; \
		echo "$$f... \033[33mrunning\033[0m"; \
		$(VGR) $(VGROPTS) ./$$f -n 1000 2>&1 | sed -e '/^[^=]/d' -e 's!^==\(.*\)!\x1B[31m==\1\x1B[0m!' ; \
		if [ $$? -ne 0 ]; then \
			echo "$$f... \033[31mfail\033[0m"; \
		else \
			echo "$$f... \033[32mok\033[0m"; \
		fi; \
		set -e ; \
	done

.dat.png:
	gnuplot -c perf.gnuplot $< $@

index.html: index.xml versions.xml $(MANXMLS)
	sblg -t index.xml -o $@ versions.xml $(MANXMLS)
	
.dot.svg:
	dot -Tsvg $< | xsltproc --novalid notugly.xsl - >$@

.3.3.html:
	mandoc -Ostyle=https://bsd.lv/css/mandoc.css -Thtml $< >$@

.3.3.xml:
	( echo '<article data-sblg-article="1" data-sblg-tags="manpages">' ; \
	  echo "<h1>`grep -h '^\.Nm ' $< | head -n1 | cut -c 5- | sed 's![ ]*,$$!!'`</h1>" ; \
	  echo '<aside>' ; \
	  grep -h '^\.Nd ' $< | cut -c 5- ; \
	  echo '</aside>' ; \
	  echo '</article>' ; \
	) >$@

atom.xml: versions.xml atom-template.xml
	sblg -s date -o $@ -a versions.xml
