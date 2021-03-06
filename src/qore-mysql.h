/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    qore-mysql.h

    Qore Programming Language

    Copyright (C) 2003 - 2020 Qore Technologies, s.r.o.

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

#include <config.h>

#include <qore/Qore.h>

#include <mysql.h>

#include <string>

#ifndef MYSQL_PORT
#define MYSQL_PORT 3306
#endif

#define MYSQL_OPT_COLLATION "collation"

class QoreMysqlConnection;

class MyResult {
private:
   MYSQL_FIELD *field;
   int num_fields;
   MYSQL_BIND *bindbuf;
   struct bindInfo {
      my_bool mnull;
      long unsigned int mlen;
   } *bi;
   QoreMysqlConnection* conn;
   const QoreEncoding* enc;

public:
   DLLLOCAL MyResult(QoreMysqlConnection* c, const QoreEncoding* n_enc)
      : field(0), num_fields(0), bindbuf(0), bi(0), conn(c), enc(n_enc) {
   }

   DLLLOCAL ~MyResult() {
      reset();
   }

   DLLLOCAL void reset() {
      if (bindbuf) {
      // delete buffer
         for (int i = 0; i < num_fields; i++) {
            switch (bindbuf[i].buffer_type) {
               case MYSQL_TYPE_DOUBLE:
               case MYSQL_TYPE_LONGLONG:
               case MYSQL_TYPE_STRING:
               case MYSQL_TYPE_BLOB:
                  if (bindbuf[i].buffer)
                     free(bindbuf[i].buffer);
                  break;

               case MYSQL_TYPE_DATETIME:
                  delete (MYSQL_TIME*)bindbuf[i].buffer;
                  break;

               // to avoid warnings
               default:
                  break;
            }
         }
         delete [] bindbuf;
         bindbuf = 0;
      }
      if (bi) {
         delete [] bi;
         bi = 0;
      }
   }

   DLLLOCAL void set(MYSQL_STMT* stmt) {
      reset();
      MYSQL_RES* res = mysql_stmt_result_metadata(stmt);
      if (res) {
         field = mysql_fetch_fields(res);
         num_fields = mysql_num_fields(res);
         mysql_free_result(res);
      }
   }

   DLLLOCAL operator bool() const {
      return (bool)num_fields;
   }

   DLLLOCAL void bind(MYSQL_STMT *stmt);
   DLLLOCAL QoreValue getBoundColumnValue(int i, bool destructive = false);

   DLLLOCAL char *getFieldName(int i) {
      return field[i].name;
   }

   DLLLOCAL unsigned long getFieldMaxLength(int i) {
      return field[i].length;
   }

   DLLLOCAL enum_field_types getFieldType(int i) {
      return field[i].type;
   }

   DLLLOCAL int getNumFields() {
      return num_fields;
   }

   DLLLOCAL QoreHashNode* getSingleRow(ExceptionSink* xsink);

   // returns a hash of empty lists keyed by column name
   DLLLOCAL void setupColumns(QoreHashNode& h);
};

// FIXME: do not assume byte widths
union my_val {
   MYSQL_TIME time;
   int i4;
   int64 i8;
   double f8;
   void *ptr;

   DLLLOCAL void assign(const QoreMysqlConnection& conn, const DateTime &d);
};

class QoreMysqlBindNode {
protected:
    DLLLOCAL ~QoreMysqlBindNode() {
        assert(!data.value);
        assert(!data.tstr);
    }

public:
    int bindtype;
    unsigned long len;

    struct {
        QoreValue value;   // value to be bound
        QoreStringNode *tstr;   // temporary string to be deleted
    } data;

    union my_val vbuf;
    QoreMysqlBindNode *next;

    // for value nodes
    DLLLOCAL QoreMysqlBindNode(QoreValue v) {
        bindtype = BN_VALUE;
        data.value = v.refSelf();
        data.tstr = 0;
        next = 0;
    }

    DLLLOCAL void del(ExceptionSink* xsink) {
        reset(xsink);
        delete this;
    }

    DLLLOCAL int reset(ExceptionSink* xsink) {
        if (data.tstr) {
            data.tstr->deref();
            data.tstr = 0;
        }
        if (data.value) {
            data.value.discard(xsink);
            data.value = QoreValue();
        }
        return *xsink ? -1 : 0;
    }

    DLLLOCAL int rebind(QoreValue v, ExceptionSink* xsink) {
        if (reset(xsink))
            return -1;
        data.value = v.refSelf();
        return 0;
    }

    DLLLOCAL int bindValue(const QoreMysqlConnection& conn, MYSQL_BIND *buf, ExceptionSink* xsink);
};

static MYSQL* qore_mysql_init(Datasource *ds, ExceptionSink* xsink);
static int mysql_set_collation(MYSQL* db, const char* collation_str, ExceptionSink* xsink);

static inline bool wasInTransaction(Datasource *ds) {
#ifdef _QORE_HAS_DATASOURCE_ACTIVETRANSACTION
   return ds->activeTransaction();
#else
   return ds->isInTransaction();
#endif
}

