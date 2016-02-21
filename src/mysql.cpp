/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
  mysql.cpp

  Mysql Interface to Qore DBI layer

  Qore Programming Language

  0.4.0 changes:
  * multi-threaded access added
  * transaction management added
  * character set support added

  Copyright (C) 2003 - 2016 David Nichols

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

#include "qore-mysql.h"
#include "qore-mysql-module.h"

#include <errmsg.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>

#include <mysqld_error.h>

DLLEXPORT char qore_module_name[] = "mysql";
DLLEXPORT char qore_module_version[] = PACKAGE_VERSION;
#if defined(MARIADB_BASE_VERSION)
DLLEXPORT char qore_module_description[] = "Mariadb/MySQL database driver";
#else
DLLEXPORT char qore_module_description[] = "Mysql database driver";
#endif
DLLEXPORT char qore_module_author[] = "David Nichols <david@qore.org>";
DLLEXPORT char qore_module_url[] = "http://qore.org";
DLLEXPORT int qore_module_api_major = QORE_MODULE_API_MAJOR;
DLLEXPORT int qore_module_api_minor = QORE_MODULE_API_MINOR;
DLLEXPORT qore_module_init_t qore_module_init = qore_mysql_module_init;
DLLEXPORT qore_module_ns_init_t qore_module_ns_init = qore_mysql_module_ns_init;
DLLEXPORT qore_module_delete_t qore_module_delete = qore_mysql_module_delete;
#if defined(HAVE_MYSQL_CLIENT_LICENSE) || defined(MARIADB_BASE_VERSION)
DLLEXPORT qore_license_t qore_module_license = QL_LGPL;
DLLEXPORT char qore_module_license_str[] = "LGPL 2.1";
#else
DLLEXPORT qore_license_t qore_module_license = QL_GPL;
DLLEXPORT char qore_module_license_str[] = "GPL 2.1";
#endif

// driver capabilities
static int mysql_caps = DBI_CAP_NONE
#ifdef HAVE_MYSQL_COMMIT
   | DBI_CAP_TRANSACTION_MANAGEMENT
#endif
#ifdef HAVE_MYSQL_SET_CHARACTER_SET
   | DBI_CAP_CHARSET_SUPPORT
#endif
#ifdef HAVE_MYSQL_STMT
   | DBI_CAP_STORED_PROCEDURES | DBI_CAP_LOB_SUPPORT | DBI_CAP_BIND_BY_VALUE
#ifdef _QORE_HAS_NUMBER_TYPE
   | DBI_CAP_HAS_NUMBER_SUPPORT
#endif
#endif
#ifdef _QORE_HAS_DBI_EXECRAW
   | DBI_CAP_HAS_EXECRAW
#endif
#ifdef _QORE_HAS_FIND_CREATE_TIMEZONE
   | DBI_CAP_SERVER_TIME_ZONE
#endif
;

DBIDriver* DBID_MYSQL = 0;

// this is the thread key that will tell us if the current thread has been initialized for mysql threading
static pthread_key_t ptk_mysql;

static struct mapEntry {
      char *mysql;
      const QoreEncoding* id;
} mapList[] =
{
   { (char*)"utf8", QCS_UTF8 },
   { (char*)"latin1", QCS_ISO_8859_1 },
   { (char*)"latin2", QCS_ISO_8859_2 },
   { (char*)"ascii", QCS_USASCII },
   { (char*)"koi8r", QCS_KOI8_R },
   { (char*)"koi8u", QCS_KOI8_U },
   { (char*)"greek", QCS_ISO_8859_7 },
   { (char*)"hebrew", QCS_ISO_8859_8 },
   { (char*)"latin5", QCS_ISO_8859_9 },
   { (char*)"latin7", QCS_ISO_8859_13 },
   //{ "", "big5_chinese_ci" },
   //{ "", "dec8_swedish_ci" },
   //{ "", "cp850_general_ci" },
   //{ "", "hp8_english_ci" },
   //{ "", "swe7_swedish_ci" },
   //{ "", "ujis_japanese_ci" },
   //{ "", "sjis_japanese_ci" },
   //{ "", "tis620_thai_ci" },
   //{ "", "euckr_korean_ci" },
   //{ "", "gb2312_chinese_ci" },
   //{ "", "cp1250_general_ci" },
   //{ "", "gbk_chinese_ci" },
   //{ "", "armscii8_general_ci" },
   //{ "", "ucs2_general_ci" },
   //{ "", "cp866_general_ci" },
   //{ "", "keybcs2_general_ci" },
   //{ "", "macce_general_ci" },
   //{ "", "macroman_general_ci" },
   //{ "", "cp852_general_ci" },
   //{ "", "cp1251_general_ci" },
   //{ "", "cp1256_general_ci" }, iso-8859-6 ?
   //{ "", "cp1257_general_ci" },
   //{ "", "binary" },
   //{ "", "geostd8_general_ci" },
   //{ "", "cp932_japanese_ci" },
};

#define NUM_CHARMAPS (sizeof(mapList) / sizeof(struct mapEntry))

static const QoreEncoding* get_qore_cs(char *cs) {
   int end;
   // get end of charset name
   char *p = strchr(cs, '_');
   if (p)
      end = p - cs;
   else
      end = strlen(cs);

   for (unsigned i = 0; i < NUM_CHARMAPS; i++)
      if (!strncasecmp(cs, mapList[i].mysql, end))
         return mapList[i].id;

   QoreString cset;
   cset.concat(cs, end);
   return QEM.findCreate(&cset);
}

static char* get_mysql_cs(const QoreEncoding* id) {
   for (unsigned i = 0; i < NUM_CHARMAPS; i++)
      if (mapList[i].id == id)
         return mapList[i].mysql;

   return NULL;
}

void my_val::assign(const QoreMysqlConnection& conn, const DateTime &d) {
#ifdef _QORE_HAS_TIME_ZONES
   qore_tm tm;
   d.getInfo(conn.getTZ(), tm);

   time.year = tm.year;
   time.month = tm.month;
   time.day = tm.day;
   time.hour = tm.hour;
   time.minute = tm.minute;
   time.second = tm.second;
   time.second_part = tm.us;
#else
   time.year = d.getYear();
   time.month = d.getMonth();
   time.day = d.getDay();
   time.hour = d.getHour();
   time.minute = d.getMinute();
   time.second = d.getSecond();
   time.second_part = tm.getMicrosecond();
#endif
   time.neg = false;
}

static void check_init() {
   if (!pthread_getspecific(ptk_mysql)) {
      mysql_thread_init();
      pthread_setspecific(ptk_mysql, (void*)1);
   }
}

static void mysql_thread_cleanup(void* unused) {
   if (pthread_getspecific(ptk_mysql))
      mysql_thread_end();
}

static DateTimeNode* qore_mysql_makedt(const QoreMysqlConnection& conn, int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int us = 0) {
   // we have to ensure that time values with no date component are created with 1970-01-01 (the start of the UNIX epoch)
   if (!year && !month && !day) {
      year = 1970;
      month = 01;
      day = 01;
   }
#ifdef _QORE_HAS_TIME_ZONES
   return DateTimeNode::makeAbsolute(conn.getTZ(), year, month, day, hour, minute, second, us);
#else
   return new DateTimeNode(year, month, day, hour, minute, second, us);
#endif
}

