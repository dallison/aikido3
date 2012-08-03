#include "libpq-fe.h"
#include "aikido.h"
#include "aikidointerpret.h"
#include <stdio.h>

namespace aikido {

// these are internal objects (see native.cc) for dates

extern Class *dateClass ;

Package *pgPackage ;
Class *tableClass ;



extern "C" {

AIKIDO_NATIVE (pginit) {
    static bool initdone = false ;

    if (initdone) {
        return 0 ;
    }
    initdone = true ;

    Scope *scope ;
    Tag *pkg = b->findTag (string (".PostgreSQL"), currentScope) ;
    if (pkg == NULL) {
        throw Exception ("Can't find PostgreSQL package") ;
    }
    pgPackage = (Package *)pkg->block ;

    Tag *tabletag = (Tag *)pgPackage->findVariable (string (".Table"), scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (tabletag == NULL) {
        throw Exception ("Can't find PostgreSQL.Table") ;
    }
    tableClass = (Class *)tabletag->block ;
    return 0 ;
}

AIKIDO_NATIVE (pgopen) {
    string *host,*database,*username,*password;

    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "pgopen", "Invalid hostname");
    }
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "pgopen", "Invalid database");
    }
    if (paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "pgopen", "Invalid username");
    }
    if (paras[4].type != T_STRING) {
        throw newParameterException (vm, stack, "pgopen", "Invalid password");
    }
    host = paras[1].str;
    database = paras[2].str;
    username = paras[3].str;
    password = paras[4].str;

    PGconn *conn;
    char conninfo[1024];
    snprintf (conninfo, sizeof(conninfo), "host=%s dbname=%s user=%s password=%s", host->c_str(), database->c_str(), username->c_str(), password->c_str());
    conn = PQconnectdb (conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        std::string error = PQerrorMessage(conn);
        throw newException (vm, stack, error.c_str());
        //throw newException (vm, stack, "Failed to connect to database");
    }
    return (INTEGER)conn;
}

AIKIDO_NATIVE(pgclose) {
    if (paras[1].type != T_INTEGER) {
        throw newParameterException (vm, stack, "pgclose", "Invalid connection");
    }
    PGconn *conn = (PGconn*)paras[1].integer;
    PQfinish (conn);
    return 0;
}

AIKIDO_NATIVE(pgexec) {
    if (paras[1].type != T_INTEGER) {
        throw newParameterException (vm, stack, "pgexec", "Invalid connection");
    }
    PGconn *conn = (PGconn*)paras[1].integer;

    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "pgexec", "Invalid query");
    }
    string *query = paras[2].str;

    PGresult *res = PQexec (conn, query->c_str()) ;
    if (res == NULL) {
        throw newException (vm, stack, "FATAL postgres error");
    }
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error = PQerrorMessage(conn);
        PQclear(res);
        throw newException (vm, stack, error.c_str());
    }
    return 0;
}

AIKIDO_NATIVE(pgquery) {
    if (paras[1].type != T_INTEGER) {
        throw newParameterException (vm, stack, "pgexec", "Invalid connection");
    }
    PGconn *conn = (PGconn*)paras[1].integer;
    
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "pgexec", "Invalid query");
    }
    string *query = paras[2].str;

    PGresult *res = PQexec (conn, query->c_str()) ;
    if (res == NULL) {
        throw newException (vm, stack, "FATAL postgres error");
    }
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string error = PQerrorMessage(conn);
        PQclear(res);
        throw newException (vm, stack, error.c_str());
    }

    int ntuples = PQntuples(res);
    int nfields = PQnfields(res);

    // since this is called through an object, the static link has to be
    // modified to be that of the parent of the object.

    StaticLink *prevlink = staticLink->next ;

    // allocate a new Table object
    Object *table = new (tableClass->stacksize) Object (tableClass, prevlink, stack, stack->inst) ;
    table->ref++ ;                  // just in case
    table->varstack[0] = Value (table) ;        // assign this
    Value::vector *rows = new Value::vector (ntuples) ;
    rows->clear() ;
    table->varstack[1] = rows ;
    vm->execute (tableClass, table, prevlink, 0) ;      // construct it
    table->ref-- ;

    // get field names
    std::vector<std::string> fieldnames;
    for (int i = 0 ; i < nfields ; i++) {
        fieldnames.push_back (PQfname(res,i));
    }

    for (int i = 0 ; i < ntuples ; i++) {
        map *rowobj = new map() ;
        rows->push_back (rowobj) ;
        for (int j = 0 ; j < nfields ; j++) {
            Value fieldname(fieldnames[j]);
            (*rowobj)[fieldname] = new string(PQgetvalue(res, i, j));
        }
    }

    PQclear (res);
    return table;
}



}
}



