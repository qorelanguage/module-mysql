MYSQL_SOURCES = mysql.cc

lib_LTLIBRARIES = mysql.la
mysql_la_SOURCES = $(MYSQL_SOURCES)
mysql_la_LDFLAGS = -module -avoid-version ${MYSQL_LIBS}

INCLUDES = -I$(top_srcdir)/include $(MYSQL_INCLUDES)

AM_CPPFLAGS=${QORE_CPPFLAGS}

if COND_PROFILE
AM_CXXFLAGS=-g -pg
else
if COND_DEBUG
AM_CXXFLAGS=-g
endif
endif

install-exec-hook:
	mv $(DESTDIR)$(libdir)/mysql.$(MODULE_SUFFIX) $(DESTDIR)$(libdir)/mysql-api-$(MODULE_API).qmod
	rm -f $(DESTDIR)$(libdir)/mysql.la