static MYSQL* qore_mysql_init(Datasource* ds, ExceptionSink* xsink) {
   printd(5, "qore_mysql_init() datasource %08p for DB=%s\n", ds,
          ds->getDBName() ? ds->getDBName() : "unknown");

   if (!ds->getDBName()) {
      xsink->raiseException("DATASOURCE-MISSING-DBNAME", "Datasource has an empty dbname parameter");
      return 0;
   }

   if (ds->getDBEncoding())
      ds->setQoreEncoding(get_qore_cs((char *)ds->getDBEncoding()));
   else {
      char *enc = get_mysql_cs(QCS_DEFAULT);
      if (!enc) {
         xsink->raiseException("DBI:MYSQL:UNKNOWN-CHARACTER-SET", "cannot find the mysql character set equivalent for '%s'", QCS_DEFAULT->getCode());
         return 0;
      }

      ds->setDBEncoding(enc);
      ds->setQoreEncoding(QCS_DEFAULT);
   }

   MYSQL *db = mysql_init(NULL);
   if (!db) {
      xsink->outOfMemory();
      return 0;
   }
#ifdef QORE_HAS_DATASOURCE_PORT
   int port = ds->getPort();
#else
   int port = 0;
#endif

   if (!port && ds->getHostName())
      port = MYSQL_PORT;

   printd(3, "qore_mysql_init(): user: '%s' pass: '%s' db: '%s' (encoding=%s) host: '%s' port: %d\n",
          ds->getUsername(), ds->getPassword(), ds->getDBName(), ds->getDBEncoding() ? ds->getDBEncoding() : "(none)", ds->getHostName(), port);


   if (!mysql_real_connect(db, ds->getHostName(), ds->getUsername(), ds->getPassword(), ds->getDBName(), port, 0, CLIENT_FOUND_ROWS)) {
      xsink->raiseException("DBI:MYSQL:CONNECT-ERROR", "%s", mysql_error(db));
      mysql_close(db);
      return 0;
   }

#ifdef HAVE_MYSQL_SET_CHARACTER_SET
   // set character set
   mysql_set_character_set(db, ds->getDBEncoding());
#endif

#ifdef HAVE_MYSQL_COMMIT
   // autocommits are handled by qore, not by Mysql
   mysql_autocommit(db, false);

   // set transaction handling
   if (mysql_query(db, "set session transaction isolation level read committed")) {
      xsink->raiseException("DBI:MYSQL:INIT-ERROR", (char *)mysql_error(db));
      mysql_close(db);
      return 0;
   }
#endif

   return db;
}

static int qore_mysql_commit(Datasource* ds, ExceptionSink* xsink) {
#ifdef HAVE_MYSQL_COMMIT
   check_init();
   QoreMysqlConnection *d_mysql =(QoreMysqlConnection *)ds->getPrivateData();

   // calls mysql_commit() on the connection
   if (d_mysql->commit()) {
      xsink->raiseException("DBI:MYSQL:COMMIT-ERROR", d_mysql->error());
      return -1;
   }
   return 0;
#else
   xsink->raiseException("DBI:MYSQL:NOT-IMPLEMENTED", "this version of the Mysql client API does not support transaction management");
   return -1;
#endif
}

static int qore_mysql_rollback(Datasource* ds, ExceptionSink* xsink) {
#ifdef HAVE_MYSQL_COMMIT
   check_init();
   QoreMysqlConnection *d_mysql =(QoreMysqlConnection *)ds->getPrivateData();

   // calls mysql_rollback() on the connection
   if (d_mysql->rollback()) {
      xsink->raiseException("DBI:MYSQL:ROLLBACK-ERROR", d_mysql->error());
      return -1;
   }
   return 0;
#else
   xsink->raiseException("DBI:MYSQL:NOT-IMPLEMENTED", "this version of the Mysql client API does not support transaction management");
   return -1;
#endif
}

#ifdef HAVE_MYSQL_STMT
void MyResult::bind(MYSQL_STMT *stmt) {
   if (bindbuf)
      return;

   bindbuf = new MYSQL_BIND[num_fields];
   bi      = new bindInfo[num_fields];

   // zero out bind memory
   memset(bindbuf, 0, sizeof(MYSQL_BIND) * num_fields);

   for (int i = 0; i < num_fields; i++) {
      // setup bind structure
      //printd(5, "%d type=%d (%d %d %d)\n", field[i].type, FIELD_TYPE_TINY_BLOB, FIELD_TYPE_MEDIUM_BLOB, FIELD_TYPE_BLOB);
      switch (field[i].type) {
         // for integer values
         case FIELD_TYPE_SHORT:
         case FIELD_TYPE_LONG:
         case FIELD_TYPE_LONGLONG:
         case FIELD_TYPE_INT24:
         case FIELD_TYPE_TINY:
         case FIELD_TYPE_YEAR:
            bindbuf[i].buffer_type = MYSQL_TYPE_LONGLONG;
            bindbuf[i].buffer = malloc(sizeof(int64));
            break;

            // for floating point values
         case FIELD_TYPE_FLOAT:
         case FIELD_TYPE_DOUBLE:
            bindbuf[i].buffer_type = MYSQL_TYPE_DOUBLE;
            bindbuf[i].buffer = malloc(sizeof(double));
            break;

            // for datetime values
         case FIELD_TYPE_DATETIME:
         case FIELD_TYPE_DATE:
         case FIELD_TYPE_TIME:
         case FIELD_TYPE_TIMESTAMP:
            bindbuf[i].buffer_type = MYSQL_TYPE_DATETIME;
            bindbuf[i].buffer = new MYSQL_TIME;
            break;

            // for binary types
         case FIELD_TYPE_TINY_BLOB:
         case FIELD_TYPE_MEDIUM_BLOB:
         case FIELD_TYPE_BLOB:
         case FIELD_TYPE_LONG_BLOB:
            // this is only binary data if charsetnr == 63
            if (field[i].charsetnr == 63) {
               bindbuf[i].buffer_type = MYSQL_TYPE_BLOB;
               bindbuf[i].buffer = malloc(sizeof(char) * field[i].length);
               bindbuf[i].buffer_length = field[i].length;
               break;
            }

            // for all other types (treated as string)
         default:
            bindbuf[i].buffer_type = MYSQL_TYPE_STRING;
            bindbuf[i].buffer = malloc(sizeof(char) * (field[i].length + 1));
            bindbuf[i].buffer_length = field[i].length + 1;
            break;
      }
      bi[i].mnull = 0;
      bindbuf[i].is_null = &bi[i].mnull;
      bi[i].mlen = 0;
      bindbuf[i].length = &bi[i].mlen;
   }

   // FIXME: check for errors here
   mysql_stmt_bind_result(stmt, bindbuf);
}

