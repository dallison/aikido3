/*
 * registry.cc
 *
 * Aikido Language System,
 * export version: 1.00
 * Copyright (c) 2002 Sun Microsystems, Inc.
 *
 * Sun Public License Notice
 *
 * The contents of this file are subject to the Sun Public License Version
 * 1.0 (the "License"). You may not use this file except in compliance with
 * the License. A copy of the License is included as the file "license.terms",
 * and also available at http://www.sun.com/
 *
 * The Original Code is from:
 *    Aikido Language System release 1.00
 * The Initial Developer of the Original Code is: David Allison
 * Portions created by David Allison are Copyright (C) Sun Microsystems, Inc.
 * All Rights Reserved.
 *
 * Contributor(s): dallison
 *
 * Version:  1.4
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/01/24
 */


#include "../aikido.h"
#include <windows.h>

namespace aikido {

extern Class *pairClass ;


static bool isIntegral (const Value &v) {
    return v.type == T_INTEGER || v.type == T_BYTE || v.type == T_CHAR || v.type == T_ENUMCONST ;
}

static INTEGER getInt (const Value &v) {
    if (v.type == T_ENUMCONST) {
        return v.ec->value ;
    } else {
        return v.integer ;
    }
}


extern "C" {


static HKEY tohkey (int i) {
    switch (i) {
    case 0:
        return HKEY_CLASSES_ROOT ;
    case 1:
        return HKEY_CURRENT_CONFIG ;
    case 2:
        return HKEY_CURRENT_USER ;
    case 3:
        return HKEY_LOCAL_MACHINE ;
    case 4:
        return HKEY_USERS ;
    default:
        return NULL ;
    }
}


// open a registry key and return a handle (in an integer)
// parameters:
//   1: handle for open key (0..4 == predefined key)
//   2: string for key name to open

// outut: handle for open key

AIKIDO_NATIVE (regopenkey) {
    if (!isIntegral(paras[1])) {
        throw newParameterException (vm, stack, "openkey", "Illegal key handle") ;
    }
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "openkey", "Illegal key name") ;
    }
    INTEGER handle = getInt (paras[1]) ;
    HKEY hkey ;
    if (handle >= 0 && handle < 5) {
        hkey = tohkey (handle) ;
    } else {
        hkey = (HKEY)handle ;
    }
    HKEY outkey ;
    LONG res = RegOpenKeyExA (hkey, paras[2].str->c_str(), 0, KEY_ALL_ACCESS, &outkey) ;
    if (res != ERROR_SUCCESS) {
        throw newException (vm, stack, "Failed to open registry key") ;
    }
    return (INTEGER)outkey ;
}

// open a registry key and return a handle (in an integer)
// parameters:
//   1: handle for open key (0..4 == predefined key)
//   2: string for key name to create or open
//   
// outut: handle for open key

AIKIDO_NATIVE (regcreatekey) {
    if (!isIntegral(paras[1])) {
        throw newParameterException (vm, stack, "createkey", "Illegal key handle") ;
    }
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "createkey", "Illegal key name") ;
    }
    INTEGER handle = getInt (paras[1]) ;
    HKEY hkey ;
    if (handle >= 0 && handle < 5) {
        hkey = tohkey (handle) ;
    } else {
        hkey = (HKEY)handle ;
    }
    HKEY outkey ;
    DWORD disp ;
    LONG res = RegCreateKeyExA (hkey, paras[2].str->c_str(), 0, NULL, 
            REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &outkey, &disp) ;
    if (res != ERROR_SUCCESS) {
        throw newException (vm, stack, "Failed to open registry key") ;
    }
    return (INTEGER)outkey ;
}

static void getKeyInfo (VirtualMachine *vm, StackFrame *stack, HKEY key, DWORD &nkeys, DWORD &maxkeylen, DWORD &nvalues, DWORD &maxvaluenamelen, DWORD &maxvaluedatalen) {
    LONG res = RegQueryInfoKey (key, NULL, NULL, NULL,
        &nkeys, &maxkeylen, NULL,
        &nvalues, &maxvaluenamelen, &maxvaluedatalen, NULL, NULL) ;
    if (res != ERROR_SUCCESS) {
        throw newException (vm, stack, "Failed to get information on key") ;
    }
}


