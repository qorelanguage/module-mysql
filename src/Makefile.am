MNAME=mysql

.qpp.cpp:
	qpp $<

MYSQL_SOURCES = mysql.cpp

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

if COND_MINGW
RM_OTHER=$(DESTDIR)$(libdir)/$(MNAME).dll.a
endif

install-exec-hook:
	mv $(DESTDIR)$(libdir)/$(MNAME).$(MODULE_SUFFIX) $(DESTDIR)$(libdir)/$(MNAME)-api-$(MODULE_API).qmod
	rm -f $(DESTDIR)$(libdir)/$(MNAME).la $(DESTDIR)$(libdir)/$(MNAME).a $(RM_OTHER)