#define OPT_NUM_OPTIMAL 0  // return numeric as int64 if it fits or "number" if not
#define OPT_NUM_STRING  1  // always return numeric types as strings
#define OPT_NUM_NUMERIC 2  // always return numeric types as "number"

#ifdef _QORE_HAS_DBI_OPTIONS
// return optimal numeric values if options are supported
#define OPT_NUM_DEFAULT OPT_NUM_OPTIMAL
#else
// return numeric values as strings if options are not supported -- for backwards-compatibility
#define OPT_NUM_DEFAULT OPT_NUM_STRING
#endif

class QoreMysqlConnection {
public:
    MYSQL* db;
    Datasource& ds;
    const AbstractQoreZoneInfo* server_tz;
    int numeric_support;
    std::string collation;

    DLLLOCAL QoreMysqlConnection(MYSQL* d, Datasource& n_ds)
        : db(d), ds(n_ds),
        server_tz(currentTZ()),
        numeric_support(OPT_NUM_DEFAULT) {
    }

    DLLLOCAL ~QoreMysqlConnection() {
        mysql_close(db);
    }

    DLLLOCAL int reconnect(Datasource *ds, MYSQL_STMT *&stmt, const QoreString& str, ExceptionSink* xsink) {
        // throw an exception if a transaction is in progress
        if (wasInTransaction(ds))
            xsink->raiseException("DBI:MYSQL:CONNECTION-ERROR", "connection to MySQL database server lost while in a transaction; transaction has been lost");

        MYSQL *new_db = qore_mysql_init(ds, xsink);
        if (!new_db) {
            ds->connectionAborted();
            return -1;
        }
        if (!collation.empty() && mysql_set_collation(new_db, collation.c_str(), xsink)) {
            return -1;
        }

        printd(5, "mysql datasource %08p reconnected after timeout\n", ds);
        mysql_close(db);
        db = new_db;

        if (wasInTransaction(ds))
            return -1;

        // reinitialize statement
        mysql_stmt_close(stmt);
        stmt = stmt_init(xsink);
        if (!stmt)
            return -1;

        // prepare the statement for execution (again)
        if (mysql_stmt_prepare(stmt, str.getBuffer(), str.strlen()))
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

    DLLLOCAL MYSQL_STMT *stmt_init(ExceptionSink* xsink) {
        MYSQL_STMT *stmt = mysql_stmt_init(db);
        if (!stmt)
            xsink->raiseException("DBI:MYSQL:ERROR", "error creating MySQL statement handle: out of memory");
        return stmt;
    }

    DLLLOCAL unsigned long getServerVersion() {
        return mysql_get_server_version(db);
    }

    DLLLOCAL int setOption(const char* opt, const QoreValue val, ExceptionSink* xsink) {
        if (!strcasecmp(opt, DBI_OPT_NUMBER_OPT)) {
            numeric_support = OPT_NUM_OPTIMAL;
            return 0;
        }
        if (!strcasecmp(opt, DBI_OPT_NUMBER_STRING)) {
            numeric_support = OPT_NUM_STRING;
            return 0;
        }
        if (!strcasecmp(opt, DBI_OPT_NUMBER_NUMERIC)) {
            numeric_support = OPT_NUM_NUMERIC;
            return 0;
        }

        if (!strcasecmp(opt, MYSQL_OPT_COLLATION)) {
            assert(val.getType() == NT_STRING);
            const QoreStringNode* str = val.get<const QoreStringNode>();
            std::string new_collation;
            if (str->empty()) {
                xsink->raiseException("MYSQL-OPTION-ERROR", "the 'collation' option requires a value; leaving with " \
                    "the current value: '%s'", collation.c_str());
                return -1;
            }
            new_collation = str->c_str();
            int rc = mysql_set_collation(db, new_collation.c_str(), xsink);
            if (!rc) {
                collation = new_collation;
            }
            return rc;
        }

        assert(!strcasecmp(opt, DBI_OPT_TIMEZONE));
        assert(val.getType() == NT_STRING);
        const QoreStringNode* str = val.get<const QoreStringNode>();
        const AbstractQoreZoneInfo* tz = find_create_timezone(str->c_str(), xsink);
        if (*xsink)
            return -1;
        server_tz = tz;
        return 0;
    }

    DLLLOCAL QoreValue getOption(const char* opt) {
        if (!strcasecmp(opt, DBI_OPT_NUMBER_OPT))
            return numeric_support == OPT_NUM_OPTIMAL;

        if (!strcasecmp(opt, DBI_OPT_NUMBER_STRING))
            return numeric_support == OPT_NUM_STRING;

        if (!strcasecmp(opt, DBI_OPT_NUMBER_NUMERIC))
            return numeric_support == OPT_NUM_NUMERIC;

        if (!strcasecmp(opt, MYSQL_OPT_COLLATION)) {
            if (collation.empty()) {
                return QoreValue();
            }
            return QoreValue(new QoreStringNode(collation));
        }

        assert(!strcasecmp(opt, DBI_OPT_TIMEZONE));
        return new QoreStringNode(tz_get_region_name(server_tz));
    }

    DLLLOCAL int getNumeric() const {
        return numeric_support;
    }

    DLLLOCAL const AbstractQoreZoneInfo* getTZ() const {
        return server_tz;
    }
};

class QoreMysqlBindGroup {
protected:
    QoreMysqlBindNode *head, *tail;
    QoreString *str;
    MYSQL_STMT *stmt;
    bool hasOutput;
    MYSQL_BIND *bind;
    Datasource *ds;
    QoreMysqlConnection* mydata;
    int len;
    cstr_vector_t phl;
    MyResult myres;