AbstractQoreNode* MyResult::getBoundColumnValue(int i, bool destructive) {
   AbstractQoreNode* n = NULL;

   unsigned long len = *bindbuf[i].length;

   if (bi[i].mnull)
      n = null();
   else if (bindbuf[i].buffer_type == MYSQL_TYPE_LONGLONG)
      n = new QoreBigIntNode(*((int64 *)bindbuf[i].buffer));
   else if (bindbuf[i].buffer_type == MYSQL_TYPE_DOUBLE)
      n = new QoreFloatNode(*((double *)bindbuf[i].buffer));
   else if (bindbuf[i].buffer_type == MYSQL_TYPE_STRING) {
      const char* p = (const char *)bindbuf[i].buffer;
      // see if this was originally a decimal/numeric type
      if (field[i].type == FIELD_TYPE_DECIMAL
#ifdef FIELD_TYPE_NEWDECIMAL
          || field[i].type == FIELD_TYPE_NEWDECIMAL
#endif
         ) {
         switch (conn->getNumeric()) {
            case OPT_NUM_OPTIMAL: {
               size_t len = strlen(p);
               bool has_decimal = (bool)strchr(p, '.');
               if (has_decimal) {
                  char* c = (char*)p;
                  // trim off trailing zeros
                  while (len && c[len - 1] == '0') {
                     --len;
                     c[len] = '\0';
                  }
                  if (c[len - 1] == '.') {
                     --len;
                     c[len] = '\0';
                     has_decimal = false;
                  }
               }
               if (!has_decimal) {
                  bool sign = p[0] == '-';
                  if (sign)
                     --len;
                  if (!strchr(p, '.')
                      && (len < 19
                          || (len == 19 &&
                              ((!sign && strcmp(p, "9223372036854775807") <= 0)
                               ||(sign && strcmp(p, "-9223372036854775808") >= 0)))))
                     return new QoreBigIntNode(strtoll(p, 0, 10));
               }
#ifdef _QORE_HAS_NUMBER_TYPE
               return new QoreNumberNode(p);
#else
               // return as string
               break;
#endif
            }
#ifdef _QORE_HAS_NUMBER_TYPE
            case OPT_NUM_NUMERIC:
               return new QoreNumberNode(p);
#endif
         }
         // fall through and return as a string
      }

      //printd(5, "string destr: %d (%ld): '%s' flen: %ld blen: %ld charsetnr: %d\n", destructive, strlen(p), p, field[i].length, len, field[i].charsetnr);

      // return a binary object for binary values
      if (field[i].charsetnr == 63) {
         if (destructive) {
            n = new BinaryNode((void*)p, len);
	    bindbuf[i].buffer = 0;
	 }
	 else {
	    BinaryNode* b = new BinaryNode();
	    b->append(p, len);
	    n = b;
	 }
      }
      else {
	 //printd(5, "string destr: %d (%ld): '%s'\n", destructive, strlen(p), p);
	 if (destructive) {
	    n = new QoreStringNode((char*)p, len, len + 1, enc);
	    bindbuf[i].buffer = 0;
	 }
	 else
	    n = new QoreStringNode(p, enc);
      }
   }
   else if (bindbuf[i].buffer_type == MYSQL_TYPE_DATETIME) {
      MYSQL_TIME *t = (MYSQL_TIME*)bindbuf[i].buffer;
      n = qore_mysql_makedt(*conn, t->year, t->month, t->day, t->hour, t->minute, t->second);
   }
   else if (bindbuf[i].buffer_type == MYSQL_TYPE_BLOB) {
      if (destructive) {
         n = new BinaryNode(bindbuf[i].buffer, len);
         bindbuf[i].buffer = 0;
      }
      else {
         BinaryNode* b = new BinaryNode;
         b->append(bindbuf[i].buffer, len);
         n = b;
      }
   }

   //printd(5, "MyResult::getBoundColumnValue(%d) this: %p returning: %p (%s)\n", i, this, n, get_type_name(n));
   return n;
}

void QoreMysqlBindGroup::reset(ExceptionSink* xsink) {
   if (bind) {
      delete [] bind;
      bind = 0;
   }

   if (stmt) {
      mysql_stmt_close(stmt);
      stmt = 0;
   }

   if (str) {
      delete str;
      str = 0;
   }

   if (head) {
      QoreMysqlBindNode* w = head;
      while (w) {
         head = w->next;
         w->del(xsink);
         w = head;
      }
      tail = 0;
   }
}

int QoreMysqlBindGroup::prepare(bool unsupported_ok, ExceptionSink* xsink) {
   assert(!stmt);
   stmt = mydata->stmt_init(xsink);
   if (!stmt)
      return -1;

   if (mysql_stmt_prepare(stmt, str->getBuffer(), str->strlen())) {
      int en = mydata->q_errno();
      if (en != CR_SERVER_GONE_ERROR) {
         if (en == ER_UNSUPPORTED_PS && unsupported_ok)
            return 1;
         xsink->raiseException("DBI:MYSQL:STATEMENT-ERROR", "error %d: %s", en, mydata->error());
         return -1;
      }

      if (mydata->reconnect(ds, stmt, *str, xsink))
         return -1;
   }

   return 0;
}

int QoreMysqlBindGroup::bindArgs(ExceptionSink* xsink) {
   if (!len)
      return 0;

   if (!bind) {
      // allocate bind buffer
      bind = new MYSQL_BIND[len];
      // zero out bind memory
      memset(bind, 0, sizeof(MYSQL_BIND) * len);
   }

   // (re-)bind all values/placeholders
   QoreMysqlBindNode* w = head;
   int pos = 0;
   while (w) {
      printd(5, "MBG::MBG() binding value at position %d (%s)\n", pos, w->data.value ? w->data.value->getTypeName() : "<null>");
      if (w->bindValue(*mydata, &bind[pos], xsink))
         return -1;
      pos++;
      w = w->next;
   }

   // now perform the bind
   if (mysql_stmt_bind_param(stmt, bind)) {
      xsink->raiseException("DBI:MYSQL-ERROR", "error %d: %s", mydata->q_errno(), mydata->error());
      return -1;
   }

   return 0;
}

int QoreMysqlBindGroup::rebindArgs(const QoreListNode* args, ExceptionSink* xsink) {
   QoreMysqlBindNode* w = head;
   unsigned pos = 0;
   while (w) {
      if (w->rebind(args->retrieve_entry(pos), xsink))
         return -1;
      w = w->next;
      ++pos;
   }

   return bindArgs(xsink);
}

int QoreMysqlBindGroup::prepareAndBind(const QoreString* ostr, const QoreListNode* args, ExceptionSink* xsink) {
   // create copy of string and convert encoding if necessary
   str = ostr->convertEncoding(ds->getQoreEncoding(), xsink);
   if (!str)
      return -1;

   // parse query and bind variables/placeholders, return on error
   if (parse(args, xsink))
      return -1;

   //printd(5, "mysql prepare: (%d) %s\n", str->strlen(), str->getBuffer());

   // prepare the statement for execution
   int rc = prepare(true, xsink);
   if (rc)
      return rc;

   // if there is data to bind, then bind it
   if (bindArgs(xsink))
      return -1;

   return 0;
}

#define QMDC_LINE 1
#define QMDC_BLOCK 2

