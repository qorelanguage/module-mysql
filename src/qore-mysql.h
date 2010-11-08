/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
  qore-mysql.h

  Qore Programming Language

  Copyright (C) 2003 - 2010

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _QORE_MYSQL_H

#define _QORE_MYSQL_H

#include "../config.h"

#include <qore/Qore.h>

#include <mysql.h>

#ifdef HAVE_MYSQL_STMT
class MyResult {
private:
   MYSQL_FIELD *field;
   int num_fields;
   int type;
   MYSQL_BIND *bindbuf;
   struct bindInfo {
      my_bool mnull;
      long unsigned int mlen;
   } *bi;

public:
   DLLLOCAL inline MyResult(MYSQL_RES *res) {
      field = mysql_fetch_fields(res);
      num_fields = mysql_num_fields(res);
      mysql_free_result(res);

      bindbuf = NULL;
      bi = NULL;
   }

   DLLLOCAL inline ~MyResult() {
      if (bindbuf) {
	 // delete buffer
	 for (int i = 0; i < num_fields; i++)
	    if (bindbuf[i].buffer_type == MYSQL_TYPE_DOUBLE || bindbuf[i].buffer_type == MYSQL_TYPE_LONGLONG)
	       free(bindbuf[i].buffer);
	    else if (bindbuf[i].buffer_type == MYSQL_TYPE_STRING)
	       delete [] (char *)bindbuf[i].buffer;
	    else if (bindbuf[i].buffer_type == MYSQL_TYPE_DATETIME)
	       delete (MYSQL_TIME *)bindbuf[i].buffer;
	 delete [] bindbuf;
      }
      if (bi)
	 delete [] bi;
   }

   DLLLOCAL void bind(MYSQL_STMT *stmt);
   DLLLOCAL AbstractQoreNode *getBoundColumnValue(const QoreEncoding *csid, int i);

   DLLLOCAL inline char *getFieldName(int i) {
      return field[i].name;
   }

   DLLLOCAL inline int getNumFields() {
      return num_fields;
   }

};

// FIXME: do not assume byte widths
union my_val {
   MYSQL_TIME time;
   int i4;
   int64 i8;
   double f8;
   void *ptr;

   DLLLOCAL void assign(const DateTime &d) {
#ifdef _QORE_HAS_TIME_ZONES
      qore_tm tm;
      d.getInfo(tm);

      time.year = tm.year;
      time.month = tm.month;
      time.day = tm.day;
      time.hour = tm.hour;
      time.minute = tm.minute;
      time.second = tm.second;
#else
      time.year = d.getYear();
      time.month = d.getMonth();
      time.day = d.getDay();
      time.hour = d.getHour();
      time.minute = d.getMinute();
      time.second = d.getSecond();
#endif
      time.neg = false;
   }
};

class QoreMySQLBindNode {
private:

public:
   int bindtype;
   unsigned long len;

   struct {
      const AbstractQoreNode *value;   // value to be bound
      QoreStringNode *tstr;   // temporary string to be deleted
   } data;

   union my_val vbuf;
   QoreMySQLBindNode *next;

   // for value nodes
   DLLLOCAL inline QoreMySQLBindNode(const AbstractQoreNode *v) {
      bindtype = BN_VALUE;
      data.value = v;
      data.tstr = NULL;
      next = NULL;
   }

   DLLLOCAL inline ~QoreMySQLBindNode() {
      if (data.tstr)
	 data.tstr->deref();
   }
     
   DLLLOCAL int bindValue(const QoreEncoding *enc, MYSQL_BIND *buf, ExceptionSink *xsink);
};

static MYSQL *qore_mysql_init(Datasource *ds, ExceptionSink *xsink);

class QoreMySQLConnection {
public:
   MYSQL *db;

   DLLLOCAL QoreMySQLConnection(MYSQL *d) { db = d; }
   DLLLOCAL ~QoreMySQLConnection() {
      mysql_close(db);
   }