AIKIDO_NATIVE(regenumkeys) {
    if (!isIntegral(paras[1])) {
        throw newParameterException (vm, stack, "enumkeys", "Illegal key handle") ;
    }

    INTEGER handle = getInt (paras[1]) ;
    HKEY hkey ;
    if (handle >= 0 && handle < 5) {
        hkey = tohkey (handle) ;
    } else {
        hkey = (HKEY)handle ;
    }

    DWORD index = 0 ;
    LONG res ;
    DWORD nkeys, maxkeylen, dummy ;
    getKeyInfo (vm, stack, hkey, nkeys, maxkeylen, dummy, dummy, dummy) ;

    Value::vector *v = new Value::vector (nkeys) ;	// result vector
    v->clear() ;
    char *buf = new char[maxkeylen+ 1] ;
    DWORD len ;
    FILETIME time ;
    do {
        len = maxkeylen + 1;
        res = RegEnumKeyExA (hkey, index, buf, &len, 0, NULL, NULL, &time) ;
        if (res == ERROR_SUCCESS) {
            string *str = new string (buf) ;
            v->push_back (str) ;
            index++ ;
        }
    } while (res == ERROR_SUCCESS) ;
    delete [] buf ;
    return v ;
}

AIKIDO_NATIVE (regtest) {
    string *s = new string ("dave") ;
    return s ;
}


static Value parseValue (VirtualMachine *vm, StackFrame *stack, DWORD type, char *val, DWORD len) {
    switch (type) {
    case REG_BINARY: {
        Value::bytevector *v = new Value::bytevector (len) ;
        for (int i = 0 ; i < len ; i++) {
            (*v)[i] = val[i] ;
        }
        return v ;
        break ;
        }
    case REG_DWORD: 
        return (INTEGER)(*(int*)val) ;
        break ;
    case REG_DWORD_BIG_ENDIAN: {
        int v = *(int*)val ;
        v = ((v >> 24) & 0xff) | ((v >> 8) & 0xff00) |
            ((v <<8) & 0xff0000) | ((v << 24) & 0xff000000) ;
        return (INTEGER)v ;
        break ;
        }
    case REG_EXPAND_SZ:
    case REG_SZ:
        return new string (val) ;

    case REG_QWORD:
        return *(INTEGER*)val ;

    case REG_MULTI_SZ: {
        Value::vector *v = new Value::vector ;
        char *p = val ;
        while (*p != 0) {
            string *s = new string ;
            while (*p != 0) {
                *s += p ;
                p++ ;
            }
            v->push_back (s) ;
            p++ ;
        }
        return v ;
        break ;
        }

    case REG_NONE:
        return Value() ;

    default:
       throw newException (vm, stack, "Unsupported registry value type") ;
    }
}


AIKIDO_NATIVE(regenumvalues) {
    if (!isIntegral(paras[1])) {
        throw newParameterException (vm, stack, "enumvalueskeys", "Illegal key handle") ;
    }

    INTEGER handle = getInt (paras[1]) ;
    HKEY hkey ;
    if (handle >= 0 && handle < 5) {
        hkey = tohkey (handle) ;
    } else {
        hkey = (HKEY)handle ;
    }

    DWORD nkeys, maxkeylen, nvalues, maxnamelen, maxdatalen ;
    getKeyInfo (vm, stack, hkey, nkeys, maxkeylen, nvalues, maxnamelen, maxdatalen) ;
    DWORD index = 0 ;
    LONG res ;
    Value::vector *v = new Value::vector(nvalues) ;	// result vector
    v->clear() ;
    char *namebuf = new char [maxnamelen + 1] ;
    DWORD namelen ;
    BYTE *databuf = new BYTE [maxdatalen + 1] ;
    DWORD datalen ;
    DWORD type ;
    do {
        namelen = maxnamelen + 1 ;
        datalen = maxdatalen + 1 ;
        res = RegEnumValueA (hkey, index, namebuf, &namelen, 0, &type, databuf, &datalen) ;
        if (res == ERROR_SUCCESS) {
            string *name = new string (namebuf) ;
            Object *pairobj = new (pairClass->stacksize) Object (pairClass, NULL, NULL, NULL) ;
            pairobj->ref++ ;
            pairobj->varstack[0] = Value(pairobj) ;
            pairobj->varstack[1] = name ;
            pairobj->varstack[2] = parseValue (vm, stack, type, (char*)databuf, datalen) ;
            pairobj->ref-- ;
            v->push_back (pairobj) ;
            index++ ;
        }
    } while (res == ERROR_SUCCESS) ;
    delete [] namebuf ;
    delete [] databuf ;
    return v ;
}


AIKIDO_NATIVE (regsetvalue) {
    if (!isIntegral(paras[1])) {
        throw newParameterException (vm, stack, "regsetvalue", "Illegal key handle") ;
    }
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "regsetvalue", "Illegal value name") ;
    }
    INTEGER handle = getInt (paras[1]) ;
    HKEY hkey ;
    if (handle >= 0 && handle < 5) {
        hkey = tohkey (handle) ;
    } else {
        hkey = (HKEY)handle ;
    }
    DWORD type ;
    if (paras[4].type == T_NONE) {
        switch (paras[3].type) {
        case T_INTEGER:
        case T_CHAR:
            if (paras[3].integer > (INTEGER)0xffffffff) {
                type = REG_QWORD ;
            } else {
                type = REG_DWORD ;
            }
            break ;
        case T_STRING:
            type = REG_SZ ;
            break ;
        case T_BYTEVECTOR:
            type = REG_BINARY ;
            break ;
        case T_VECTOR: {
            bool ok = true ;
            for (int i = 0 ; i < paras[3].vec->size() ; i++) {
                if ((*paras[3].vec)[i].type != T_STRING) {
                    ok = false ;
                    break ;
                }
            }
            if (ok) {
                type = REG_MULTI_SZ ;
                break ;
            }
            }
        default:
            throw newParameterException (vm, stack, "regsetvalue", "Illegal value type") ;
        }
    } else {
        if (!isIntegral (paras[4].type)) {
            throw newParameterException (vm, stack, "regsetvalue", "Illegal value type") ;
        }
        switch (getInt (paras[4])) {
        case 0:		// DWORD
           type = REG_DWORD ;
           break ;
        case 1:         // DWORD_LITTLE_ENDIAN:
           type = REG_DWORD_LITTLE_ENDIAN ;
           break ;
        case 2:         // DWORD_BIG_ENDIAN:
           type = REG_DWORD_BIG_ENDIAN ;
           break ;
        case 3:         // QWORD
           type = REG_QWORD ;
           break ;
        case 4:         // QWORD_LITTLE_ENDIAN:
           type = REG_QWORD_LITTLE_ENDIAN ;
           break ;
        case 5:		// SZ
           type = REG_SZ ;
           break ;
        case 6:		// EXPAND_SZ
           type = REG_EXPAND_SZ ;
           break ;
        case 7:		// MULTI_SZ
           type = REG_MULTI_SZ ;
           break ;
        case 8:		// BINARY
           type = REG_BINARY ;
           break ;
	case 9:		// NONE
           type = REG_NONE ;
           break ;
        default:
            throw newParameterException (vm, stack, "regsetvalue", "Illegal value type") ;

        }
    }
    const Value &v = paras[3] ;
    char *buf  = NULL ;
    DWORD len = 0 ;
    bool freebuf = false ;
    switch (type) {
    case REG_DWORD: {
        if (!isIntegral (v)) {
            throw newParameterException (vm, stack, "regsetvalue", "Illegal value type") ;
        }
        INTEGER i = getInt (v) ;
        len = 4 ;
        buf = new char [4] ;
        buf[0] = i & 0xff ;
        buf[1] = (i >> 8) & 0xff ;
        buf[2] = (i >> 16) & 0xff ;
        buf[3] = (i >> 24) & 0xff ;
        freebuf = true ;
        break ;
        }
    case REG_DWORD_BIG_ENDIAN: {
        if (!isIntegral (v)) {
            throw newParameterException (vm, stack, "regsetvalue", "Illegal value type") ;
        }
        INTEGER i = getInt (v) ;
        len = 4 ;
        buf = new char [4] ;
        buf[3] = i & 0xff ;
        buf[2] = (i >> 8) & 0xff ;
        buf[1] = (i >> 16) & 0xff ;
        buf[0] = (i >> 24) & 0xff ;
        freebuf = true ;
        break ;
        }
    case REG_QWORD: {
        if (!isIntegral (v)) {
            throw newParameterException (vm, stack, "regsetvalue", "Illegal value type") ;
        }
        INTEGER i = getInt (v) ;
        len = 8 ;
        buf = new char [8] ;
        buf[0] = i & 0xff ;
        buf[1] = (i >> 8) & 0xff ;
        buf[2] = (i >> 16) & 0xff ;
        buf[3] = (i >> 24) & 0xff ;
        buf[4] = (i >> 32) & 0xff ;
        buf[5] = (i >> 40) & 0xff ;
        buf[6] = (i >> 48) & 0xff ;
        buf[7] = (i >> 56) & 0xff ;
        freebuf = true ;
        break ;
        }
    case REG_SZ:
    case REG_EXPAND_SZ: 
        if (v.type != T_STRING) {
            throw newParameterException (vm, stack, "regsetvalue", "Illegal value type") ;
        }
        len = v.str->size() + 1 ;
        buf = (char*)v.str->c_str() ;
        break ;
    case REG_BINARY: 
        if (v.type != T_BYTEVECTOR) {
            throw newParameterException (vm, stack, "regsetvalue", "Illegal value type") ;
        }
        len = v.bytevec->size() + 1 ;
//        buf = (char*)v.bytevec->begin() ;  (FIX)
        break ;
    case REG_MULTI_SZ: {
        if (v.type != T_VECTOR) {
            throw newParameterException (vm, stack, "regsetvalue", "Illegal value type") ;
        }
        for (int i = 0 ; i < v.vec->size() ; i++) {
            if ((*v.vec)[i].type == T_STRING) {
                len += (*v.vec)[i].str->size() + 1 ;
            } else {
                throw newParameterException (vm, stack, "regsetvalue", "Illegal value type") ;
            }
        }
        len++ ;			// room for final 0
        buf = new char [len] ;
        char *ch = buf ;
        for (int i = 0 ; i < v.vec->size() ; i++) {
            strcpy (ch, (*v.vec)[i].str->c_str()) ;
            ch += (*v.vec)[i].str->size() + 1 ;
        }
        *ch = 0 ;
        freebuf = true ;
        break ;
        }
    }
    LONG res = RegSetValueExA (hkey, paras[2].str->c_str(), 0, type, (CONST BYTE*)buf, len) ;
    if (freebuf) {
        delete [] buf ;
    }
    if (res != ERROR_SUCCESS) {
        throw newException (vm, stack, "Error setting value") ;
    }
    return 0 ;
}

AIKIDO_NATIVE (regqueryvalue) {
    if (!isIntegral(paras[1])) {
        throw newParameterException (vm, stack, "regqueryvalue", "Illegal key handle") ;
    }

    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "regqueryvalue", "Illegal value name") ;
    }
    INTEGER handle = getInt (paras[1]) ;
    HKEY hkey ;
    if (handle >= 0 && handle < 5) {
        hkey = tohkey (handle) ;
    } else {
        hkey = (HKEY)handle ;
    }

    DWORD nkeys, maxkeylen, nvalues, maxnamelen, maxdatalen ;
    getKeyInfo (vm, stack, hkey, nkeys, maxkeylen, nvalues, maxnamelen, maxdatalen) ;
    DWORD index = 0 ;
    LONG res ;
    char *databuf = new char [maxdatalen + 1] ;
    DWORD datalen = maxdatalen ;
    DWORD type ;
    res = RegQueryValueExA (hkey, paras[2].str->c_str(), 0, &type, (LPBYTE)databuf, &datalen) ;
    if (res != ERROR_SUCCESS) {
        throw newException (vm, stack, "Error querying value") ;
    }
    Value v = parseValue (vm, stack, type, databuf, datalen) ;
    delete [] databuf ;
    return v ;
}


AIKIDO_NATIVE (regclosekey) {
    if (!isIntegral(paras[1])) {
        throw newParameterException (vm, stack, "regclosekey", "Illegal key handle") ;
    }
    HKEY hkey = (HKEY)getInt (paras[1]) ;
    LONG res = RegCloseKey (hkey) ;
    if (res != ERROR_SUCCESS) {
        throw newException (vm, stack, "Error closing key") ;
    }
    return 0 ;
}
}		// extern "C"
}		// namespace