    // returns -1 = error, 0 = OK, 1 = server doesn't support prepared statements
    DLLLOCAL int prepare(bool unsupported_ok, ExceptionSink* xsink);

    // returns -1 = error, 0 = OK
    DLLLOCAL int rebindArgs(const QoreListNode* args, ExceptionSink* xsink);
    DLLLOCAL int bindArgs(ExceptionSink* xsink);

    // returns -1 = error, 0 = OK
    DLLLOCAL inline int parse(const QoreListNode *args, ExceptionSink* xsink);
    DLLLOCAL inline void add(class QoreMysqlBindNode *c) {
        len++;
        if (!tail)
            head = c;
        else
            tail->next = c;
        tail = c;
    }

    DLLLOCAL int execIntern(ExceptionSink* xsink);

    DLLLOCAL int getDataRows(QoreListNode& l, ExceptionSink* xsink, int max = -1);
    DLLLOCAL int getDataColumns(QoreHashNode& h, ExceptionSink* xsink, int max = -1, bool cols = false);

    DLLLOCAL ~QoreMysqlBindGroup() {
        assert(!head);
        assert(!str);
        assert(!stmt);
        assert(!bind);
    }

public:
    DLLLOCAL QoreMysqlBindGroup(Datasource *ods)
    : head(0), tail(0), str(0), stmt(0), hasOutput(false), bind(0),
        ds(ods), mydata((QoreMysqlConnection *)ds->getPrivateData()), len(0),
        myres(mydata, ds->getQoreEncoding()) {
    }

    DLLLOCAL void del(ExceptionSink* xsink) {
        reset(xsink);
        delete this;
    }

    DLLLOCAL void reset(ExceptionSink* xsink);

    // returns 0=OK, -1=error and exception raised, 1=statement cannot be prepared
    DLLLOCAL int prepareAndBind(const QoreString *ostr, const QoreListNode *args, ExceptionSink* xsink);

    DLLLOCAL void add(QoreValue v) {
        add(new QoreMysqlBindNode(v));
        printd(5, "QoreMysqlBindGroup::add() value: '%s'\n", v.getTypeName());
    }

    DLLLOCAL void add(char *name) {
        phl.push_back(name);
        printd(5, "QoreMysqlBindGroup::add() placeholder '%s' %d %s\n", name);
        hasOutput = true;
    }

    DLLLOCAL int affectedRows() const {
        assert(stmt);
        return mysql_stmt_affected_rows(stmt);
    }

    // also can be used like "select"
    DLLLOCAL QoreValue exec(ExceptionSink* xsink, bool cols = false);
    DLLLOCAL QoreValue selectRows(ExceptionSink* xsink);
    DLLLOCAL QoreHashNode* selectRow(ExceptionSink* xsink);

    DLLLOCAL QoreHashNode* getOutputHash(ExceptionSink* xsink);
};

class QoreMysqlBindGroupHelper : public QoreMysqlBindGroup {
protected:
    ExceptionSink* xsink;

public:
    DLLLOCAL QoreMysqlBindGroupHelper(Datasource* ds, ExceptionSink* xs) : QoreMysqlBindGroup(ds), xsink(xs) {
    }

    DLLLOCAL ~QoreMysqlBindGroupHelper() {
        reset(xsink);
    }
};

class QoreMysqlPreparedStatement : public QoreMysqlBindGroup {
public:
    DLLLOCAL QoreMysqlPreparedStatement(Datasource* ds) : QoreMysqlBindGroup(ds) {
    }

    DLLLOCAL ~QoreMysqlPreparedStatement() {
    }

    // returns 0 for OK, -1 for error
    DLLLOCAL int prepare(const QoreString& sql, const QoreListNode* args, bool parse, ExceptionSink* xsink);
    DLLLOCAL int bind(const QoreListNode& l, ExceptionSink* xsink);
    DLLLOCAL int define(ExceptionSink* xsink);
    DLLLOCAL int exec(ExceptionSink* xsink);
    DLLLOCAL QoreHashNode* fetchRow(ExceptionSink* xsink);
    DLLLOCAL QoreListNode* fetchRows(int rows, ExceptionSink* xsink);
    DLLLOCAL QoreHashNode* fetchColumns(int rows, ExceptionSink* xsink);
    DLLLOCAL QoreHashNode* describe(ExceptionSink *xsink);
    DLLLOCAL bool next();
};

#endif // _QORE_MYSQL_H
