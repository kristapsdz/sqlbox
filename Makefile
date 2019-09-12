include Makefile.configure

VMAJOR	!= grep 'define	SQLBOX_VMAJOR' sqlbox.h | cut -f3
VMINOR	!= grep 'define	SQLBOX_VMINOR' sqlbox.h | cut -f3
VBUILD	!= grep 'define	SQLBOX_VBUILD' sqlbox.h | cut -f3
VERSION	:= $(VMAJOR).$(VMINOR).$(VBUILD)
TESTS	 = test-alloc-bad-defrole \
	    test-alloc-bad-role \
	    test-alloc-bad-src \
	    test-alloc-bad-stmt \
	    test-alloc-defrole \
	    test-alloc-null-source \
	    test-alloc-role \
	    test-alloc-src \
	    test-alloc-stmt \
	    test-close \
	    test-close-bad-id \
	    test-close-bad-role \
	    test-close-twice \
	    test-close-zero-id \
	    test-finalise \
	    test-finalise-bad-stmt \
	    test-finalise-twice \
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
	    test-prepare_bind-bad-src \
	    test-prepare_bind-bad-stmt \
	    test-prepare_bind-noparms \
	    test-role-bad-role \
	    test-role-bad-transition \
	    test-role-norole \
	    test-role-transition \
	    test-role-transition-self \
	    test-step-create \
	    test-step-create-insert-select
OBJS	  = alloc.o \
	    close.o \
	    finalise.o \
	    io.o \
	    main.o \
	    open.o \
	    parm.o \
	    ping.o \
	    prepare_bind.o \
	    role.o \
	    step.o \
	    warn.o
LDFLAGS	 += -L/usr/local/lib
CPPFLAGS += -I/usr/local/include -DDEBUG

all: libsqlbox.a

libsqlbox.a: $(OBJS) compats.o
	$(AR) rs $@ $(OBJS) compats.o

compats.o $(OBJS) $(TESTS): config.h

$(OBJS): sqlbox.h extern.h

$(TESTS): libsqlbox.a regress/regress.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ regress/$*.c compats.o $(LDFLAGS) libsqlbox.a -lsqlite3

.for test in $(TESTS)
${test}: regress/${test}.c
.endfor

clean:
	rm -f libsqlbox.a compats.o $(OBJS) $(TESTS)

distclean: clean
	rm -f config.h config.log Makefile.configure

regress: $(TESTS)
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