int QoreMysqlBindGroup::parse(const QoreListNode* args, ExceptionSink* xsink) {
   char quote = 0;

   const char *p = str->getBuffer();
   int index = 0;
   QoreString tmp(ds->getQoreEncoding());

   int comment = 0;

   while (*p) {
      if (!quote) {
         if (!comment) {
            if ((*p) == '-' && (*(p+1)) == '-') {
               comment = QMDC_LINE;
               p += 2;
               continue;
            }

            if ((*p) == '#') {
               comment = QMDC_LINE;
               ++p;
               continue;
            }

            if ((*p) == '/' && (*(p+1)) == '*') {
               comment = QMDC_BLOCK;
               p += 2;
               continue;
            }
         }
         else {
            if (comment == QMDC_LINE) {
               if ((*p) == '\n' || ((*p) == '\r'))
                  comment = 0;
               ++p;
               continue;
            }

            assert(comment == QMDC_BLOCK);
            if ((*p) == '*' && (*(p+1)) == '/') {
               comment = 0;
               p += 2;
               continue;
            }

            ++p;
            continue;
         }

         if ((*p) == '%') { // found value marker
            const AbstractQoreNode* v = args ? args->retrieve_entry(index++) : NULL;
            int offset = p - str->getBuffer();

            p++;
            if ((*p) == 'd') {
               DBI_concat_numeric(&tmp, v);
               str->replace(offset, 2, &tmp);
               p = str->getBuffer() + offset + tmp.strlen();
               tmp.clear();
               continue;
            }
            if ((*p) == 's') {
               if (DBI_concat_string(&tmp, v, xsink))
                  return -1;
               str->replace(offset, 2, &tmp);
               p = str->getBuffer() + offset + tmp.strlen();
               tmp.clear();
               continue;
            }
            if ((*p) != 'v') {
               xsink->raiseException("DBI-EXEC-PARSE-EXCEPTION", "invalid value specification (expecting '%%v' or '%%d', got %%%c)", *p);
               return -1;
            }
            p++;
            if (isalpha(*p)) {
               xsink->raiseException("DBI-EXEC-PARSE-EXCEPTION", "invalid value specification (expecting '%%v' or '%%d', got %%v%c)", *p);
               return -1;
            }

            // replace value marker with "?"
            // find byte offset in case string buffer is reallocated with replace()
            str->replace(offset, 2, "?");
            p = str->getBuffer() + offset;

            printd(5, "QoreMysqlBindGroup::parse() newstr=%s\n", str->getBuffer());
            printd(5, "QoreMysqlBindGroup::parse() adding value type=%s\n",v ? v->getTypeName() : "<NULL>");
            add(v);
            continue;
         }

         if ((*p) == ':') { // found placeholder marker
            const char *w = p;

            p++;
            if (!isalpha(*p))
               continue;

            // get placeholder name
            QoreString tstr;
            while (isalnum(*p) || (*p) == '_')
               tstr.concat(*(p++));

            printd(5, "QoreMysqlBindGroup::parse() adding placeholder for '%s'\n", tstr.getBuffer());
            add(tstr.giveBuffer());

            // substitute "@" for ":" in bind name
            // find byte position of start of string
            int offset = w - str->getBuffer();
            str->replace(offset, 1, "@");

            printd(5, "QoreMysqlBindGroup::parse() offset=%d, new str=%s\n", offset, str->getBuffer());
            continue;
         }
      }

      if (((*p) == '\'') || ((*p) == '\"')) {
         if (!quote)
            quote = *p;
         else if (quote == (*p))
            quote = 0;
         p++;
         continue;
      }

      p++;
   }

   return 0;
}

QoreHashNode* QoreMysqlBindGroup::getOutputHash(ExceptionSink* xsink) {
   ReferenceHolder<QoreHashNode> h(new QoreHashNode, xsink);

   cstr_vector_t::iterator sli = phl.begin();
   while (sli != phl.end()) {
      // setup a temporary statement to retrieve values
      MYSQL_STMT *tmp_stmt = mydata->stmt_init(xsink);
      if (!tmp_stmt)
         return 0;
      ON_BLOCK_EXIT(mysql_stmt_close, tmp_stmt);

      QoreString qstr;
      qstr.sprintf("select @%s", *sli);

      // prepare the statement for execution
      if (mysql_stmt_prepare(tmp_stmt, qstr.getBuffer(), qstr.strlen())) {
         xsink->raiseException("DBI:MYSQL:ERROR", mydata->error());
         return 0;
      }

      AbstractQoreNode* v = NULL;

      MyResult tmpres(mydata, ds->getQoreEncoding());
      tmpres.set(tmp_stmt);
      // don't even execute the statement if there is no result data
      if (tmpres) {
         // execute the temporary statement
         if (mysql_stmt_execute(tmp_stmt)) {
            xsink->raiseException("DBI:MYSQL:ERROR", mydata->error());
            return 0;
         }

         int rows = mysql_stmt_affected_rows(tmp_stmt);
         if (rows) {
            tmpres.bind(tmp_stmt);

            if (rows > 1) {
               QoreListNode* l = new QoreListNode;
               while (!mysql_stmt_fetch(tmp_stmt))
                  l->push(tmpres.getBoundColumnValue(0, true));
               v = l;
            }
            else {
               mysql_stmt_fetch(tmp_stmt);
               v = tmpres.getBoundColumnValue(0, true);
            }
         }
      }

      h->setKeyValue(*sli, v, xsink);
      sli++;
   }
   return h.release();
}

int QoreMysqlBindGroup::execIntern(ExceptionSink* xsink) {
   assert(stmt);
   myres.reset();

   if (mysql_stmt_execute(stmt)) {
      xsink->raiseException("DBI:MYSQL:ERROR", mydata->error());
      return -1;
   }

   myres.set(stmt);

   return 0;
}

int QoreMysqlBindGroup::getDataRows(QoreListNode& l, ExceptionSink* xsink, int max) {
   // row count
   int c = 0;
   while ((max < 0 || c < max) && !mysql_stmt_fetch(stmt)) {
      l.push(myres.getSingleRow(xsink));
      assert(!*xsink);
      ++c;
   }

   return 0;
}

int QoreMysqlBindGroup::getDataColumns(QoreHashNode& h, ExceptionSink* xsink, int max) {
   assert(!h.empty());

   // row count
   int c = 0;
   while ((max < 0 || c < max) && !mysql_stmt_fetch(stmt)) {
      HashIterator hi(h);
      int i = 0;
      while (hi.next()) {
         QoreListNode* l = reinterpret_cast<QoreListNode*>(hi.getValue());
         l->push(myres.getBoundColumnValue(i++));
      }
      ++c;
   }

   return 0;
}

AbstractQoreNode* QoreMysqlBindGroup::exec(ExceptionSink* xsink) {
   if (execIntern(xsink))
      return 0;

   if (myres) {
      ReferenceHolder<QoreHashNode> h(myres.setupColumns(), xsink);

      if (!mysql_stmt_affected_rows(stmt))
         return h.release();

      myres.bind(stmt);

      return getDataColumns(**h, xsink) ? 0 : h.release();
   }

   return !hasOutput
      ? (AbstractQoreNode*)new QoreBigIntNode((int64)mysql_stmt_affected_rows(stmt))
      : (AbstractQoreNode*)getOutputHash(xsink);
}

AbstractQoreNode* QoreMysqlBindGroup::selectRows(ExceptionSink* xsink) {
   if (execIntern(xsink))
      return 0;

   if (myres) {
      ReferenceHolder<QoreListNode> l(new QoreListNode, xsink);

      if (!mysql_stmt_affected_rows(stmt))
         return l.release();

      myres.bind(stmt);

      return getDataRows(**l, xsink) ? 0 : l.release();
   }

   return !hasOutput
      ? (AbstractQoreNode*)new QoreBigIntNode((int64)mysql_stmt_affected_rows(stmt))
      : (AbstractQoreNode*)getOutputHash(xsink);
}

QoreHashNode* QoreMysqlBindGroup::selectRow(ExceptionSink* xsink) {
   if (execIntern(xsink))
      return 0;

   if (myres) {
      my_ulonglong rowcnt = mysql_stmt_affected_rows(stmt);
      if (!rowcnt)
         return 0;

      myres.bind(stmt);

      QoreString tstr;
      const QoreEncoding* enc = ds->getQoreEncoding();

      if (!mysql_stmt_fetch(stmt)) {
         ReferenceHolder<QoreHashNode> h(new QoreHashNode, xsink);

         for (int i = 0; i < myres.getNumFields(); i++) {
            get_lower_case_name(&tstr, enc, myres.getFieldName(i));
            h->setKeyValue(&tstr, myres.getBoundColumnValue(i, true), xsink);
         }

         // see if there is a second row
         if (!mysql_stmt_fetch(stmt)) {
            xsink->raiseException("MYSQL-SELECT-ROW-ERROR", "SQL passed to selectRow() returned more than 1 row");
            return 0;
         }

         return h.release();
      }

      return 0;
   }

   return hasOutput ? getOutputHash(xsink) : 0;
}

