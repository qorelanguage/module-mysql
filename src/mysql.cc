/*
  mysql.cc

  MySQL Interface to Qore DBI layer

  Qore Programming Language

  0.4.0 changes: 
  * multi-threaded access added
  * transaction management added
  * character set support added

  Copyright 2003 - 2009 David Nichols

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

#ifndef QORE_MONOLITHIC
DLLEXPORT char qore_module_name[] = "mysql";
DLLEXPORT char qore_module_version[] = PACKAGE_VERSION;
DLLEXPORT char qore_module_description[] = "MySQL database driver";
DLLEXPORT char qore_module_author[] = "David Nichols";
DLLEXPORT char qore_module_url[] = "http://www.qoretechnologies.com/qore";
DLLEXPORT int qore_module_api_major = QORE_MODULE_API_MAJOR;
DLLEXPORT int qore_module_api_minor = QORE_MODULE_API_MINOR;
DLLEXPORT qore_module_init_t qore_module_init = qore_mysql_module_init;
DLLEXPORT qore_module_ns_init_t qore_module_ns_init = qore_mysql_module_ns_init;
DLLEXPORT qore_module_delete_t qore_module_delete = qore_mysql_module_delete;
#ifdef HAVE_MYSQL_CLIENT_LICENSE
DLLEXPORT qore_license_t qore_module_license = QL_LGPL;
#else
DLLEXPORT qore_license_t qore_module_license = QL_GPL;
#endif
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
#endif
;

DBIDriver *DBID_MYSQL = 0;

// this is the thread key that will tell us if the current thread has been initialized for mysql threading
static pthread_key_t ptk_mysql;

static struct mapEntry {
      char *mysql;
      const QoreEncoding *id;
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

static const QoreEncoding *get_qore_cs(char *cs) {
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

static char *get_mysql_cs(const QoreEncoding *id) {
   for (unsigned i = 0; i < NUM_CHARMAPS; i++)
      if (mapList[i].id == id)
	 return mapList[i].mysql;

   return NULL;
}

static inline void checkInit() {
   if (!pthread_getspecific(ptk_mysql)) {
      mysql_thread_init();
      pthread_setspecific(ptk_mysql, (void *)1);
   }
}

static void mysql_thread_cleanup(void *unused) {
   if (pthread_getspecific(ptk_mysql))
      mysql_thread_end();
}

static MYSQL *qore_mysql_init(Datasource *ds, ExceptionSink *xsink) {   
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
   
   printd(3, "qore_mysql_init(): user=%s pass=%s db=%s (encoding=%s)\n",
	  ds->getUsername(), ds->getPassword(), ds->getDBName(), ds->getDBEncoding() ? ds->getDBEncoding() : "(none)");

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
   // autocommits are handled by qore, not by MySQL
   mysql_autocommit(db, false);
   
   // set transaction handling
   if (mysql_query(db, "set transaction isolation level read committed")) {
      xsink->raiseException("DBI:MYSQL:INIT-ERROR", (char *)mysql_error(db));
      mysql_close(db);
      return NULL;
   }
   
#endif
   
   return db;
}

static int qore_mysql_commit(Datasource *ds, ExceptionSink *xsink) {
#ifdef HAVE_MYSQL_COMMIT
   checkInit();
   QoreMySQLConnection *d_mysql =(QoreMySQLConnection *)ds->getPrivateData();

   // calls mysql_commit() on the connection
   if (d_mysql->commit()) {
      xsink->raiseException("DBI:MYSQL:COMMIT-ERROR", d_mysql->error());
      return -1;
   }
   return 0;
#else
   xsink->raiseException("DBI:MYSQL:NOT-IMPLEMENTED", "this version of the MySQL client API does not support transaction management");
   return -1;
#endif
}

static int qore_mysql_rollback(Datasource *ds, ExceptionSink *xsink) {
#ifdef HAVE_MYSQL_COMMIT
   checkInit();
   QoreMySQLConnection *d_mysql =(QoreMySQLConnection *)ds->getPrivateData();

   // calls mysql_rollback() on the connection
   if (d_mysql->rollback()) {
      xsink->raiseException("DBI:MYSQL:ROLLBACK-ERROR", d_mysql->error());
      return -1;
   }
   return 0;
#else
   xsink->raiseException("DBI:MYSQL:NOT-IMPLEMENTED", "this version of the MySQL client API does not support transaction management");
   return -1;
#endif
}

static void getLowerCaseName(QoreString *str, const QoreEncoding *enc, const char *name) {
   str->set(name, enc);
   str->tolwr();
}

#ifdef HAVE_MYSQL_STMT
void MyResult::bind(MYSQL_STMT *stmt) {
   bindbuf = new MYSQL_BIND[num_fields];
   bi      = new bindInfo[num_fields];

   // zero out bind memory
   memset(bindbuf, 0, sizeof(MYSQL_BIND) * num_fields);

   for (int i = 0; i < num_fields; i++)
   {
      // setup bind structure
      //printd(5, "%d type=%d (%d %d %d)\n", field[i].type, FIELD_TYPE_TINY_BLOB, FIELD_TYPE_MEDIUM_BLOB, FIELD_TYPE_BLOB); 
      switch (field[i].type)
      {
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
	    // this is only binary data if charsetnr == 63
	    if (field[i].charsetnr == 63)
	    {
	       bindbuf[i].buffer_type = MYSQL_TYPE_BLOB;
	       bindbuf[i].buffer = new char[field[i].length];
	       bindbuf[i].buffer_length = field[i].length;
	       break;
	    }

	    // for all other types (treated as string)
	 default:
	    bindbuf[i].buffer_type = MYSQL_TYPE_STRING;
	    bindbuf[i].buffer = new char[field[i].length + 1];
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

AbstractQoreNode *MyResult::getBoundColumnValue(const QoreEncoding *csid, int i) {
   AbstractQoreNode *n = NULL;
   
   if (bi[i].mnull)
      n = null();
   else if (bindbuf[i].buffer_type == MYSQL_TYPE_LONGLONG)
      n = new QoreBigIntNode(*((int64 *)bindbuf[i].buffer));
   else if (bindbuf[i].buffer_type == MYSQL_TYPE_DOUBLE)
      n = new QoreFloatNode(*((double *)bindbuf[i].buffer));
   else if (bindbuf[i].buffer_type == MYSQL_TYPE_STRING) {
      //printf("string (%d): '%s'\n", mlen[i], (char *)bindbuf[i].buffer);
      n = new QoreStringNode((const char *)bindbuf[i].buffer, csid);
   }
   else if (bindbuf[i].buffer_type == MYSQL_TYPE_DATETIME) {
      MYSQL_TIME *t = (MYSQL_TIME *)bindbuf[i].buffer;
      n = new DateTimeNode(t->year, t->month, t->day, t->hour, t->minute, t->second);
   }
   else if (bindbuf[i].buffer_type == MYSQL_TYPE_BLOB)
      n = new BinaryNode(bindbuf[i].buffer, *bindbuf[i].length);

   return n;
}

QoreMySQLBindGroup::QoreMySQLBindGroup(Datasource *ods) {
   head = tail = NULL;
   stmt = NULL;
   hasOutput = false;
   bind = NULL;
   len = 0;
   ds = ods;
   mydata = (QoreMySQLConnection *)ds->getPrivateData();
}

QoreMySQLBindGroup::~QoreMySQLBindGroup() {
   if (bind)
      delete [] bind;

   if (stmt)
      mysql_stmt_close(stmt);

   if (str)
      delete str;

   QoreMySQLBindNode *w = head;
   while (w) {
      
      head = w->next;
      delete w;
      w = head;
   }
}

int QoreMySQLBindGroup::prepare_and_bind(const QoreString *ostr, const QoreListNode *args, ExceptionSink *xsink) {
   // create copy of string and convert encoding if necessary
   str = ostr->convertEncoding(ds->getQoreEncoding(), xsink);
   if (!str)
      return -1;

   // parse query and bind variables/placeholders, return on error
   if (parse(args, xsink))
      return -1;

   stmt = mydata->stmt_init(xsink);
   if (!stmt)
      return -1;

   //printd(5, "mysql prepare: (%d) %s\n", str->strlen(), str->getBuffer());

   // prepare the statement for execution
   if (mysql_stmt_prepare(stmt, str->getBuffer(), str->strlen())) {
      int en = mydata->q_errno();
      if (en != CR_SERVER_GONE_ERROR) {
	 if (en == ER_UNSUPPORTED_PS)
	    return 1;
	 xsink->raiseException("DBI:MYSQL:STATEMENT-ERROR", "error %d: %s", en, mydata->error());
	 return -1;
      }

      if (mydata->reconnect(ds, stmt, str, xsink))
	 return -1;
   }

   // if there is data to bind, then bind it
   if (len) {
      // allocate bind buffer
      bind = new MYSQL_BIND[len];
      // zero out bind memory
      memset(bind, 0, sizeof(MYSQL_BIND) * len);

      // bind all values/placeholders
      QoreMySQLBindNode *w = head;
      int pos = 0;
      while (w) {
	 printd(5, "MBG::MBG() binding value at position %d (%s)\n", pos, w->data.value ? w->data.value->getTypeName() : "<null>");
	 if (w->bindValue(ds->getQoreEncoding(), &bind[pos], xsink))
	    return -1;
	 pos++;
	 w = w->next;
      }
   }
   // now perform the bind
   if (mysql_stmt_bind_param(stmt, bind)) {
      xsink->raiseException("DBI:MYSQL-ERROR", "error %d: %s", mydata->q_errno(), mydata->error());
      return -1;
   }

   return 0;
}

inline int QoreMySQLBindGroup::parse(const QoreListNode *args, ExceptionSink *xsink) {
   char quote = 0;

   const char *p = str->getBuffer();
   int index = 0;
   QoreString tmp(ds->getQoreEncoding());
   while (*p) {
      if (!quote && (*p) == '%') { // found value marker
	 const AbstractQoreNode *v = args ? args->retrieve_entry(index++) : NULL;
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
	    xsink->raiseException("DBI-EXEC-PARSE-EXCEPTION", "invalid value specification (expecting '%v' or '%%d', got %%%c)", *p);
	    return -1;
	 }
	 p++;
	 if (isalpha(*p)) {
	    xsink->raiseException("DBI-EXEC-PARSE-EXCEPTION", "invalid value specification (expecting '%v' or '%%d', got %%v%c*)", *p);
	    return -1;
	 }

	 // replace value marker with "?"
	 // find byte offset in case string buffer is reallocated with replace()
	 str->replace(offset, 2, "?");
	 p = str->getBuffer() + offset;

	 printd(5, "QoreMySQLBindGroup::parse() newstr=%s\n", str->getBuffer());
	 printd(5, "QoreMySQLBindGroup::parse() adding value type=%s\n",v ? v->getTypeName() : "<NULL>");
	 add(v);
      }
      else if (!quote && (*p) == ':') { // found placeholder marker
	 const char *w = p;

	 p++;
	 if (!isalpha(*p))
	    continue;

	 // get placeholder name
	 QoreString tstr;
	 while (isalnum(*p) || (*p) == '_')
	    tstr.concat(*(p++));

	 printd(5, "QoreMySQLBindGroup::parse() adding placeholder for '%s'\n", tstr.getBuffer());
	 add(tstr.giveBuffer());

	 // substitute "@" for ":" in bind name
	 // find byte position of start of string
	 int offset = w - str->getBuffer();
	 str->replace(offset, 1, "@");

	 printd(5, "QoreMySQLBindGroup::parse() offset=%d, new str=%s\n", offset, str->getBuffer());
      }
      else if (((*p) == '\'') || ((*p) == '\"')) {
	 if (!quote)
	    quote = *p;
	 else if (quote == (*p))
	    quote = 0;
	 p++;
      }
      else
	 p++;
   }

   return 0;
}

inline AbstractQoreNode *QoreMySQLBindGroup::getOutputHash(ExceptionSink *xsink) {
   ReferenceHolder<QoreHashNode> h(new QoreHashNode(), xsink);

   cstr_vector_t::iterator sli = phl.begin();
   while (sli != phl.end()) {
      // prepare statement to retrieve values
      mysql_stmt_close(stmt);
      stmt = mydata->stmt_init(xsink);
      if (!stmt)
	 return 0;
      
      QoreString qstr;
      qstr.sprintf("select @%s", *sli);

      // prepare the statement for execution
      if (mysql_stmt_prepare(stmt, qstr.getBuffer(), qstr.strlen())) {
	 xsink->raiseException("DBI:MYSQL:ERROR", mydata->error());
	 return 0;
      }

      AbstractQoreNode *v = NULL;

      MYSQL_RES *res = mysql_stmt_result_metadata(stmt);
      if (res) {
	 MyResult myres(res);

	 if (mysql_stmt_execute(stmt)) {
	    xsink->raiseException("DBI:MYSQL:ERROR", mydata->error());
	    return NULL;
	 }

	 int rows = mysql_stmt_affected_rows(stmt);
	 if (rows) {
	    myres.bind(stmt);

	    if (rows > 1) {
	       QoreListNode *l = new QoreListNode();
	       while (!mysql_stmt_fetch(stmt))
		  l->push(myres.getBoundColumnValue(ds->getQoreEncoding(), 0));
	       v = l;
	    }
	    else
	    {
	       mysql_stmt_fetch(stmt);
	       v = myres.getBoundColumnValue(ds->getQoreEncoding(), 0);
	    }
	 }
      }

      h->setKeyValue(*sli, v, xsink);
      sli++;
   }
   return h.release();
}

AbstractQoreNode *QoreMySQLBindGroup::execIntern(ExceptionSink *xsink) {
   AbstractQoreNode *rv = NULL;
   MYSQL_RES *res = mysql_stmt_result_metadata(stmt);
   if (res) {
      MyResult myres(res);

      if (mysql_stmt_execute(stmt)) {
	 xsink->raiseException("DBI:MYSQL:ERROR", mydata->error());
	 return NULL;
      }

      QoreHashNode *h = new QoreHashNode();
      const QoreEncoding *enc = ds->getQoreEncoding();
      QoreString tstr;
	 
      for (int i = 0; i < myres.getNumFields(); i++) {
	 getLowerCaseName(&tstr, enc, myres.getFieldName(i));
	 h->setKeyValue(&tstr, new QoreListNode(), xsink);
      }
	 
      if (mysql_stmt_affected_rows(stmt)) {
	 myres.bind(stmt);
	    
	 while (!mysql_stmt_fetch(stmt)) {
	    HashIterator hi(h);
	    int i = 0;
	    while (hi.next()) {
	       QoreListNode *l = reinterpret_cast<QoreListNode *>(hi.getValue());
	       l->push(myres.getBoundColumnValue(enc, i++));
	    }
	 }
      }
      rv = h;
   }
   else {
      // there is no result set
      if (mysql_stmt_execute(stmt)) {
	 xsink->raiseException("DBI:MYSQL:ERROR", mydata->error());
	 return NULL;
      }

      if (!hasOutput)
	 rv = new QoreBigIntNode((int64)mysql_stmt_affected_rows(stmt));
      else
	 rv = getOutputHash(xsink);
   }

   return rv;
}

inline AbstractQoreNode *QoreMySQLBindGroup::exec(ExceptionSink *xsink) {
   return execIntern(xsink);
}

inline AbstractQoreNode *QoreMySQLBindGroup::select(ExceptionSink *xsink) {
   return execIntern(xsink);
}

AbstractQoreNode *QoreMySQLBindGroup::selectRows(ExceptionSink *xsink) {
   AbstractQoreNode *rv = NULL;
   MYSQL_RES *res = mysql_stmt_result_metadata(stmt);
   if (res) {
      MyResult myres(res);

      if (mysql_stmt_execute(stmt)) {
	 xsink->raiseException("DBI:MYSQL:ERROR", mydata->error());
	 return NULL;
      }

      QoreListNode *l = new QoreListNode();

      if (mysql_stmt_affected_rows(stmt)) {
	 myres.bind(stmt);

	 QoreString tstr;
	 const QoreEncoding *enc = ds->getQoreEncoding();

	 while (!mysql_stmt_fetch(stmt)) {
	    QoreHashNode *h = new QoreHashNode();

	    for (int i = 0; i < myres.getNumFields(); i++) {
	       getLowerCaseName(&tstr, enc, myres.getFieldName(i));
	       h->setKeyValue(&tstr, myres.getBoundColumnValue(enc, i), xsink);
	    }

	    l->push(h);
	 }
      }

      rv = l;
   }
   else {
      // there is no result set
      if (mysql_stmt_execute(stmt)) {
	 xsink->raiseException("DBI:MYSQL:ERROR", mydata->error());
	 return NULL;
      }

      if (!hasOutput)
	 rv = new QoreBigIntNode((int64)mysql_stmt_affected_rows(stmt));
      else
	 rv = getOutputHash(xsink);
   }

   return rv;
}

int QoreMySQLBindNode::bindValue(const QoreEncoding *enc, MYSQL_BIND *buf, ExceptionSink *xsink) {
   //printd(5, "QoreMySQLBindNode::bindValue() type=%s\n", data.value ? data.value->getTypeName() : "NOTHING");

   // bind a NULL value
   if (is_nothing(data.value) || is_null(data.value)) {
      buf->buffer_type = MYSQL_TYPE_NULL;
      return 0;
   }

   qore_type_t ntype = data.value->getType();

   if (ntype == NT_STRING) {
      QoreStringNode *bstr = const_cast<QoreStringNode *>(reinterpret_cast<const QoreStringNode *>(data.value));
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

   if (ntype == NT_DATE)
   {
      const DateTimeNode *date = reinterpret_cast<const DateTimeNode *>(data.value);
      vbuf.assign(date);
      
      buf->buffer_type = MYSQL_TYPE_DATETIME;
      buf->buffer = &vbuf.time;
      return 0;
   }

   
   if (ntype == NT_BINARY)
   {
      const BinaryNode *b = reinterpret_cast<const BinaryNode *>(data.value);
      len = b->size();
      buf->buffer_type = MYSQL_TYPE_BLOB;
      buf->buffer = (void *)b->getPtr();
      buf->buffer_length = len;
      buf->length = &len;
      return 0;
   }

   if (ntype == NT_BOOLEAN)
   {
      vbuf.i4 = reinterpret_cast<const QoreBoolNode *>(data.value)->getValue();
      buf->buffer_type = MYSQL_TYPE_LONG;
      buf->buffer = (char *)&vbuf.i4;
      return 0;
   }
   
   if (ntype == NT_INT)
   {
      buf->buffer_type = MYSQL_TYPE_LONGLONG;
      buf->buffer = (char *)&(reinterpret_cast<const QoreBigIntNode *>(data.value))->val;
      return 0;
   }

   if (ntype == NT_FLOAT)
   {
      buf->buffer_type = MYSQL_TYPE_DOUBLE;
      buf->buffer = (char *)&(reinterpret_cast<const QoreFloatNode *>(data.value)->f);
      return 0;
   }

   xsink->raiseException("DBI-EXEC-EXCEPTION", "type '%s' is not supported for SQL binding", data.value->getTypeName());
   return -1;
}

#endif // !HAVE_MYSQL_STMT

static QoreHashNode *get_result_set(const Datasource *ds, MYSQL_RES *res, ExceptionSink *xsink) {
   MYSQL_ROW row;
   int num_fields = mysql_num_fields(res);
   QoreHashNode *h = new QoreHashNode();
   
   // get column names and set up column lists
   MYSQL_FIELD *field = mysql_fetch_fields(res);

   QoreString tstr;
   for (int i = 0; i < num_fields; i++) {
      getLowerCaseName(&tstr, ds->getQoreEncoding(), field[i].name);
      h->setKeyValue(&tstr, new QoreListNode(), xsink);
   }
   
   int rn = 0;
   while ((row = mysql_fetch_row(res))) {
      for (int i = 0; i < num_fields; i++) {
	 AbstractQoreNode *n;
	 // some basic type checking
	 switch (field[i].type) {
	    // for integer values
	    case FIELD_TYPE_SHORT:
	    case FIELD_TYPE_LONG:
	    case FIELD_TYPE_INT24:
	    case FIELD_TYPE_TINY:
	       n = new QoreBigIntNode(atoi(row[i]));
	       break;
	       
	       // for floating point values
	    case FIELD_TYPE_FLOAT:
	    case FIELD_TYPE_DOUBLE:
	       n = new QoreFloatNode(atof(row[i]));
	       break;
	       
	       // for datetime values
	    case FIELD_TYPE_DATETIME: {
	       row[i][4]  = '\0';
	       row[i][7]  = '\0';
	       row[i][10] = '\0';
	       row[i][13] = '\0';
	       row[i][16] = '\0';

	       n = new DateTimeNode(atoi(row[i]), atoi(row[i] + 5), atoi(row[i] + 8), atoi(row[i] + 11), atoi(row[i] + 14), atoi(row[i] + 17));
	       break;
	    }

	    // for date values
	    case FIELD_TYPE_DATE: {
	       row[i][4] = '\0';
	       row[i][7] = '\0';
	       n = new DateTimeNode(atoi(row[i]), atoi(row[i] + 5), atoi(row[i] + 8), 0, 0, 0);
	       break;
	    }
	    
	    // for time values
	    case FIELD_TYPE_TIME: {
	       row[i][2] = '\0';
	       row[i][5] = '\0';
	       n = new DateTimeNode(0, 0, 0, atoi(row[i]), atoi(row[i] + 3), atoi(row[i] + 6));
	       break;
	    }

	    case FIELD_TYPE_TIMESTAMP:
	       n = new DateTimeNode(row[i]);
	       break;
	    
	    // the rest defaults to string
	    default:
	       n = new QoreStringNode(row[i], ds->getQoreEncoding());
	       break;
	 }
	 //printd(5, "get_result_set() row %d col %d: %s (type=%d)=\"%s\"\n", rn, i, field[i].name, field[i].type, row[i]);
	 QoreListNode *l = reinterpret_cast<QoreListNode *>(h->getKeyValue(field[i].name));
	 l->push(n);
      }
      rn++;
   }
   return h;
}

static AbstractQoreNode *qore_mysql_do_sql(const Datasource *ds, const QoreString *qstr, const QoreListNode *args, ExceptionSink *xsink) {
   QORE_TRACE("qore_mysql_do_sql()");

   TempEncodingHelper tqstr(qstr, ds->getQoreEncoding(), xsink);
   if (!tqstr)
      return NULL;
   
   QoreMySQLConnection *d_mysql =(QoreMySQLConnection *)ds->getPrivateData();
   MYSQL *db = d_mysql->db;
   
   if (mysql_query(db, tqstr->getBuffer())) {
      xsink->raiseException("DBI:MYSQL:SELECT-ERROR", (char *)mysql_error(db));
      return NULL;
   }

   AbstractQoreNode *rv;
   if (mysql_field_count(db) > 0) {
      MYSQL_RES *res = mysql_store_result(db);

      if (!res) {
	 xsink->raiseException("DBI:MYSQL:SELECT-ERROR", (char *)mysql_error(db));
	 return NULL;
      }
      rv = get_result_set(ds, res, xsink);
      mysql_free_result(res);
   }
   else {
      rv = new QoreBigIntNode(mysql_affected_rows(db));
   }
#ifdef HAVE_MYSQL_COMMIT
   if (ds->getAutoCommit())
      mysql_commit(db);
#endif

   return rv;
}

static AbstractQoreNode *qore_mysql_do_sql_horizontal(const Datasource *ds, const QoreString *qstr, const QoreListNode *args, ExceptionSink *xsink) {
   xsink->raiseException("MYSQL-UNSUPPORTED", "row retrieval not yet implemented for executing MySQL statements without the prepared statement interface");
   return 0;
}

static AbstractQoreNode *qore_mysql_select_rows(Datasource *ds, const QoreString *qstr, const QoreListNode *args, ExceptionSink *xsink) {
   checkInit();
#ifdef HAVE_MYSQL_STMT
   QoreMySQLBindGroup bg(ds);
   int rc = bg.prepare_and_bind(qstr, args, xsink);
   if (rc == -1)
      return 0;

   if (rc == 1)
      return qore_mysql_do_sql_horizontal(ds, qstr, args, xsink);

   return bg.selectRows(xsink);
#else
   return qore_mysql_do_sql_horizontal(ds, qstr, args, xsink);
#endif
}

static AbstractQoreNode *qore_mysql_select(Datasource *ds, const QoreString *qstr, const QoreListNode *args, ExceptionSink *xsink) {
   checkInit();
#ifdef HAVE_MYSQL_STMT
   QoreMySQLBindGroup bg(ds);
   int rc = bg.prepare_and_bind(qstr, args, xsink);
   if (rc == -1)
      return 0;

   if (rc == 1)
      return qore_mysql_do_sql(ds, qstr, args, xsink);

   return bg.select(xsink);
#else
   return qore_mysql_do_sql(ds, qstr, args, xsink);
#endif
}

static AbstractQoreNode *qore_mysql_exec(Datasource *ds, const QoreString *qstr, const QoreListNode *args, ExceptionSink *xsink) {
   checkInit();
#ifdef HAVE_MYSQL_STMT
   QoreMySQLBindGroup bg(ds);
   int rc = bg.prepare_and_bind(qstr, args, xsink);
   if (rc == -1)
      return 0;

   if (rc == 1)
      return qore_mysql_do_sql(ds, qstr, args, xsink);

   return bg.exec(xsink);
#else
   return qore_mysql_do_sql(ds, qstr, args, xsink);
#endif
}

static int qore_mysql_open_datasource(Datasource *ds, ExceptionSink *xsink) {
   checkInit();

   MYSQL *db = qore_mysql_init(ds, xsink);
   if (!db)
      return -1;
   
   QoreMySQLConnection *d_mysql = new QoreMySQLConnection(db);
   ds->setPrivateData((void *)d_mysql);

   return 0;
}

static int qore_mysql_close_datasource(Datasource *ds) {
   QORE_TRACE("qore_mysql_close_datasource()");

   checkInit();

   QoreMySQLConnection *d_mysql = (QoreMySQLConnection *)ds->getPrivateData();
   
   printd(3, "qore_mysql_close_datasource(): connection to %s closed.\n", ds->getDBName());
   
   delete d_mysql;
   ds->setPrivateData(NULL);

   return 0;
}

static AbstractQoreNode *qore_mysql_get_server_version(Datasource *ds, ExceptionSink *xsink) {
   checkInit();
   QoreMySQLConnection *d_mysql = (QoreMySQLConnection *)ds->getPrivateData();
   return new QoreBigIntNode(d_mysql->getServerVersion());
}

static AbstractQoreNode *qore_mysql_get_client_version(const Datasource *ds, ExceptionSink *xsink) {
   checkInit();
   return new QoreBigIntNode(mysql_get_client_version());
}

QoreStringNode *qore_mysql_module_init() {
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
   methods.add(QDBI_METHOD_EXEC,               qore_mysql_exec);
   methods.add(QDBI_METHOD_COMMIT,             qore_mysql_commit);
   methods.add(QDBI_METHOD_ROLLBACK,           qore_mysql_rollback);
   methods.add(QDBI_METHOD_GET_SERVER_VERSION, qore_mysql_get_server_version);
   methods.add(QDBI_METHOD_GET_CLIENT_VERSION, qore_mysql_get_client_version);
   
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
