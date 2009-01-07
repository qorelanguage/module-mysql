AUTOMAKE_OPTIONS = foreign

SUBDIRS = src

noinst_HEADERS = src/qore-mysql.h \
	src/qore-mysql-module.h

EXTRA_DIST = COPYING ChangeLog AUTHORS README \
	RELEASE-NOTES \
	test/db-test.q \
	docs/mysql-module-doc.html \
	qore-mysql-module.spec

ACLOCAL_AMFLAGS=-I m4

libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck
