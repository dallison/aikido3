#include "aikido.h"

#include "aikidointerpret.h"

// MySQL interface file
#include "mysql.h"

// everything in the aikido namespace
namespace aikido {

// these are internal objects (see native.cc) for dates

extern Class *dateClass ;

Package *mysqlPackage ;
Class *tableClass ;


// since we are linking these through the dynamic linker we need them
// unmangled
extern "C" {

aikido::OSMutex lock ;

//
// initialize the database internals
//

AIKIDO_NATIVE (db_init) {
    static bool initdone = false ;

    if (initdone) {
        return 0 ;
    }
    initdone = true ;

    Scope *scope ;
    Tag *pkg = b->findTag (string (".MySQL")) ;
    if (pkg == NULL) {
        throw Exception ("Can't find MySQL package") ;
    }
    mysqlPackage = (Package *)pkg->block ;

    Tag *tabletag = (Tag *)mysqlPackage->findVariable (string (".Table"), scope, VAR_ACCESSFULL, NULL, NULL)
 ;
    if (tabletag == NULL) {
        throw Exception ("Can't find MySQL.Table") ;
    }
    tableClass = (Class *)tabletag->block ;
    return 0 ;
}

// connect to the database

AIKIDO_NATIVE (connect_db) {
    const char *host = paras[1].str->c_str() ;
    const char *dbname = paras[2].str->c_str() ;
    const char *uname = paras[3].str->c_str() ;
    const char *passwd = paras[4].str->c_str() ;
    MYSQL *db = mysql_init (NULL) ;
    lock.lock() ;		// the mysql_real_connect is not thread safe
    db = mysql_real_connect (db, host, uname, passwd, dbname, 0, NULL, CLIENT_MULTI_RESULTS) ;
    lock.unlock() ;
    if (db == NULL) {
        throw Exception ("Unable to connect to database") ;
    }
    return (INTEGER)db ;
}

// close the database
AIKIDO_NATIVE (close_db) {
    MYSQL *db = (MYSQL*)paras[1].integer ;
    mysql_close (db) ;
    return 0 ;
}

// perform a database query and return the results
// results are returned as an instance of a MySQL.Table object

AIKIDO_NATIVE (query_db) {
    MYSQL *db = (MYSQL*)paras[1].integer ;
    if (paras[2].type != T_STRING) {
        throw Exception ("Bad query type, string expected") ;
    }
    const char *q = paras[2].str->c_str() ;
    int r = mysql_query (db, q) ;
    if (r) {
        const char *err = mysql_error (db) ;
        if (err != NULL) {
            throw Exception (err) ;
        }
        throw Exception ("query failed with unknown error") ;
    }

    // since this is called through an object, the static link has to be
    // modified to be that of the parent of the object.

    StaticLink *prevlink = staticLink->next ;

    // allocate a new Table object
    Object *table = new (tableClass->stacksize) Object (tableClass, prevlink, stack, stack->inst) ;
    table->ref++ ;					// just in case
    table->varstack[0] = Value (table) ;		// assign this
    Value::vector *rows = new Value::vector() ;
    rows->clear() ;
    table->varstack[1] = rows ;
    vm->execute (tableClass, table, prevlink, 0) ;		// construct it
    table->ref-- ;

    do {
        MYSQL_RES *result = mysql_store_result (db) ;
        if (result == NULL) {
            if (mysql_errno (db) != 0) {
                const char *err = mysql_error (db) ;
                if (err != NULL) {
                    std::cout << "error = " << err << "\n" ;
                    throw Exception (err) ;
                }
            }
            break;
        }
        int numrows = mysql_num_rows (result) ;
        int numfields = mysql_num_fields (result) ;
        MYSQL_ROW row ;

	    MYSQL_FIELD *fields = mysql_fetch_fields (result) ;
	    while (row = mysql_fetch_row(result)) {
		unsigned long *lengths = mysql_fetch_lengths (result) ;
		map *rowobj = new map() ;
		rows->push_back (rowobj) ;
		for (int i = 0; i < numfields ; i++) {
		    Value fieldname (fields[i].name);
		    if (row[i] == NULL) {
			(*rowobj)[fieldname] = Value (new string (""));
			//(*rowv)[i] = Value ((Object*)NULL) ;
			continue ;
		    }
		    switch (fields[i].type) {
		    case FIELD_TYPE_INT24:
		    case FIELD_TYPE_TINY:
		    case FIELD_TYPE_SHORT:
		    case FIELD_TYPE_LONG:
		    case FIELD_TYPE_DECIMAL: {
			//int v ;
			//sscanf (row[i], "%d", &v) ;
			(*rowobj)[fieldname] = Value (new string (row[i]));;
			//(*rowv)[i] = (INTEGER)v ;
			break ;
			}
		    
		    case FIELD_TYPE_NULL:
			(*rowobj)[fieldname] = Value (new string(""));
			//(*rowv)[i] = Value ((Object*)NULL) ;
		       break ;

		    case FIELD_TYPE_DOUBLE:
		    case FIELD_TYPE_FLOAT: {
			//float v ;
			//sscanf (row[i], "%f", &v) ;
			(*rowobj)[fieldname] = Value (new string (row[i]));;
			//(*rowv)[i] = v ;
			break ;
			}

		    case FIELD_TYPE_LONGLONG: {
			//INTEGER v ;
			//sscanf (row[i], "%lld", &v) ;
			(*rowobj)[fieldname] = Value (new string (row[i]));;
			//(*rowobj)[fieldname] = v;
			//(*rowv)[i] = (INTEGER)v ;
			break ;
			}

		    // for a set, split the value into an array of strings at the commas
		    case FIELD_TYPE_SET: {
			std::string str = row[i] ;
			Value::vector *vec = new Value::vector (10) ;
			vec->clear() ;
			std::string::size_type i, oldi = 0 ;
			do {   
			    i = str.find (',', oldi) ;
			    if (i != std::string::npos) {
				string *s = new string (str.substr (oldi, i - oldi)) ;
				vec->push_back (s) ;
				oldi = str.find_first_not_of (',', i) ;
				if (oldi == std::string::npos) {
				    break ;
				}
			    } else {
				string *s = new string (str.substr (oldi)) ;
				vec->push_back (s) ;
			    }
			} while (i != std::string::npos) ;
			(*rowobj)[fieldname] = vec;
			//(*rowv)[i] = vec ;
			break ;
			}

		    // for dates and times, we generate a System.Date object that contains
		    // the data for the date.
		    case FIELD_TYPE_DATE:
		    case FIELD_TYPE_TIME:
		    case FIELD_TYPE_DATETIME:
		    case FIELD_TYPE_NEWDATE:
		    case FIELD_TYPE_YEAR: {
			(*rowobj)[fieldname] = new string (row[i]) ;
	#if 0
			// since this is called through an object, the static link has to be
			// modified to be that of the parent of the object.

			StaticLink *prevlink = staticLink->next ;

			// allocate a new Date object
			Object *dateobj = new (dateClass->stacksize) Object (dateClass, prevlink, stack, stack->inst) ;
			dateobj->ref++ ;					// just in case
			dateobj->varstack[0] = Value (dateobj) ;		// assign this
			dateobj->varstack[1] = Value (row[i]) ;			// constructor is string
			try {
			    vm->execute (dateClass, dateobj, prevlink, 0) ;		// construct it
			} catch (Exception e) {
			    // for a bad date, just return a string
			    delete dateobj ;
			    (*rowobj)[fieldname] = new string (row[i]) ;
			    //(*rowv)[i] = new string (row[i]) ;
			    break ;
			}
			dateobj->ref-- ;
			(*rowobj)[fieldname] = dateobj;
			//(*rowv)[i] = dateobj ;
	#endif
			break ;
			}

		    // blobs are byte arrays
		    case FIELD_TYPE_TINY_BLOB:
		    case FIELD_TYPE_MEDIUM_BLOB:
		    case FIELD_TYPE_LONG_BLOB:
		    case FIELD_TYPE_BLOB: {
			(*rowobj)[fieldname] = new string (row[i]) ;
	#if 0
			Value::bytevector *vec = new Value::bytevector (lengths[i]) ;
			char *s = row[i] ;
			for (int k = 0 ; k < lengths[i] ; k++) {
			   vec->vec[k] = s[k] ;
			}
			(*rowobj)[fieldname] = vec;
			//(*rowv)[i] = vec ;
	#endif
			break ;
			}

		    case FIELD_TYPE_ENUM:
		    case FIELD_TYPE_TIMESTAMP: 
		    case FIELD_TYPE_VAR_STRING:
		    case FIELD_TYPE_STRING: {
			string *str = new string (row[i], lengths[i]) ;
			(*rowobj)[fieldname] = str;
			//(*rowv)[i] = str ;
			break ;
			}
		    }
		}

	    }
	    mysql_free_result (result) ;
    } while (mysql_next_result(db) == -1);

    return table ;
}

AIKIDO_NATIVE (last_insert_id_db) {
    MYSQL *db = (MYSQL*)paras[1].integer ;
    return (INTEGER)mysql_insert_id (db) ;
}

AIKIDO_NATIVE (escape_file) {
    const char *filename = paras[1].str->c_str() ;
    std::ifstream in (filename) ;
    if (!in) {
       throw Exception ("Cannot open file") ;
    }
    string *s = new string ;
    while (!in.eof()) {
        char ch = in.get() ;
        if (!in.eof()) {
            if (ch == '\'' || ch == '\\') {
                s->push_back ('\\') ;
            }
            s->push_back (ch) ;
        }
    }
    return s ;
}

AIKIDO_NATIVE(prepare_db) {
    MYSQL *db = (MYSQL*)paras[1].integer ;
    if (paras[2].type != T_STRING) {
        throw  Exception ("Bad query type, string expected") ;
    }
    const char *q = paras[2].str->c_str() ;
    MYSQL_STMT *stmt = mysql_stmt_init (db);
    if (stmt == NULL) {
        const char *err = mysql_stmt_error (stmt) ;
        if (err != NULL) {
            throw Exception (err) ;
        }
        throw Exception ("stmt_init failed with unknown error") ;
    }
    int r = mysql_stmt_prepare (stmt, q, paras[2].str->size());
    if (r != 0) {
        const char *err = mysql_stmt_error (stmt) ;
        if (err != NULL) {
            throw Exception (err) ;
        }
        throw Exception ("stmt_prepare failed with unknown error") ;
    }
    return (INTEGER)stmt;
}

AIKIDO_NATIVE(db_query_prepared) {
    MYSQL *db = (MYSQL*)paras[1].integer ;
    MYSQL_STMT *stmt = (MYSQL_STMT*)paras[2].integer;
    Value::vector *bindings = paras[3].vec;
    int numbindings = paras[4].integer;

    // bind the input parameters
    MYSQL_BIND *inbind = new MYSQL_BIND[numbindings];
    memset (inbind, 0, sizeof(MYSQL_BIND)*numbindings);
    char *inbuffer = new char[numbindings*8];;
    memset (inbuffer, 0, numbindings*8);
    int freespace = 0;

    for (int i = 0 ; i < numbindings ; i++) {
        Value &v = (*bindings)[i];
        MYSQL_BIND *b = &inbind[i];
        switch (v.type) {
        case T_INTEGER: 
        case T_CHAR:
            b->buffer_type = MYSQL_TYPE_LONGLONG;	// seems to work with 4 byte integers too
            *(int*)&inbuffer[freespace] = (int)v.integer;
            b->buffer = &inbuffer[freespace];
            freespace += 8;
            b->is_null = 0;
            b->length = 0;
            break;
        case T_STRING:
            b->buffer_type = MYSQL_TYPE_VAR_STRING;
            b->buffer = (char*)v.str->c_str();
            b->is_null = 0;
            b->buffer_length = v.str->size();

            *(int*)&inbuffer[freespace] = b->buffer_length;
            b->length = (unsigned long*)&inbuffer[freespace];
            freespace += 8;
            break;
        case T_REAL:
            b->buffer_type = MYSQL_TYPE_DOUBLE;
            *(double*)&inbuffer[freespace] = v.real;
            b->buffer = &inbuffer[freespace];
            freespace += 8;
            b->is_null = 0;
            b->length = 0;
            break;
        case T_OBJECT: {
            Object *obj = v.object;
            if (obj->block == dateClass) {
                b->buffer_type = MYSQL_TYPE_TIMESTAMP;
                b->is_null = 0;
                b->length = 0;

                MYSQL_TIME *ts = (MYSQL_TIME*)&inbuffer[freespace];
                b->buffer = (char*)ts;
                freespace += sizeof (*ts);
                ts->second = obj->varstack[2].integer;
                ts->minute = obj->varstack[3].integer;
                ts->hour = obj->varstack[4].integer;
                ts->day = obj->varstack[5].integer;
                ts->month = obj->varstack[6].integer;
                ts->year = obj->varstack[7].integer;
            } else {
                throw Exception ("Unsupport object type");
            }
            break;
            }
        default:
            throw Exception ("Unsupported data type");
        }
    }
    int r = mysql_stmt_bind_param (stmt, inbind);
    if (r != 0) {
        delete [] inbind;
        delete [] inbuffer;
        const char *err = mysql_stmt_error (stmt) ;
        if (err != NULL) {
            throw Exception (err) ;
        }
        throw Exception ("bind_params failed with unknown error") ;
    }

    r = mysql_stmt_execute (stmt);
    if (r != 0) {
        delete [] inbind;
        delete [] inbuffer;
        const char *err = mysql_stmt_error (stmt) ;
        if (err != NULL) {
            throw Exception (err) ;
        }
        throw Exception ("execute failed with unknown error") ;
    }

    // done with these
    delete [] inbind;
    delete [] inbuffer;

    MYSQL_RES *result = mysql_stmt_result_metadata (stmt) ;
    if (result == NULL) {
        return 0;		// no result metadata exists.
    }

    // allocate the output binding objects
    int numfields = mysql_num_fields(result);
    MYSQL_BIND *outbind = new MYSQL_BIND[numfields];
    const int SPACE_SIZE = sizeof(MYSQL_TIME) + 16 ;
    char *outbuffer = new char[numfields *SPACE_SIZE];
    memset (outbuffer, 0, numfields*SPACE_SIZE);
    freespace = 0;

    Object *table = NULL;

    // now we need to set up all the pointers and types in the outbind array
    MYSQL_FIELD *fields = mysql_fetch_fields (result) ;
    for (int i = 0 ; i < numfields ; i++) {
        MYSQL_BIND *b = &outbind[i];
        b->buffer = &outbuffer[freespace];		// enough space for a MYSQL_TIME

        b->is_null = &outbuffer[freespace+SPACE_SIZE-12];
        b->length = (unsigned long*)&outbuffer[freespace+SPACE_SIZE-8];
        b->error = &outbuffer[freespace+SPACE_SIZE-4];
        switch (fields[i].type) {
        case FIELD_TYPE_TINY:
            b->buffer_type = MYSQL_TYPE_TINY;
            break;

        case FIELD_TYPE_SHORT:
            b->buffer_type = MYSQL_TYPE_SHORT;
            break;
        case FIELD_TYPE_INT24:
        case FIELD_TYPE_LONG:
        case FIELD_TYPE_DECIMAL: 
        case FIELD_TYPE_ENUM:
            b->buffer_type = MYSQL_TYPE_LONG;
            break;
        
        case FIELD_TYPE_DOUBLE:
            b->buffer_type = MYSQL_TYPE_DOUBLE;
            break;
 
        case FIELD_TYPE_FLOAT: 
            b->buffer_type = MYSQL_TYPE_FLOAT;
            break;

        case FIELD_TYPE_LONGLONG: 
            b->buffer_type = MYSQL_TYPE_LONGLONG;
            break ;

        case FIELD_TYPE_SET: 

        case FIELD_TYPE_DATE:
        case FIELD_TYPE_TIME:
        case FIELD_TYPE_DATETIME:
        case FIELD_TYPE_NEWDATE:
        case FIELD_TYPE_YEAR: 
           break;

        // blobs are byte arrays
        case FIELD_TYPE_TINY_BLOB:
        case FIELD_TYPE_MEDIUM_BLOB:
        case FIELD_TYPE_LONG_BLOB:
        case FIELD_TYPE_BLOB: {
            b->buffer_type = MYSQL_TYPE_BLOB;
            b->buffer = malloc (8192);
            b->buffer_length = 8192;
            break ;
            }

        case FIELD_TYPE_TIMESTAMP: 
            b->buffer_type = MYSQL_TYPE_TIMESTAMP;
            break;

        case FIELD_TYPE_VAR_STRING:
        case FIELD_TYPE_STRING: 
            b->buffer_type = MYSQL_TYPE_STRING;
            b->buffer = malloc (8192);
            b->buffer_length = 8192;
            break ;
        }
        freespace += SPACE_SIZE;
    }

    try {
        r = mysql_stmt_bind_result (stmt, outbind);
        if (r != 0) {
            const char *err = mysql_stmt_error (stmt) ;
            if (err != NULL) {
                throw Exception (err) ;
            }
            throw Exception ("bind_result failed with unknown error") ;
        }
    
        r = mysql_stmt_store_result (stmt);
        if (r != 0) {
            const char *err = mysql_stmt_error (stmt) ;
            if (err != NULL) {
                throw Exception (err) ;
            }
            throw Exception ("store_result failed with unknown error") ;
        }
    
        // since this is called through an object, the static link has to be
        // modified to be that of the parent of the object.
    
        StaticLink *prevlink = staticLink->next ;
    
        // allocate a new Table object
        table = new (tableClass->stacksize) Object (tableClass, prevlink, stack, stack->inst) ;
        table->ref++ ;					// just in case
        table->varstack[0] = Value (table) ;		// assign this
        Value::vector *rows = new Value::vector() ;
        rows->clear() ;
        table->varstack[1] = rows ;
        vm->execute (tableClass, table, prevlink, 0) ;		// construct it
        table->ref-- ;
        char buf[256];

        while (!mysql_stmt_fetch (stmt)) {
            freespace = 0;
            map *rowobj = new map() ;
            rows->push_back (rowobj) ;
            for (int i = 0  ; i < numfields ; i++) {
                Value fieldname (fields[i].name);
                switch (fields[i].type) {
                case FIELD_TYPE_TINY:
                    snprintf (buf, sizeof(buf), "%d", *(char*)&outbuffer[freespace]);
                    (*rowobj)[fieldname] = Value (new string (buf));
                    break;

                case FIELD_TYPE_SHORT:
                    snprintf (buf, sizeof(buf), "%d", *(short*)&outbuffer[freespace]);
                    (*rowobj)[fieldname] = Value (new string (buf));
                    break;
                case FIELD_TYPE_INT24:
                case FIELD_TYPE_LONG:
                case FIELD_TYPE_DECIMAL: 
                case FIELD_TYPE_ENUM:
                    snprintf (buf, sizeof(buf), "%d", *(int*)&outbuffer[freespace]);
                    (*rowobj)[fieldname] = Value (new string (buf));
                    break;
        
                case FIELD_TYPE_DOUBLE:
                    snprintf (buf, sizeof(buf), "%.15f", *(double*)&outbuffer[freespace]);
                    (*rowobj)[fieldname] = Value (new string (buf));
                    break;
 
                case FIELD_TYPE_FLOAT: 
                    snprintf (buf, sizeof(buf), "%.15f", *(float*)&outbuffer[freespace]);
                    (*rowobj)[fieldname] = Value (new string (buf));
                    break;

                case FIELD_TYPE_LONGLONG: 
                    snprintf (buf, sizeof(buf), "%.lld", *(INTEGER*)&outbuffer[freespace]);
                    (*rowobj)[fieldname] = Value (new string (buf));
                    break ;

                case FIELD_TYPE_SET: 

                case FIELD_TYPE_DATE:
                case FIELD_TYPE_TIME:
                case FIELD_TYPE_DATETIME:
                case FIELD_TYPE_NEWDATE:
                case FIELD_TYPE_YEAR: 
                   break;

                // blobs are byte arrays
                case FIELD_TYPE_TINY_BLOB:
                case FIELD_TYPE_MEDIUM_BLOB:
                case FIELD_TYPE_LONG_BLOB:
                case FIELD_TYPE_BLOB: {
                    // XXX: how to determine length?
                    (*rowobj)[fieldname] = Value (new string ((char*)outbind[i].buffer));
                    break ;
                    }

                case FIELD_TYPE_TIMESTAMP: {
                    MYSQL_TIME *ts = (MYSQL_TIME*)&outbuffer[freespace];
                    snprintf (buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", ts->year, ts->month, ts->day, ts->hour, ts->minute, ts->second);
                    (*rowobj)[fieldname] = Value (new string (buf));
                    break; 
                    }

                case FIELD_TYPE_VAR_STRING:
                case FIELD_TYPE_STRING: 
                    (*rowobj)[fieldname] = Value (new string ((char*)outbind[i].buffer));
                    break ;
                }
                freespace += SPACE_SIZE;
            }
        }
    } catch (...) {
        // delete the memory
        for (int i = 0 ; i < numfields ; i++) {
            switch (fields[i].type) {
            case FIELD_TYPE_VAR_STRING:
            case FIELD_TYPE_STRING: 
            case FIELD_TYPE_TINY_BLOB:
            case FIELD_TYPE_MEDIUM_BLOB:
            case FIELD_TYPE_LONG_BLOB:
            case FIELD_TYPE_BLOB: 
                free (outbind[i].buffer);
                break;
            }
        }
        delete [] outbuffer;
        delete [] outbind;
        mysql_free_result (result) ;
        throw;
    }

    // delete the memory
    for (int i = 0 ; i < numfields ; i++) {
        switch (fields[i].type) {
        case FIELD_TYPE_VAR_STRING:
        case FIELD_TYPE_STRING: 
        case FIELD_TYPE_TINY_BLOB:
        case FIELD_TYPE_MEDIUM_BLOB:
        case FIELD_TYPE_LONG_BLOB:
        case FIELD_TYPE_BLOB: 
            free (outbind[i].buffer);
            break;
        }
    }
    delete [] outbuffer;
    delete [] outbind;

    mysql_free_result (result) ;
    return table ;
}

AIKIDO_NATIVE(close_stmt_db) {
    MYSQL *db = (MYSQL*)paras[1].integer ;
    MYSQL_STMT *stmt = (MYSQL_STMT*)paras[2].integer;
    mysql_stmt_close (stmt);
    return 0;
}


}

}