int QoreMysqlBindNode::bindValue(const QoreMysqlConnection& conn, MYSQL_BIND* buf, ExceptionSink* xsink) {
   //printd(5, "QoreMysqlBindNode::bindValue() type=%s\n", data.value ? data.value->getTypeName() : "NOTHING");

   // bind a NULL value
   if (is_nothing(data.value) || is_null(data.value)) {
      buf->buffer_type = MYSQL_TYPE_NULL;
      return 0;
   }

   qore_type_t ntype = data.value->getType();

   if (ntype == NT_STRING) {
      QoreStringNode* bstr = const_cast<QoreStringNode*>(reinterpret_cast<const QoreStringNode*>(data.value));
      const QoreEncoding* enc = conn.ds.getQoreEncoding();
      // convert to the db charset if necessary
      if (bstr->getEncoding() != enc) {
         bstr = bstr->convertEncoding(enc, xsink);
         if (!bstr) // exception was thrown
            return -1;
         // save temporary string for later deleting
         data.tstr = bstr;
      }

      len = bstr->strlen();

      buf->buffer_type = MYSQL_TYPE_STRING;
      buf->buffer = (char *)bstr->getBuffer();
      buf->buffer_length = len + 1;
      buf->length = &len;
      return 0;
   }

   if (ntype == NT_DATE) {
      const DateTimeNode* date = reinterpret_cast<const DateTimeNode*>(data.value);
      vbuf.assign(conn, *date);

      buf->buffer_type = MYSQL_TYPE_DATETIME;
      buf->buffer = &vbuf.time;
      return 0;
   }

   if (ntype == NT_BINARY) {
      const BinaryNode* b = reinterpret_cast<const BinaryNode*>(data.value);
      len = b->size();
      buf->buffer_type = MYSQL_TYPE_BLOB;
      buf->buffer = (void*)b->getPtr();
      buf->buffer_length = len;
      buf->length = &len;
      return 0;
   }

   if (ntype == NT_BOOLEAN) {
      vbuf.i4 = reinterpret_cast<const QoreBoolNode*>(data.value)->getValue();
      buf->buffer_type = MYSQL_TYPE_LONG;
      buf->buffer = (char *)&vbuf.i4;
      return 0;
   }

   if (ntype == NT_INT) {
      buf->buffer_type = MYSQL_TYPE_LONGLONG;
      buf->buffer = (char *)&(reinterpret_cast<const QoreBigIntNode*>(data.value))->val;
      return 0;
   }

   if (ntype == NT_FLOAT) {
      buf->buffer_type = MYSQL_TYPE_DOUBLE;
      buf->buffer = (char *)&(reinterpret_cast<const QoreFloatNode*>(data.value)->f);
      return 0;
   }

#ifdef _QORE_HAS_NUMBER_TYPE
   if (ntype == NT_NUMBER) {
      const QoreNumberNode* num = reinterpret_cast<const QoreNumberNode*>(data.value);

      // create string for binding (will be deleted later)
      data.tstr = new QoreStringNode;
      // convert number to string
      num->getStringRepresentation(*data.tstr);

      len = data.tstr->strlen();

      buf->buffer_type = MYSQL_TYPE_STRING;
      buf->buffer = (char *)data.tstr->getBuffer();
      buf->buffer_length = len + 1;
      buf->length = &len;
      return 0;
   }
#endif

   xsink->raiseException("DBI-EXEC-EXCEPTION", "type '%s' is not supported for SQL binding", data.value->getTypeName());
   return -1;
}

#ifdef _QORE_HAS_PREPARED_STATMENT_API
int QoreMysqlPreparedStatement::prepare(const QoreString& n_sql, const QoreListNode* args, bool n_parse, ExceptionSink* xsink) {
   assert(!str);
   // create copy of string and convert encoding if necessary
   str = n_sql.convertEncoding(ds->getQoreEncoding(), xsink);
   if (*xsink)
      return -1;

   if (n_parse && parse(args, xsink))
      return -1;

   if (QoreMysqlBindGroup::prepare(false, xsink))
      return -1;

   return bindArgs(xsink);
}

int QoreMysqlPreparedStatement::bind(const QoreListNode &l, ExceptionSink *xsink) {
   return rebindArgs(&l, xsink);
}

int QoreMysqlPreparedStatement::define(ExceptionSink *xsink) {
   //printd(5, "QoreMysqlPreparedStatement::define() this: %p myres: %d\n", this, (bool)myres);
   if (myres)
      myres.bind(stmt);
   return 0;
}

int QoreMysqlPreparedStatement::exec(ExceptionSink* xsink) {
   return execIntern(xsink);
}

QoreHashNode* QoreMysqlPreparedStatement::fetchRow(ExceptionSink* xsink) {
   if (!myres) {
      xsink->raiseException("DBI:MYSQL-FETCH-ROW-ERROR", "call SQLStatement::next() before calling SQLStatement::fetchRow()");
      return 0;
   }
   return myres.getSingleRow(xsink);
}

QoreListNode* QoreMysqlPreparedStatement::fetchRows(int rows, ExceptionSink *xsink) {
   ReferenceHolder<QoreListNode> l(new QoreListNode, xsink);
   return !getDataRows(**l, xsink, rows) ? l.release() : 0;
}

QoreHashNode* QoreMysqlPreparedStatement::fetchColumns(int rows, ExceptionSink *xsink) {
   ReferenceHolder<QoreHashNode> h(myres.setupColumns(), xsink);
   return !getDataColumns(**h, xsink) ? h.release() : 0;
}

#ifdef _QORE_HAS_DBI_DESCRIBE
QoreHashNode* QoreMysqlPreparedStatement::describe(ExceptionSink *xsink) {
   if (!myres) {
      xsink->raiseException("DBI:MYSQL-DESCRIBE-ERROR", "call SQLStatement::next() before calling SQLStatement::describe()");
      return 0;
   }

   // set up hash for row
   ReferenceHolder<QoreHashNode> h(new QoreHashNode, xsink);
   QoreString namestr("name");
   QoreString maxsizestr("maxsize");
   QoreString typestr("type");
   QoreString dbtypestr("native_type");
   QoreString internalstr("internal_id");

   // copy data or perform per-value processing if needed
   for (int i = 0; i < myres.getNumFields(); ++i) {
      ReferenceHolder<QoreHashNode> col(new QoreHashNode, xsink);
      col->setKeyValue(namestr, new QoreStringNode(myres.getFieldName(i)), xsink);
      col->setKeyValue(maxsizestr, new QoreBigIntNode(myres.getFieldMaxLength(i)), xsink);
      col->setKeyValue(internalstr, new QoreBigIntNode(myres.getFieldType(i)), xsink);
      switch (myres.getFieldType(i)) {
      case MYSQL_TYPE_TINY:            // TINYINT field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_INT), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("TINYINT"), xsink);
         break;
      case MYSQL_TYPE_SHORT:           // SMALLINT field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_INT), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("SMALLINT"), xsink);
         break;
      case MYSQL_TYPE_LONG:            // INTEGER field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_INT), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("INTEGER"), xsink);
         break;
      case MYSQL_TYPE_INT24:           // MEDIUMINT field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_INT), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("MEDIUMINT"), xsink);
         break;
      case MYSQL_TYPE_LONGLONG:        // BIGINT field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_INT), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("BIGINT"), xsink);
         break;
      case MYSQL_TYPE_DECIMAL:         // DECIMAL or NUMERIC field
      case MYSQL_TYPE_NEWDECIMAL:      // Precision math DECIMAL or NUMERIC
         col->setKeyValue(typestr, new QoreBigIntNode(NT_NUMBER), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("NUMERIC"), xsink);
         break;
      case MYSQL_TYPE_FLOAT:           // FLOAT field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_FLOAT), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("FLOAT"), xsink);
         break;
      case MYSQL_TYPE_DOUBLE:          // DOUBLE or REAL field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_FLOAT), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("DOUBLE"), xsink);
         break;
      case MYSQL_TYPE_BIT:             // BIT field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_INT), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("BIT"), xsink);
         break;
      case MYSQL_TYPE_TIMESTAMP:       // TIMESTAMP field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_DATE), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("TIMESTAMP"), xsink);
         break;
      case MYSQL_TYPE_DATE:            // DATE field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_DATE), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("DATE"), xsink);
         break;
      case MYSQL_TYPE_TIME:            // TIME field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_DATE), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("TIME"), xsink);
         break;
      case MYSQL_TYPE_DATETIME:        // DATETIME field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_DATE), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("DATETIME"), xsink);
         break;
      case MYSQL_TYPE_YEAR:            // YEAR field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_DATE), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("YEAR"), xsink);
         break;
      case MYSQL_TYPE_STRING:          // CHAR or BINARY field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_STRING), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("CHAR"), xsink);
         break;
      case MYSQL_TYPE_VAR_STRING:      // VARCHAR or VARBINARY field
         col->setKeyValue(typestr, new QoreBigIntNode(NT_STRING), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("VARCHAR"), xsink);
         break;
      case MYSQL_TYPE_BLOB:            // BLOB or TEXT field (use max_length to determine the maximum length)
         col->setKeyValue(typestr, new QoreBigIntNode(NT_STRING), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("TEXT"), xsink);
         break;
      case MYSQL_TYPE_SET:             // SET field
      case MYSQL_TYPE_ENUM:            // ENUM field
      case MYSQL_TYPE_GEOMETRY:        // Spatial field
      case MYSQL_TYPE_NULL:            // NULL-type field
      default:
         col->setKeyValue(typestr, new QoreBigIntNode(-1), xsink);
         col->setKeyValue(dbtypestr, new QoreStringNode("n/a"), xsink);
         break;
      } // switch

      h->setKeyValue(myres.getFieldName(i), col.release(), xsink);
      if (*xsink)
         return 0;
   }

   return h.release();
}
#endif

bool QoreMysqlPreparedStatement::next() {
   assert(stmt);
   return !mysql_stmt_fetch(stmt);
}

static int mysql_stmt_api_prepare(SQLStatement* stmt, const QoreString& str, const QoreListNode* args, ExceptionSink* xsink) {
   assert(!stmt->getPrivateData());

   QoreMysqlPreparedStatement* bg = new QoreMysqlPreparedStatement(stmt->getDatasource());
   stmt->setPrivateData(bg);

   return bg->prepare(str, args, true, xsink);
}

static int mysql_stmt_api_prepare_raw(SQLStatement* stmt, const QoreString& str, ExceptionSink* xsink) {
   assert(!stmt->getPrivateData());

   QoreMysqlPreparedStatement* bg = new QoreMysqlPreparedStatement(stmt->getDatasource());
   stmt->setPrivateData(bg);

   return bg->prepare(str, 0, false, xsink);
}

static int mysql_stmt_api_bind(SQLStatement* stmt, const QoreListNode& l, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->bind(l, xsink);
}

static int mysql_stmt_api_bind_placeholders(SQLStatement* stmt, const QoreListNode& l, ExceptionSink* xsink) {
   xsink->raiseException("DBI:PGSQL-BIND-PLACEHHODERS-ERROR", "binding placeholders is not necessary or supported with the pgsql driver");
   return -1;
}

static int mysql_stmt_api_bind_values(SQLStatement* stmt, const QoreListNode& l, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->bind(l, xsink);
}

static int mysql_stmt_api_exec(SQLStatement* stmt, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->exec(xsink);
}

static int mysql_stmt_api_define(SQLStatement* stmt, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->define(xsink);
}

static int mysql_stmt_api_affected_rows(SQLStatement* stmt, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->affectedRows();
}

static QoreHashNode* mysql_stmt_api_get_output(SQLStatement* stmt, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->getOutputHash(xsink);
}

static QoreHashNode* mysql_stmt_api_get_output_rows(SQLStatement* stmt, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->getOutputHash(xsink);
}

static QoreHashNode* mysql_stmt_api_fetch_row(SQLStatement* stmt, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->fetchRow(xsink);
}

static QoreListNode* mysql_stmt_api_fetch_rows(SQLStatement* stmt, int rows, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->fetchRows(rows, xsink);
}

static QoreHashNode* mysql_stmt_api_fetch_columns(SQLStatement* stmt, int rows, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->fetchColumns(rows, xsink);
}

#ifdef _QORE_HAS_DBI_DESCRIBE
static QoreHashNode* mysql_stmt_api_describe(SQLStatement* stmt, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->describe(xsink);
}
#endif

static bool mysql_stmt_api_next(SQLStatement* stmt, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   return bg->next();
}

static int mysql_stmt_api_close(SQLStatement* stmt, ExceptionSink* xsink) {
   QoreMysqlPreparedStatement* bg = (QoreMysqlPreparedStatement*)stmt->getPrivateData();
   assert(bg);

   bg->reset(xsink);
   delete bg;
   stmt->setPrivateData(0);
   return *xsink ? -1 : 0;
}
#endif

#endif // !HAVE_MYSQL_STMT

static AbstractQoreNode* mysql_to_qore(const MYSQL_FIELD& f, char* row, unsigned long len, const QoreMysqlConnection& conn) {
   // some basic type checking
   switch (f.type) {
      // for integer values
      case FIELD_TYPE_SHORT:
      case FIELD_TYPE_LONG:
      case FIELD_TYPE_INT24:
      case FIELD_TYPE_TINY:
         return new QoreBigIntNode(atoi(row));

      // for floating point values
      case FIELD_TYPE_FLOAT:
      case FIELD_TYPE_DOUBLE:
         return new QoreFloatNode(atof(row));

      // for datetime values
      case FIELD_TYPE_DATETIME: {
         row[4]  = '\0';
         row[7]  = '\0';
         row[10] = '\0';
         row[13] = '\0';
         row[16] = '\0';

         return qore_mysql_makedt(conn, atoi(row), atoi(row + 5), atoi(row + 8), atoi(row + 11), atoi(row + 14), atoi(row + 17));
      }

      // for date values
      case FIELD_TYPE_DATE: {
         row[4] = '\0';
         row[7] = '\0';
         return qore_mysql_makedt(conn, atoi(row), atoi(row + 5), atoi(row + 8));
      }

      // for time values
      case FIELD_TYPE_TIME: {
         row[2] = '\0';
         row[5] = '\0';
         return qore_mysql_makedt(conn, 1970, 1, 1, atoi(row), atoi(row + 3), atoi(row + 6));
      }

      case FIELD_TYPE_TIMESTAMP:
#ifdef _QORE_HAS_TIME_ZONES
         return new DateTimeNode(conn.getTZ(), row);
#else
         return new DateTimeNode(row);
#endif

      // process binary values
      case FIELD_TYPE_STRING:
         //printd(5, "charset: %d str: %s\n", f.charsetnr, row);
         if (f.charsetnr == 63)
            return new BinaryNode(row, len);
         break;

      // to avoid warning about unhandled types
      default:
         break;
   }

   // the rest defaults to string
   return new QoreStringNode(row, conn.ds.getQoreEncoding());
}

static QoreHashNode* get_result_set(const QoreMysqlConnection& conn, MYSQL_RES *res, ExceptionSink* xsink, bool single_row = false) {
   MYSQL_ROW row;
   int num_fields = mysql_num_fields(res);
   ReferenceHolder<QoreHashNode> h(new QoreHashNode, xsink);

   // get column names and set up column lists
   MYSQL_FIELD *field = mysql_fetch_fields(res);

   QoreString tstr;
   if (!single_row) {
      for (int i = 0; i < num_fields; i++) {
         get_lower_case_name(&tstr, conn.ds.getQoreEncoding(), field[i].name);
         h->setKeyValue(&tstr, new QoreListNode, xsink);
      }
   }

   int rn = 0;
   unsigned long* lengths = 0;
   while ((row = mysql_fetch_row(res))) {
      if (!lengths) {
	 lengths = mysql_fetch_lengths(res);
	 assert(lengths);
      }
      rn++;
      if (single_row && rn > 1) {
         xsink->raiseException("MYSQL-SELECT-ROW-ERROR", "SQL passed to selectRow() returned more than 1 row");
         return 0;
      }
      for (int i = 0; i < num_fields; i++) {
         AbstractQoreNode* n = mysql_to_qore(field[i], row[i], lengths[i], conn);
         //printd(5, "get_result_set() row %d col %d: %s (type=%d)=\"%s\"\n", rn, i, field[i].name, field[i].type, row[i]);
         if (single_row) {
            get_lower_case_name(&tstr, conn.ds.getQoreEncoding(), field[i].name);
            h->setKeyValue(&tstr, n, xsink);
         }
         else {
            QoreListNode* l = reinterpret_cast<QoreListNode*>(h->getKeyValue(field[i].name));
            l->push(n);
         }
      }
   }

   return h.release();
}

static QoreListNode* get_result_set_horiz(const QoreMysqlConnection& conn, MYSQL_RES* res, ExceptionSink* xsink) {
   MYSQL_ROW row;
   int num_fields = mysql_num_fields(res);
   ReferenceHolder<QoreListNode> l(new QoreListNode, xsink);

   // get column names and set up column lists
   MYSQL_FIELD *field = mysql_fetch_fields(res);

   QoreString tstr;

   int rn = 0;
   unsigned long* lengths = 0;
   while ((row = mysql_fetch_row(res))) {
      if (!lengths) {
	 lengths = mysql_fetch_lengths(res);
	 assert(lengths);
      }
      rn++;
      ReferenceHolder<QoreHashNode> h(new QoreHashNode, xsink);

      for (int i = 0; i < num_fields; i++) {
         get_lower_case_name(&tstr, conn.ds.getQoreEncoding(), field[i].name);
         h->setKeyValue(&tstr, mysql_to_qore(field[i], row[i], lengths[i], conn), xsink);
      }
      // add row to output
      l->push(h.release());
   }

   return l.release();
}

static AbstractQoreNode* qore_mysql_do_sql(const QoreMysqlConnection& conn, const QoreString* qstr, const QoreListNode* args, ExceptionSink* xsink, bool horiz = false) {
   QORE_TRACE("qore_mysql_do_sql()");

   TempEncodingHelper tqstr(qstr, conn.ds.getQoreEncoding(), xsink);
   if (!tqstr)
      return 0;

   if (mysql_query(conn.db, tqstr->getBuffer())) {
      xsink->raiseException("DBI:MYSQL:SELECT-ERROR", (const char*)mysql_error(conn.db));
      return 0;
   }

   if (!mysql_field_count(conn.db))
      return new QoreBigIntNode(mysql_affected_rows(conn.db));

   MYSQL_RES* res = mysql_store_result(conn.db);
   if (!res) {
      xsink->raiseException("DBI:MYSQL:SELECT-ERROR", (const char*)mysql_error(conn.db));
      return 0;
   }
   ON_BLOCK_EXIT(mysql_free_result, res);

   return horiz
      ? (AbstractQoreNode*)get_result_set_horiz(conn, res, xsink)
      : (AbstractQoreNode*)get_result_set(conn, res, xsink);
}

static QoreHashNode* qore_mysql_do_select_row(const QoreMysqlConnection& conn, const QoreString* qstr, const QoreListNode* args, ExceptionSink* xsink) {
   QORE_TRACE("qore_mysql_do_select_row()");

   TempEncodingHelper tqstr(qstr, conn.ds.getQoreEncoding(), xsink);
   if (!tqstr)
      return 0;

   if (mysql_query(conn.db, tqstr->getBuffer())) {
      xsink->raiseException("DBI:MYSQL:SELECT-ERROR", (char *)mysql_error(conn.db));
      return 0;
   }

   if (!mysql_field_count(conn.db))
      return 0;

   MYSQL_RES* res = mysql_store_result(conn.db);
   if (!res) {
      xsink->raiseException("DBI:MYSQL:SELECT-ERROR", (char *)mysql_error(conn.db));
      return 0;
   }
   ON_BLOCK_EXIT(mysql_free_result, res);

   return get_result_set(conn, res, xsink, true);
}

static AbstractQoreNode* qore_mysql_select_rows(Datasource *ds, const QoreString* qstr, const QoreListNode* args, ExceptionSink* xsink) {
   const QoreMysqlConnection& conn = *((QoreMysqlConnection*)ds->getPrivateData());
   check_init();
#ifdef HAVE_MYSQL_STMT
   QoreMysqlBindGroupHelper bg(ds, xsink);
   int rc = bg.prepareAndBind(qstr, args, xsink);
   if (rc == -1)
      return 0;

   if (rc == 1)
      return qore_mysql_do_sql(conn, qstr, args, xsink, true);

   return bg.selectRows(xsink);
#else
   return qore_mysql_do_sql(conn, qstr, args, xsink, true);
#endif
}

#ifdef _QORE_HAS_DBI_SELECT_ROW
static QoreHashNode* qore_mysql_select_row(Datasource *ds, const QoreString* qstr, const QoreListNode* args, ExceptionSink* xsink) {
   const QoreMysqlConnection& conn = *((QoreMysqlConnection*)ds->getPrivateData());
   check_init();
#ifdef HAVE_MYSQL_STMT
   QoreMysqlBindGroupHelper bg(ds, xsink);
   int rc = bg.prepareAndBind(qstr, args, xsink);
   if (rc == -1)
      return 0;

   if (rc == 1)
      return qore_mysql_do_select_row(conn, qstr, args, xsink);

   return bg.selectRow(xsink);
#else
   return qore_mysql_do_sql(conn, qstr, args, xsink, true, true);
#endif
}
#endif

static AbstractQoreNode* qore_mysql_select(Datasource *ds, const QoreString* qstr, const QoreListNode* args, ExceptionSink* xsink) {
   const QoreMysqlConnection& conn = *((QoreMysqlConnection*)ds->getPrivateData());
   check_init();
#ifdef HAVE_MYSQL_STMT
   QoreMysqlBindGroupHelper bg(ds, xsink);
   int rc = bg.prepareAndBind(qstr, args, xsink);
   if (rc == -1)
      return 0;

   if (rc == 1)
      return qore_mysql_do_sql(conn, qstr, args, xsink);

   return bg.exec(xsink);
#else
   return qore_mysql_do_sql(conn, qstr, args, xsink);
#endif
}

static AbstractQoreNode* qore_mysql_exec(Datasource *ds, const QoreString* qstr, const QoreListNode* args, ExceptionSink* xsink) {
   const QoreMysqlConnection& conn = *((QoreMysqlConnection*)ds->getPrivateData());
   check_init();
#ifdef HAVE_MYSQL_STMT
   QoreMysqlBindGroupHelper bg(ds, xsink);
   int rc = bg.prepareAndBind(qstr, args, xsink);
   if (rc == -1)
      return 0;

   if (rc == 1)
      return qore_mysql_do_sql(conn, qstr, args, xsink);

   return bg.exec(xsink);
#else
   return qore_mysql_do_sql(conn, qstr, args, xsink);
#endif
}

#ifdef _QORE_HAS_DBI_EXECRAW
static AbstractQoreNode* qore_mysql_execRaw(Datasource *ds, const QoreString* qstr, ExceptionSink* xsink) {
   const QoreMysqlConnection& conn = *((QoreMysqlConnection*)ds->getPrivateData());
   check_init();
   return qore_mysql_do_sql(conn, qstr, 0, xsink);
}
#endif

static int qore_mysql_open_datasource(Datasource* ds, ExceptionSink* xsink) {
   check_init();

   MYSQL *db = qore_mysql_init(ds, xsink);
   if (!db)
      return -1;

   QoreMysqlConnection *d_mysql = new QoreMysqlConnection(db, *ds);
   ds->setPrivateData((void*)d_mysql);

   return 0;
}

static int qore_mysql_close_datasource(Datasource* ds) {
   QORE_TRACE("qore_mysql_close_datasource()");

   check_init();

   QoreMysqlConnection *d_mysql = (QoreMysqlConnection*)ds->getPrivateData();

   printd(3, "qore_mysql_close_datasource(): connection to %s closed.\n", ds->getDBName());

   delete d_mysql;
   ds->setPrivateData(NULL);

   return 0;
}

static AbstractQoreNode* qore_mysql_get_server_version(Datasource* ds, ExceptionSink* xsink) {
   check_init();
   QoreMysqlConnection *d_mysql = (QoreMysqlConnection*)ds->getPrivateData();
   return new QoreBigIntNode(d_mysql->getServerVersion());
}

static AbstractQoreNode* qore_mysql_get_client_version(const Datasource* ds, ExceptionSink* xsink) {
   check_init();
   return new QoreBigIntNode(mysql_get_client_version());
}

#ifdef _QORE_HAS_DBI_OPTIONS
static int mysql_opt_set(Datasource* ds, const char* opt, const AbstractQoreNode* val, ExceptionSink* xsink) {
   QoreMysqlConnection* mc = (QoreMysqlConnection*)ds->getPrivateData();
   return mc->setOption(opt, val, xsink);
}

static AbstractQoreNode* mysql_opt_get(const Datasource* ds, const char* opt) {
   QoreMysqlConnection* mc = (QoreMysqlConnection*)ds->getPrivateData();
   return mc->getOption(opt);
}
#endif

QoreStringNode* qore_mysql_module_init() {
   // initialize thread key to test for mysql_thread_init()
   pthread_key_create(&ptk_mysql, NULL);
   tclist.push(mysql_thread_cleanup, NULL);

#ifdef HAVE_MYSQL_LIBRARY_INIT
   mysql_library_init(0, 0, 0);
#else
   mysql_server_init(0, 0, 0);
#endif

   // populate the method list structure with the method pointers
   qore_dbi_method_list methods;
   methods.add(QDBI_METHOD_OPEN,               qore_mysql_open_datasource);
   methods.add(QDBI_METHOD_CLOSE,              qore_mysql_close_datasource);
   methods.add(QDBI_METHOD_SELECT,             qore_mysql_select);
   methods.add(QDBI_METHOD_SELECT_ROWS,        qore_mysql_select_rows);
#ifdef _QORE_HAS_DBI_SELECT_ROW
   methods.add(QDBI_METHOD_SELECT_ROW,         qore_mysql_select_row);
#endif
   methods.add(QDBI_METHOD_EXEC,               qore_mysql_exec);
#ifdef _QORE_HAS_DBI_EXECRAW
   methods.add(QDBI_METHOD_EXECRAW,            qore_mysql_execRaw);
#endif
   methods.add(QDBI_METHOD_COMMIT,             qore_mysql_commit);
   methods.add(QDBI_METHOD_ROLLBACK,           qore_mysql_rollback);
   methods.add(QDBI_METHOD_GET_SERVER_VERSION, qore_mysql_get_server_version);
   methods.add(QDBI_METHOD_GET_CLIENT_VERSION, qore_mysql_get_client_version);

#if defined(HAVE_MYSQL_STMT) && defined(_QORE_HAS_PREPARED_STATMENT_API)
   methods.add(QDBI_METHOD_STMT_PREPARE, mysql_stmt_api_prepare);
   methods.add(QDBI_METHOD_STMT_PREPARE_RAW, mysql_stmt_api_prepare_raw);
   methods.add(QDBI_METHOD_STMT_BIND, mysql_stmt_api_bind);
   methods.add(QDBI_METHOD_STMT_BIND_PLACEHOLDERS, mysql_stmt_api_bind_placeholders);
   methods.add(QDBI_METHOD_STMT_BIND_VALUES, mysql_stmt_api_bind_values);
   methods.add(QDBI_METHOD_STMT_EXEC, mysql_stmt_api_exec);
   methods.add(QDBI_METHOD_STMT_DEFINE, mysql_stmt_api_define);
   methods.add(QDBI_METHOD_STMT_FETCH_ROW, mysql_stmt_api_fetch_row);
   methods.add(QDBI_METHOD_STMT_FETCH_ROWS, mysql_stmt_api_fetch_rows);
   methods.add(QDBI_METHOD_STMT_FETCH_COLUMNS, mysql_stmt_api_fetch_columns);
#ifdef _QORE_HAS_DBI_DESCRIBE
   methods.add(QDBI_METHOD_STMT_DESCRIBE, mysql_stmt_api_describe);
#endif
   methods.add(QDBI_METHOD_STMT_NEXT, mysql_stmt_api_next);
   methods.add(QDBI_METHOD_STMT_CLOSE, mysql_stmt_api_close);
   methods.add(QDBI_METHOD_STMT_AFFECTED_ROWS, mysql_stmt_api_affected_rows);
   methods.add(QDBI_METHOD_STMT_GET_OUTPUT, mysql_stmt_api_get_output);
   methods.add(QDBI_METHOD_STMT_GET_OUTPUT_ROWS, mysql_stmt_api_get_output_rows);
#endif

#if defined(HAVE_MYSQL_STMT) && defined(_QORE_HAS_DBI_OPTIONS)
   methods.add(QDBI_METHOD_OPT_SET, mysql_opt_set);
   methods.add(QDBI_METHOD_OPT_GET, mysql_opt_get);

   methods.registerOption(DBI_OPT_NUMBER_OPT, "when set, numeric/decimal values are returned as integers if possible, otherwise as arbitrary-precision number values; the argument is ignored; setting this option turns it on and turns off 'string-numbers' and 'numeric-numbers'");
   methods.registerOption(DBI_OPT_NUMBER_STRING, "when set, numeric/decimal values are returned as strings for backwards-compatibility; the argument is ignored; setting this option turns it on and turns off 'optimal-numbers' and 'numeric-numbers'");
   methods.registerOption(DBI_OPT_NUMBER_NUMERIC, "when set, numeric/decimal values are returned as arbitrary-precision number values; the argument is ignored; setting this option turns it on and turns off 'string-numbers' and 'optimal-numbers'");
   methods.registerOption(DBI_OPT_TIMEZONE, "set the server-side timezone, value must be a string in the format accepted by Timezone::constructor() on the client (ie either a region name or a UTC offset like \"+01:00\"), if not set the server's time zone will be assumed to be the same as the client's", stringTypeInfo);
#endif

   // register database functions with DBI subsystem
   DBID_MYSQL = DBI.registerDriver("mysql", methods, mysql_caps);

   return 0;
}

void qore_mysql_module_ns_init(QoreNamespace *rns, QoreNamespace *qns) {
   QORE_TRACE("qore_mysql_module_ns_init()");
   // nothing to do at the moment
}

void qore_mysql_module_delete() {
   QORE_TRACE("qore_mysql_module_delete()");

   //printf("mysql delete\n");

   // cleanup any thread data
   tclist.pop(1);
   // delete thread key
   pthread_key_delete(ptk_mysql);

#ifdef HAVE_MYSQL_LIBRARY_INIT
   mysql_library_end();
#endif
}