   DLLLOCAL int reconnect(Datasource *ds, MYSQL_STMT *&stmt, const QoreString *str, ExceptionSink *xsink) {	 
      // throw an exception if a transaction is in progress
#ifdef _QORE_HAS_DATASOURCE_ACTIVETRANSACTION
      if (ds->activeTransaction()) {
#else
      if (ds->isInTransaction()) {
#endif
         ds->connectionAborted();
	 xsink->raiseException("DBI:MYSQL:CONNECTION-ERROR", "connection to MySQL database server lost while in a transaction; transaction has been lost");
      }

      MYSQL *new_db = qore_mysql_init(ds, xsink);
      if (!new_db)
	 return -1;

      printd(5, "mysql datasource %08p reconnected after timeout\n", ds);
      mysql_close(db);
      db = new_db;

      if (ds->wasConnectionAborted())
         return -1;

      // reinitialize statement
      mysql_stmt_close(stmt);
      stmt = stmt_init(xsink);
      if (!stmt)
	 return -1;
	 
      // prepare the statement for execution (again)
      if (mysql_stmt_prepare(stmt, str->getBuffer(), str->strlen()))
	 return -1;
	 
      return 0;
   }
   DLLLOCAL int commit() {
      return mysql_commit(db);
   }
   DLLLOCAL int rollback() {
      return mysql_rollback(db);
   }
   DLLLOCAL const char *error() {
      return mysql_error(db);
   }
   DLLLOCAL int q_errno() {
      return mysql_errno(db);
   }
   DLLLOCAL MYSQL_STMT *stmt_init(ExceptionSink *xsink) {
      MYSQL_STMT *stmt = mysql_stmt_init(db);
      if (!stmt)
	 xsink->raiseException("DBI:MYSQL:ERROR", "error creating MySQL statement handle: out of memory");
      return stmt;
   }
   DLLLOCAL unsigned long getServerVersion() {
      return mysql_get_server_version(db);
   }
};

class QoreMySQLBindGroup {
private:
   QoreMySQLBindNode *head, *tail;
   QoreString *str;
   MYSQL_STMT *stmt;
   bool hasOutput;
   MYSQL_BIND *bind;
   Datasource *ds;
   QoreMySQLConnection *mydata;
   int len;
   cstr_vector_t phl;
   //bool locked;

   // returns -1 for error, 0 for OK
   DLLLOCAL inline int parse(const QoreListNode *args, ExceptionSink *xsink);
   DLLLOCAL inline void add(class QoreMySQLBindNode *c) {
      len++;
      if (!tail)
	 head = c;
      else
	 tail->next = c;
      tail = c;
   }

   DLLLOCAL inline class AbstractQoreNode *getOutputHash(ExceptionSink *xsink);
   DLLLOCAL class AbstractQoreNode *execIntern(ExceptionSink *xsink);

public:
   DLLLOCAL QoreMySQLBindGroup(Datasource *ods);
   DLLLOCAL ~QoreMySQLBindGroup();

   // returns 0=OK, -1=error and exception raised, 1=statement cannot be prepared
   DLLLOCAL int prepare_and_bind(const QoreString *ostr, const QoreListNode *args, ExceptionSink *xsink);

   DLLLOCAL inline void add(const AbstractQoreNode *v) {
      add(new QoreMySQLBindNode(v));
      printd(5, "QoreMySQLBindGroup::add() value=%08p\n", v);
   }

   DLLLOCAL inline void add(char *name) {
      phl.push_back(name);
      printd(5, "QoreMySQLBindGroup::add() placeholder '%s' %d %s\n", name);
      hasOutput = true;
   }
   DLLLOCAL class AbstractQoreNode *exec(ExceptionSink *xsink);
   DLLLOCAL class AbstractQoreNode *select(ExceptionSink *xsink);
   DLLLOCAL class AbstractQoreNode *selectRows(ExceptionSink *xsink);
};

#endif // HAVE_MYSQL_STMT

#endif // _QORE_MYSQL_H
