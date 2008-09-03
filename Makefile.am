AUTOMAKE_OPTIONS = foreign

SUBDIRS = src

noinst_HEADERS = src/qore-mysql.h \
	src/qore-mysql-module.h

EXTRA_DIST = COPYING ChangeLog AUTHORS README \
	test/db-test.q \
	qore-mysql-module.spec

libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck
