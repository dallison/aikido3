/*
 * value.cc
 *
 * Aikido Language System,
 * export version: 1.00
 * Copyright (c) 2002-2003 Sun Microsystems, Inc.
 *
 * Sun Public License Notice
 * 
 * The contents of this file are subject to the Sun Public License Version 1.0 (the "License"). You
 * may not use this file except in compliance with the License. A copy of the License is available
 * at http://www.opensource.org/licenses/sunpublic.php
 * 
 * The Original Code is Aikido. 
 * The Initial Developer of the Original Code is David Allison on behalf of Sun Microsystems, Inc. 
 * Copyright (C) Sun Microsystems, Inc. 2000-2003. All Rights Reserved.
 * 
 * Closure support is Copyright (C) 2004 David Allison.  All rights reserved.
 * 
 * Contributor(s): dallison
 *
 * Version:  1.19
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 *
 * Change log:
 * ----------
 * 
 * 10/21/2004           David Allison           Added closure support
 */



//
// Values and the manipulation thereof
// 
// Also includes the garbage collector (reference counting). This is done
// in the Value destructor and assignment functions
//
// This file contains overloaded operators for values.
//

#include "aikido.h"
#include "aikidointerpret.h"
#include <stdlib.h>
#include "aikidocodegen.h"

#ifndef _OS_WINDOWS
#include <unistd.h>
#else
#include <fcntl.h>
#endif

namespace aikido {
extern bool interrupted ;

std::ostream &operator << (std::ostream &os, Value &v) {
    switch (v.type) {
    case T_INTEGER:
        return os << v.integer ;
        break ;
    case T_BYTE:
        os.put ((unsigned char)v.integer) ;     // binary output
        break ;
    case T_CHAR:
        return os << (char)v.integer ;
        break ;

    case T_REAL:
        return os << v.real ;

    case T_STRING:
        return os << *v.str ;
        break ;
    case T_VECTOR: {
        for (int i = 0 ; i < v.vec->size() ; i++) {
            os << (*v.vec)[i] ;
        }
        return os ;
        }
    case T_BYTEVECTOR:              // binary output of whole vector
        for (int i = 0 ; i < v.bytevec->size() ; i++) {
            os.put ((*v.bytevec)[i]) ;
        }
        return os ;

    case T_MEMORY:
        os << (void*)v.mem->mem ;
        break ;

    case T_POINTER:
        os << (void*)(v.pointer->mem->mem + v.pointer->offset) ;
        break ;

    case T_STREAM: {
#if 0
        char c ;
        while (!v.stream->eof()) {
            c = v.stream->get() ;
            if (!v.stream->eof()) {
                os.put (c) ;
            }
        }
#else
        const int BUFFERSIZE = 8192 ;
        char buf[BUFFERSIZE] ;
        while (!interrupted && !v.stream->eof()) {
            int n = v.stream->get (buf, BUFFERSIZE) ;
            os.write (buf, n) ;
        }
#endif
        return os ;
        break ;
        }
    case T_MAP: {
        map::iterator s = v.m->begin() ;
        map::iterator e = v.m->end() ;
        while (s != e) {
            map::value_type v = *s ;
            Value v1 = (*s).first ;
            Value &v2 = (*s).second ;
            os << v1 << '=' << v2 << ' ' ;
            s++ ;
        }
        break ;
        }

    case T_CLASS:
    case T_MONITOR:
    case T_PACKAGE:
    case T_ENUM:
    case T_FUNCTION:
    case T_THREAD:
        return os << v.block->name ;
        break ;

    case T_CLOSURE:
        if (v.closure->vm_state != NULL) {
            return os << v.closure->value ;
        } else {
            return os << v.closure->block->name ;
        }
        break ;

    case T_OBJECT:
        return os << "object " << (void*)v.object ;

    case T_ENUMCONST:
        return os << v.ec->name ;

    case T_NONE:
        return os << "none" ;

    case T_ADDRESS:
        return os << "0x" << v.addr ;
    }
}

inline std::istream &operator >> (std::istream &is, Value &v) {
    switch (v.type) {
    case T_INTEGER:
        return is >> v.integer ;
        break ;
    case T_BYTE:
        v.integer = is.get() ;
        return is ;

    case T_CHAR:
        v.integer = is.get() ;
        return is ;
        break ;
    case T_STRING:
        return is >> *v.str ;
        break ;

    case T_REAL:
        return is >> v.real ;
        break ;

    case T_VECTOR: {            // read each line into the vector
        readfile (is, *v.vec) ;
        return is ;
        }

    // read all available characters in the stream into the byte vector and set its length
    // XXX: improve performance by reading a buffer full at a time
    case T_BYTEVECTOR: {
        v.bytevec->vec.resize (0) ;     // reset size to 0
        while (!interrupted && !is.eof() && is.rdbuf()->in_avail() > 0) {
            char ch = is.get() ;
            if (!is.eof()) {
                v.bytevec->push_back ((unsigned char)ch) ;
            }
        }
        break ;
        }

    case T_FUNCTION: {          // XXX fix this
       return is ;
       }
    case T_STREAM: {
        char c ;
        while (!interrupted && !is.eof()) {
            c = is.get() ;
            if (!is.eof()) {
                v.stream->put (c) ;
            }
        }
        if (v.stream->autoflush) {
            v.stream->flush() ;
        }
        return is ;
        break ;
        }
    case T_MAP:
        break ;

    case T_MEMORY:
        return is.read ((char*)v.mem->mem, v.mem->size) ;
        break ;

    case T_POINTER:
        return is.read ((char*)(v.pointer->mem->mem + v.pointer->offset), v.pointer->mem->size - v.pointer->offset) ;
        break ;
    default:
    case T_NONE:
        return is ;
    }
}

Value::operator bool() const {       // conversion to boolean
    switch (type) {
    case T_INTEGER:
    case T_CHAR:
    case T_BYTE:
    return integer != 0 ;
    case T_REAL:
        return real != 0.0 ;

    case T_STRING:
    return str != NULL || *str != "" ;
    case T_VECTOR:
        return vec->size() != 0 ;
    case T_BYTEVECTOR:
        return bytevec->size() != 0 ;
    case T_MAP:
        return m->size() != 0 ;

    case T_STREAM:
        return !stream->eof() ;

    case T_OBJECT:
        return object != NULL ;

    case T_FUNCTION:
    case T_CLASS:
    case T_MONITOR:
    case T_PACKAGE:
    case T_THREAD:
    case T_ENUM:
    case T_ENUMCONST:
    case T_CLOSURE:
        return true ;

    case T_ADDRESS:
        return addr != NULL ;

    case T_MEMORY:
        return mem != NULL ;

    case T_POINTER:
        return pointer != NULL ;

    default:
    return false ;
    }
}



void Value::incBlockRef () {
    incRef (block, block) ;
}

Value Value::operator< (const Value &v) {
    switch (type) {
    case T_INTEGER:
    case T_BYTE:
    case T_CHAR:
        switch (v.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            return Value (integer < v.integer) ;
        
        case T_STRING:
            return Value (toString() < *v.str) ;
   
        case T_REAL:
            return Value ((double)integer < v.real) ;

        case T_VECTOR:
            break ;
        }
        break ;
    case T_REAL:
        switch (v.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            return Value (real < v.integer) ;
        
        case T_STRING:
            return Value (toString() < *v.str) ;
   
        case T_REAL:
            return Value (real < v.real) ;

        case T_VECTOR:
            break ;
        }
        break ;
    case T_STRING:
        switch (v.type) {
        case T_INTEGER: case T_CHAR: case T_REAL: case T_BYTE:
            return Value (*str < v.toString()) ;
        
        case T_STRING:
            return Value (*str < *v.str) ;
   
        case T_VECTOR:
            break ;
        }
        break ;
        
    case T_VECTOR:
        if (v.type == T_VECTOR) {
            return Value (*vec < *v.vec) ;
        } 
        break ;

    case T_MAP:
        if (v.type == T_MAP) {
            return Value (*m < *v.m) ;
        }
        break ;

    case T_ENUMCONST:
        if (v.type == T_ENUMCONST) {
            return Value (ec->en == v.ec->en && ec->offset < v.ec->offset) ;
        }
        break ;

    case T_OBJECT: {		// need to do correct comparison
        if (object == NULL || v.object == NULL) {
            return Value (object < v.object) ;
        }
        VirtualMachine *vm = object->block->p->vm ;
        InterpretedBlock *opfunc = vm->checkForOperator (*this, LESS) ;
        if (opfunc == NULL) {
            return Value (object < v.object) ;
        }
        Operand result ;
        StaticLink slink (NULL, object) ;
        vm->randomCall (&result, opfunc, object, &slink, *this, v) ;
        return result.val ;
        }
        break ;

    }
    return Value (0) ;
}

Value Value::operator== (const Value &v) {
    switch (type) {
    case T_INTEGER: case T_BYTE:
    case T_CHAR:
        switch (v.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            return Value (integer == v.integer) ;
        
        case T_STRING:
            return Value (toString() == *v.str) ;
   
        case T_REAL:
            return Value ((double)integer == v.real) ;

        case T_VECTOR:
            break ;
        }
        break ;
    case T_REAL:
        switch (v.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            return Value (real == v.integer) ;
        
        case T_STRING:
            return Value (toString() == *v.str) ;
   
        case T_REAL:
            return Value (real == v.real) ;

        case T_VECTOR:
            break ;
        }
        break ;
    case T_STRING:
        switch (v.type) {
        case T_INTEGER: case T_CHAR: case T_REAL: case T_BYTE:
            return Value (*str == v.toString()) ;
        
        case T_STRING:
            return Value (*str == *v.str) ;
   
        case T_VECTOR:
            break ;
        }
        break ;
        
    case T_VECTOR:
        if (v.type == T_VECTOR) {
            return Value (*vec == *v.vec) ;
        } 
        break ;

    case T_BYTEVECTOR:
        if (v.type == T_BYTEVECTOR) {
            return Value (*bytevec == *v.bytevec) ;
        } 
        break ;

    case T_MAP:
        if (v.type == T_MAP) {
            return Value (*m == *v.m) ;
        }
        break ;

    case T_ENUMCONST:
        if (v.type == T_ENUMCONST) {
            return Value (ec->en == v.ec->en && ec->offset == v.ec->offset) ;
        }
        break ;

    case T_CLASS:
    case T_INTERFACE:
    case T_PACKAGE:
    case T_FUNCTION:
    case T_ENUM:
    case T_THREAD:
    case T_MONITOR:
        return block == v.block ;

    case T_OBJECT:
        return object == v.object ;

    case T_CLOSURE:
        return closure->block == v.closure->block ;
    }
    return Value (0) ;
}

int Value::size() {
    switch (type) {
    case T_INTEGER: case T_BYTE:
    case T_CHAR:
    case T_ENUMCONST:
    case T_REAL:
        return 1 ;
    case T_STRING:
        return str->size() ;
    case T_VECTOR:
        return vec->size() ;
    case T_BYTEVECTOR:
        return bytevec->size() ;
    case T_MAP:
	return m->size() ;
    case T_OBJECT: {
	Block *blk = object->block ;
	return blk->stacksize ;
        }

    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD:
	return block->stacksize ;

    case T_CLOSURE:
        if (closure->vm_state != NULL) {
            return closure->value.size() ;
        }
        return closure->block->stacksize ;

    default:
	    return 0 ;
    }
}

string Value::toString() const {
    char buf[64] ;
    switch (type) {
    case T_INTEGER:
        sprintf (buf, "%lld", integer) ;
        break ;
    case T_REAL:
        sprintf (buf, "%g", real) ;
        break ;
    case T_BYTE:
        buf[0] = (char)integer ;
        return string (buf, 1) ;
        break ;
    case T_CHAR:
        sprintf (buf, "%c", (char)integer) ;
        break ;
    }
    return buf ;
}



Value Value::operator[] (int i) const {			// array subscript
    switch (type) {
    case T_INTEGER: case T_CHAR: case T_BYTE:    			// for integer, it means a bit number
        return Value (((UINTEGER)(integer & (1 << i))) >> i) ;

    case T_STRING:
        return Value ((char)(str->data()[i])) ;
        //return Value (str->substr (i, 1)) ;

    case T_VECTOR:
        return (*vec)[i] ;

    case T_BYTEVECTOR:
        return (*bytevec)[i] ;

    case T_ENUM:
        return Value (en->consts[i]) ;
        
    case T_NONE:
        return Value (0) ;
    }
}

Value Value::operator[] (const Value &v) const {
    switch (type) {
    case T_INTEGER: case T_CHAR: case T_BYTE:
        return Value (0) ;

    case T_STRING:
        //return Value ((int)str->find (*v.str)) ; 	// not used since regular exp[ressions
        break ;

    case T_VECTOR: 
        return (*vec)[v.integer] ;

    case T_BYTEVECTOR: 
        return (*bytevec)[v.integer] ;

    case T_MAP: {
        map::ValueMap::iterator result = m->find (v) ;
        if (result != m->end()) {
            return (*result).second ;
        } else {
            return Value() ;
        }
        }

    default:
        return Value (0) ;
    }
}
 
bool operator== (Value::vector &v1, Value::vector &v2) {
    if (v1.size() != v2.size()) {
        return false ;
    }
    Value::vector::iterator i1, i2 ;
    i1 = v1.begin() ;
    i2 = v2.begin() ;

    for (int i = 0 ; i < v1.size() ; i++, i1++, i2++) {
        if (*i1 != *i2) {
            return false ;
        }
    }
    return true ;
}

bool operator!= (Value::vector &v1, Value::vector &v2) {
     return !(v1 == v2) ;
}

bool operator< (Value::vector &v1, Value::vector &v2) {
    return v1.size() < v2.size() ;
}

bool operator> (Value::vector &v1, Value::vector &v2) {
    return v1.size() > v2.size() ;
}

bool operator>= (Value::vector &v1, Value::vector &v2) {
    return v1.size() >= v2.size() ;
}

bool operator<= (Value::vector &v1, Value::vector &v2) {
    return v1.size() <= v2.size() ;
}

Value::vector operator+ (Value::vector &v1, Value::vector &v2) {
    Value::vector result ;
    for (int i = 0 ; i < v1.size() ; i++) {
        result.push_back (v1[i]) ;
    }
    for (int i = 0 ; i < v2.size() ; i++) {
        result.push_back (v2[i]) ;
    }
    return result ;
}

Value::vector operator<< (Value::vector &v1, Value::vector &v2) {
    Value::vector result ;
    return result ;
}

Value::vector operator>> (Value::vector &v1, Value::vector &v2) {
    Value::vector result ;
    return result ;
}

Value::vector operator&(Value::vector &v1, Value::vector &v2) {		// set intersection
    Value::vector result ;
    Value::vector::iterator i,j ;
    for (i = v1.begin() ; i != v1.end() ; i++) {
        for (j = v2.begin() ; j != v2.end() ; j++) {
            if (*i == *j) {
                result.push_back (*i) ;
            }
        }
    }
    return result ;
}

Value::vector operator| (Value::vector &v1, Value::vector &v2) {		// set union
    Value::vector result ;
    for (Value::vector::iterator i = v1.begin() ; i != v1.end() ; i++) {
        Value::vector::iterator j ;
        for (j = result.begin() ; j != result.end() ; j++) {
            if (*i == *j) {
                break ;
            }
        }
        if (j == result.end()) {
            result.push_back (*i) ;
        }
    }
    for (Value::vector::iterator i = v2.begin() ; i != v2.end() ; i++) {
        Value::vector::iterator j ;
        for (j = result.begin() ; j != result.end() ; j++) {
            if (*i == *j) {
                break ;
            }
        }
        if (j == result.end()) {
            result.push_back (*i) ;
        }
    }
    return result ;
}

Value::vector operator- (Value::vector &v1, Value::vector &v2) {		// set difference
    Value::vector result ;
    Value::vector common = v1 & v2 ;			// get all common elements
    for (Value::vector::iterator i = v1.begin() ; i != v1.end() ; i++) {
        Value::vector::iterator j ;
        for (j = common.begin() ; j != common.end() ; j++) {
            if (*i == *j) {
                break ;
            }
        }
        if (j == common.end()) {
            result.push_back (*i) ;
        }
    }
    for (Value::vector::iterator i = v2.begin() ; i != v2.end() ; i++) {
        Value::vector::iterator j ;
        for (j = common.begin() ; j != common.end() ; j++) {
            if (*i == *j) {
                break ;
            }
        }
        if (j == common.end()) {
            result.push_back (*i) ;
        }
    }
    
    return result ;
}


//
// bytevector
//

bool operator== (Value::bytevector &v1, Value::bytevector &v2) {
    if (v1.size() != v2.size()) {
        return false ;
    }
    Value::bytevector::iterator i1, i2 ;
    i1 = v1.begin() ;
    i2 = v2.begin() ;

    for (int i = 0 ; i < v1.size() ; i++, i1++, i2++) {
        if (*i1 != *i2) {
            return false ;
        }
    }
    return true ;
}

bool operator!= (Value::bytevector &v1, Value::bytevector &v2) {
     return !(v1 == v2) ;
}

bool operator< (Value::bytevector &v1, Value::bytevector &v2) {
    return v1.size() < v2.size() ;
}

bool operator> (Value::bytevector &v1, Value::bytevector &v2) {
    return v1.size() > v2.size() ;
}

bool operator>= (Value::bytevector &v1, Value::bytevector &v2) {
    return v1.size() >= v2.size() ;
}

bool operator<= (Value::bytevector &v1, Value::bytevector &v2) {
    return v1.size() <= v2.size() ;
}

Value::bytevector operator+ (Value::bytevector &v1, Value::bytevector &v2) {
    Value::bytevector result ;
    for (int i = 0 ; i < v1.size() ; i++) {
        result.push_back (v1[i]) ;
    }
    for (int i = 0 ; i < v2.size() ; i++) {
        result.push_back (v2[i]) ;
    }
    return result ;
}

Value::bytevector operator<< (Value::bytevector &v1, Value::bytevector &v2) {
    Value::bytevector result ;
    return result ;
}

Value::bytevector operator>> (Value::bytevector &v1, Value::bytevector &v2) {
    Value::bytevector result ;
    return result ;
}

Value::bytevector operator&(Value::bytevector &v1, Value::bytevector &v2) {		// set intersection
    Value::bytevector result ;
    Value::bytevector::iterator i,j ;
    for (i = v1.begin() ; i != v1.end() ; i++) {
        for (j = v2.begin() ; j != v2.end() ; j++) {
            if (*i == *j) {
                result.push_back (*i) ;
            }
        }
    }
    return result ;
}

Value::bytevector operator| (Value::bytevector &v1, Value::bytevector &v2) {		// set union
    Value::bytevector result ;
    for (Value::bytevector::iterator i = v1.begin() ; i != v1.end() ; i++) {
        Value::bytevector::iterator j ;
        for (j = result.begin() ; j != result.end() ; j++) {
            if (*i == *j) {
                break ;
            }
        }
        if (j == result.end()) {
            result.push_back (*i) ;
        }
    }
    for (Value::bytevector::iterator i = v2.begin() ; i != v2.end() ; i++) {
        Value::bytevector::iterator j ;
        for (j = result.begin() ; j != result.end() ; j++) {
            if (*i == *j) {
                break ;
            }
        }
        if (j == result.end()) {
            result.push_back (*i) ;
        }
    }
    return result ;
}

Value::bytevector operator- (Value::bytevector &v1, Value::bytevector &v2) {		// set difference
    Value::bytevector result ;
    Value::bytevector common = v1 & v2 ;			// get all common elements
    for (Value::bytevector::iterator i = v1.begin() ; i != v1.end() ; i++) {
        Value::bytevector::iterator j ;
        for (j = common.begin() ; j != common.end() ; j++) {
            if (*i == *j) {
                break ;
            }
        }
        if (j == common.end()) {
            result.push_back (*i) ;
        }
    }
    for (Value::bytevector::iterator i = v2.begin() ; i != v2.end() ; i++) {
        Value::bytevector::iterator j ;
        for (j = common.begin() ; j != common.end() ; j++) {
            if (*i == *j) {
                break ;
            }
        }
        if (j == common.end()) {
            result.push_back (*i) ;
        }
    }
    
    return result ;
}



bool operator== (map &v1, map &v2) {
    if (v1.size() != v2.size()) {
        return false ;
    }
    map::iterator i1, i2 ;
    i1 = v1.begin() ;
    i2 = v2.begin() ;

    for (int i = 0 ; i < v1.size() ; i++, i1++, i2++) {
        if (*i1 != *i2) {
            return false ;
        }
    }
    return true ;
}

bool operator!= (map &v1, map &v2) {
     return !(v1 == v2) ;
}

bool operator< (map &v1, map &v2) {
    return v1.size() < v2.size() ;
}

bool operator> (map &v1, map &v2) {
    return v1.size() > v2.size() ;
}

bool operator>= (map &v1, map &v2) {
    return v1.size() >= v2.size() ;
}

bool operator<= (map &v1, map &v2) {
    return v1.size() <= v2.size() ;
}

map operator+ (map &v1, map &v2) {
    map result ;
    result.insert (v1.begin(), v1.end()) ;
    result.insert (v2.begin(), v2.end()) ;
    return result ;
}

map operator<< (map &v1, map &v2) {
    map result ;
    return result ;
}

map operator>> (map &v1, map &v2) {
    map result ;
    return result ;
}


Value::Value (Object *o) { 
    integer = 0 ;
    object = o ; 
    type = T_OBJECT ; 
    if (o != NULL) {
        incRef(o,object) ;
    }
}


void Value::destruct() {
    if (isRefCounted (type)) {
         switch (type) {
         case T_MEMORY:
             if (decRef(mem,memory) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting raw memory " << (void*)mem << '\n' ;
                 }
                 GCDELETE(mem,memory) ;
             }
             mem = NULL ;
             break ;
         
         case T_POINTER:
             if (decRef(pointer,pointer) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting raw pointer " << (void*)pointer << '\n' ;
                 }
                 GCDELETE(pointer,pointer) ;
             }
             mem = NULL ;
             break ;
         
         case T_VECTOR:
             if (decRef(vec,vector) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting vector " << (void*)vec << '\n' ;
                 }
                 GCDELETE(vec,vector) ;
             }
             vec = NULL ;
             break ;
         case T_BYTEVECTOR:
             if (decRef(bytevec,bytevector) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting bytevector " << (void*)vec << '\n' ;
                 }
                 GCDELETE(bytevec,bytevector) ;
             }
             bytevec = NULL ;
             break ;
         
         case T_MAP:
             if (decRef(m,map) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting map " << (void*)m << '\n' ;
                 }
                 GCDELETE (m,map) ;
             }
             m = NULL ;
             break ;
         case T_STRING:
             if (decRef(str,string) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting string \"" << *str << "\" (" << (void*)str << ")\n" ;
                 }
                 GCDELETE (str,string) ;
             }
             str = NULL ;
             break ;
         case T_STREAM:
             if (decRef(stream,stream) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting stream " << (void*)stream << '\n' ;
                 }
                 GCDELETE (stream, stream) ;
             }
             stream = NULL ;
             break ;
         case T_OBJECT:
             if (object != NULL) {
                 if (decRef(object,object) == 1) {		// object always contains pointer to itself
                     object->block->p->freeMonitor (object) ;
                     object->ref = 100 ;			// prevent recursive calls by finalize
                     object->destruct() ;
                     if (verbosegc) {
                         std::cerr << "gc: deleting object " << (void*)object << " (instance of " << object->block->name << ")\n" ;
                     }
                     GCDELETE (object,object) ;
                     //free (object) ;
                     object = NULL ;
                 }
             }
             break ;

         case T_PACKAGE:
         case T_CLASS:
         case T_INTERFACE:
         case T_MONITOR:
         case T_FUNCTION:
         case T_THREAD:
             if (decRef(block,block) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting block " << block->name << '\n' ;
                 }
                 GCDELETE (block, block) ;
             }
             block = NULL ;
             break ;

         case T_CLOSURE:
             if (decRef(closure,closure) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting closure " << closure->block->name << '\n' ;
                 }
                 if (closure->vm_state != NULL) {
                     VirtualMachine::free_state (closure->vm_state) ;
                     closure->vm_state = NULL ;
                 }
                 GCDELETE (closure, closure) ;
             }
             block = NULL ;
             break ;

         }
    }
    type = T_NONE ;
}


//
// for assignment to a value, we may be overwriting that is reference
// counted.  In this case we need to destruct the current value.  But
// before doing that, we need to increment the reference count for
// the thing we are setting the value to (in case it's the same
// reference counted object)
//

Value &Value::operator= (const Value &v) {
    if (isRefCounted (v.type)) {		// new value is reference counted?
        switch (v.type) {
        case T_STRING:
            incRef(v.str,string) ;
            break ;
    
        case T_VECTOR:
            incRef(v.vec,vector) ;
            break ;
    
        case T_MEMORY:
            incRef(v.mem,memory) ;
            break ;
    
        case T_POINTER:
            incRef(v.pointer,pointer) ;
            break ;
    
        case T_BYTEVECTOR:
            incRef(v.bytevec,bytevector) ;
            break ;
    
        case T_FUNCTION:
           incRef (v.func, function) ;
	    break ;
    
        case T_STREAM:
           incRef(v.stream,stream) ;
           break ;
    
        case T_THREAD:
           incRef (v.thread, thread) ;
           break ;
           
        case T_MAP:
           incRef(v.m,map) ;
           break ;
    
        case T_OBJECT:
           if (v.object != NULL) {
               incRef(v.object,object) ;
           }
           break ;			// assignment of v.object to object was done above
    
        case T_CLASS:
           incRef (v.cls, class) ;
           break ;
    
        case T_CLOSURE:
           incRef (v.closure, closure) ;
           break ;
    
        case T_INTERFACE:
           incRef (v.iface, interface) ;
           break ;
    
        case T_MONITOR:
           incRef (v.block, monitor) ;
           break ;
    
        case T_PACKAGE:
           incRef (v.package, package) ;
           break ;
        }
    }

    // now destruct the current value
    if (isRefCounted (type)) {
         switch (type) {
         case T_VECTOR:
             if (decRef(vec,vector) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting vector " << (void*)vec << '\n' ;
                 }
                 GCDELETE(vec,vector) ;
             }
             break ;
         case T_MEMORY:
             if (decRef(mem,memory) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting raw memory " << (void*)mem << '\n' ;
                 }
                 GCDELETE(mem,memory) ;
             }
             break ;
         case T_POINTER:
             if (decRef(pointer,pointer) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting raw pointer " << (void*)pointer << '\n' ;
                 }
                 GCDELETE(pointer,pointer) ;
             }
             break ;
         case T_BYTEVECTOR:
             if (decRef(bytevec,bytevector) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting bytevector " << (void*)vec << '\n' ;
                 }
                 GCDELETE(bytevec,bytevector) ;
             }
             break ;
         
         case T_MAP:
             if (decRef(m,map) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting map " << (void*)m << '\n' ;
                 }
                 GCDELETE (m,map) ;
             }
             break ;
         case T_STRING:
             if (decRef(str,string) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting string \"" << *str << "\" (" << (void*)str << ")\n" ;
                 }
                 GCDELETE (str,string) ;
             }
             break ;
         case T_STREAM:
             if (decRef(stream,stream) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting stream " << (void*)stream << '\n' ;
                 }
                 GCDELETE (stream, stream) ;
             }
             break ;
         case T_OBJECT:
             if (object != NULL) {
                 if (decRef(object,object) == 1) {		// object always contains pointer to itself
                     object->block->p->freeMonitor (object) ;
                     object->ref = 100 ;			// prevent recursive calls by finalize
                     object->destruct() ;
                     if (verbosegc) {
                         std::cerr << "gc: deleting object " << (void*)object << " (instance of " << object->block->name << ")\n" ;
                     }
                     GCDELETE (object,object) ;
                 }
             }
             break ;

         case T_PACKAGE:
         case T_CLASS:
         case T_INTERFACE:
         case T_MONITOR:
         case T_FUNCTION:
         case T_THREAD:
             if (decRef(block,block) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting block " << block->name << '\n' ;
                 }
                 GCDELETE (block, block) ;
             }
             break ;

         case T_CLOSURE:
             if (decRef(closure,closure) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting closure " << closure->block->name << '\n' ;
                 }
                 if (closure->vm_state != NULL) {
                     VirtualMachine::free_state (closure->vm_state) ;
                     closure->vm_state = NULL ;
                 }
                 GCDELETE (closure, closure) ;
             }
             break ;
         }
    }

    // copy all fields from the new value
    type = v.type ;
    integer = v.integer ;

    // truncate bytes
    if (type == T_BYTE) {
        integer &= 0xff ;
    }
    return *this ;
}

// copy constructor
Value::Value (const Value &v) {
    type = v.type ;
    integer = v.integer ;
    if (isRefCounted (type)) {
        switch (type) {
        case T_STRING:
            incRef(str,string) ;
            break ;
        case T_VECTOR:
            incRef(vec,vector) ;
            break ;
        case T_MEMORY:
            incRef(mem,memory) ;
            break ;
        case T_POINTER:
            incRef(pointer,pointer) ;
            break ;
        case T_BYTEVECTOR:
            incRef(bytevec,bytevector) ;
            break ;
        case T_FUNCTION:
            incRef(func,function) ;
    	break ;
    
        case T_STREAM:
           incRef(stream,stream) ;
           break ;
    
        case T_THREAD:
           incRef(thread,thread) ;
           break ;
           
        case T_MAP:
           incRef(m,map) ;
           break ;
    
        case T_OBJECT:
           if (object != NULL) {
               incRef(object,object) ;
           }
           break ;			// assignment already done above
    
        case T_CLASS:
           incRef(cls,class) ;
           break ;
    
        case T_CLOSURE:
           incRef(closure,closure) ;
           break ;
    
        case T_INTERFACE:
           incRef(iface,interface) ;
           break ;
    
        case T_MONITOR:
           incRef(block,monitor) ;
           break ;
    
        case T_PACKAGE:
           incRef(package,package) ;
           if (package->object != NULL) {
               incRef (package->object,pobject) ;
           }
           break ;
        }
    }
}
    

EnumConst *EnumConst::nextValue (int n) {
    EnumConst *ec = this ;
    while (n-- && ec != NULL) {
        ec = ec->next ;
    }
    return ec ;
}

EnumConst *EnumConst::prevValue (int n) {
    EnumConst *ec = this ;
    while (n-- && ec != NULL) {
        ec = ec->prev ;
    }
    return ec ;
}


//
// destruct an object by calling its destructor
//

void Object::destruct() {
    if (block->finalize != NULL) {
        VirtualMachine vm (block->p) ;
        Operand res ;
        StaticLink sl (NULL, this) ;
        vm.randomCall (&res, block->finalize, this, this, &sl) ;
    }
    // remove reference to block and delete if 0
    if (decRef (block, block) == 0) {
         if (verbosegc) {
             std::cerr << "gc: deleting block " << block->name << '\n' ;
         }
         GCDELETE (block, block) ;
    }
}


void Object::acquire(bool force) {
   //std::cerr << "object acquired\n" ;
}

void Object::release(bool force) {
}



// streams

void StdStream::readValue (Value &v) {
   *stream >> v ;
}

void StdStream::writeValue (Value &v) {
   *stream << v ;
   if (autoflush) {
       stream->flush() ;
    }
}

StdOutStream::StdOutStream (std::ostream *s) : stream(s) {
}

void StdOutStream::readValue (Value &v) {
    throw "Unable to read from an output stream" ;
}

void StdOutStream::writeValue (Value &v) {
   *stream << v ;
   if (autoflush) {
       stream->flush() ;
    }
}

StdInStream::StdInStream (std::istream *s) : stream(s) {
}

void StdInStream::readValue (Value &v) {
   *stream >> v ;
}

void StdInStream::writeValue (Value &v) {
    throw "Unable to write to an input stream" ;
}

void PipeStream::readValue (Value &v) {
   input >> v ;
}

void PipeStream::writeValue (Value &v) {
   output << v ;
   if (autoflush) {
       output.flush() ;
    }
}



int StdStream::seek (int offset, int whence) {
#if !defined(_CC_GCC)
    switch (whence) {
    case 0:			// SEEK_SET
        return stream->rdbuf()->pubseekoff (offset, std::ios_base::beg) ;
        break ;
    case 1:			// SEEK_CUR
        return stream->rdbuf()->pubseekoff (offset, std::ios_base::cur) ;
        break ;
    case 2:			// SEEK_END
        return stream->rdbuf()->pubseekoff (offset, std::ios_base::end) ;
        break ;
    }
#else
    switch (whence) {
    case 0:			// SEEK_SET
        return stream->rdbuf()->pubseekoff (offset, std::ios::beg) ;
        break ;
    case 1:			// SEEK_CUR
        return stream->rdbuf()->pubseekoff (offset, std::ios::cur) ;
        break ;
    case 2:			// SEEK_END
        return stream->rdbuf()->pubseekoff (offset, std::ios::end) ;
        break ;
    }
#endif
    return -1 ;
}

int PipeStream::seek (int offset, int whence) {
    return -1 ;
}


void StdStream::rewind() {
   stream->rdbuf()->pubseekpos (0) ; stream->clear() ;
}

void PipeStream::rewind() {
}


int StdOutStream::seek (int offset, int whence) {
    return -1 ;
}


void StdOutStream::rewind() {
}

int StdInStream::seek (int offset, int whence) {
    return -1 ;
}


void StdInStream::rewind() {
}


ThreadStream::ThreadStream (Aikido *d, ThreadStream *p, Thread *t, StackFrame *f) : aikido (d), peer (p), thread (t), stack(f), input (NULL), output (NULL), connected (false) {
}

ThreadStream::~ThreadStream() {
    if (input != NULL) {
        delete input ;
    }
    if (output != NULL) {
        delete output ;
    }
}

void ThreadStream::readValue (Value &v) {
   checkstreams() ;
   *input >> v ;
}

void ThreadStream::writeValue (Value &v) {
   checkstreams() ;
   *output << v ;
   if (autoflush) {
       output->flush() ;
    }
}

void ThreadStream::rewind() {
}

int ThreadStream::seek (int offset, int whence) {
    return -1 ;
}

// This function is machine dependent 

#if defined(_OS_SOLARIS) 
#include "unix/connect.cc"
#elif defined(_OS_LINUX)
#include "unix/connect.cc"
#elif defined(_OS_MACOSX)
#include "unix/connect.cc"
#elif defined(_OS_WINDOWS)
#include "win32/connect.cc"
#endif


void readfile (std::istream &is, Value::vector &v) {
    const int BUFFERSIZE = 1024*1024 ;
    char *fb = new char [BUFFERSIZE] ;          // performance buffer (too big for stack)
    char linebuf[4096] ;
    char *s, *p ;

    p = linebuf ;

    string *str = new string() ;
// 2 version of this loop.  With GCC 3.1 and greater, we can't use the
// underlying file descriptor so we just use the iostream read functions.
// for other compilers, we can use the file descriptor  because it's faster
#if defined(_OS_WINDOWS) || (defined(_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
    while (!interrupted && !is.eof()) {
        is.read (fb, BUFFERSIZE) ;		// read data into large buffer
        int n = is.gcount() ;
#else
    int fd = static_cast<std::filebuf*>(is.rdbuf())->fd() ;
    while (!interrupted && !is.eof()) {
        int n = read (fd, fb, BUFFERSIZE) ;
#endif
        if (n <= 0) {
            is.clear() ;
            break ;
        }
        s = fb ;
        // process one buffer full of lines
        while (n > 0) {
            while (n > 0 && *s != '\n') {	// read one line into line bufer
                if (p < linebuf + 4094) {		// any room in line buffer?
                    *p++ = *s++ ;		// add it to buffer
                } else {
                    *p = 0 ;		// terminate line
                    *str += linebuf ;		// append to string
                    p = linebuf ;
                }
                --n ;
            }
            // at the end of the loop, str contains the start of the line and linebuf
            // contains the rest of the characters
            if (*s == '\n') {
                // terminate line buffer with cr + eol
                *p++ = *s++ ;
                *p = 0 ;
                // append line buffer to string
                *str += linebuf ;
                // append string to vector
                v.push_back (str) ;

                // new empty string for next line
                str = new string() ;
                n-- ;		// one few chars in file buffer
                p = linebuf ;	// reset to start of line buffer
            }
        }
    }
    delete [] fb ;

#if 0
        noline = false ;
        is.get (buf, BUFFERSIZE, '\n') ;            // read one buffer full
        *str += buf ;                       // append it
        int n = is.gcount() ;
        if (n == 0) {
            is.clear() ;                    // the failbit is set if no chars are read
        }
        if (n < (BUFFERSIZE - 1)) {             // line shorter than buffer?
            *str += '\n' ;                  // put terminator in line
            v.vec->push_back (str) ;
            str = new string() ;
            is.get() ;                      // remove terminator from stream
            noline = true ;
        }
    }
#endif

    if (str->size() > 0) {
        *str += '\n' ;                      // put terminator in line
        v.push_back (str) ;
    } else {
        delete str ;
    }
}


//
// closures
//

Closure::Closure (Type t, Block *blk, StaticLink *sl) : type(t), block(blk), slink(NULL), vm_state(NULL) {
    // copy the static link chain into the closure
    StaticLink *last = NULL ;
    if (sl == NULL) {
        abort() ;
    }

    while (sl != NULL) {
        StaticLink *newlink = new StaticLink (NULL, sl->frame) ;
        incRef (sl->frame, stackframe) ;
        if (slink == NULL) {
            slink = last = newlink ;
        } else {
            last->next = newlink ;
            last = newlink ;
        }
        if (sl->next == sl) {
            break ;
        }
        sl = sl->next ;
    }
    incRef (block, block) ;
}

Closure::~Closure() {
    //std::cerr << "deleting closure\n" ;
    // traverse the static link chain, decrementing reference counts to the frames
    while (slink != NULL) {
        if (decRef (slink->frame, stackframe) == 0) {
            if (verbosegc) {
                std::cerr << "gc: deleting frame " << slink->frame << " due to closure deletion\n" ;
            }
            delete slink->frame ;
        }
        StaticLink *next = slink->next ;
        delete slink ;
        if (next == slink) {
            break ;
        }
        slink = next ;
    }
    if (decRef(block, block) == 0) {
        if (verbosegc) {
            std::cerr << "gc: deleting block " << block << " due to closure deletion\n" ;
        }
        delete block ;
    }
}

Value::Value (Closure *c) {
    integer = 0 ; type = T_CLOSURE ; closure = c ; incRef (closure, closure) ;
}

Value &Value::operator= (Closure *c) {
    destruct() ; closure = c ; type = T_CLOSURE ; incRef (closure, closure) ; return *this ;
}

void incValueRef (Value &v) {
    if (isRefCounted (v.type)) {		// new value is reference counted?
        switch (v.type) {
        case T_STRING:
            incRef(v.str,string) ;
            break ;
    
        case T_VECTOR:
            incRef(v.vec,vector) ;
            break ;
    
        case T_MEMORY:
            incRef(v.mem,memory) ;
            break ;
    
        case T_POINTER:
            incRef(v.pointer,pointer) ;
            break ;
    
        case T_BYTEVECTOR:
            incRef(v.bytevec,bytevector) ;
            break ;
    
        case T_FUNCTION:
           incRef (v.func, function) ;
	    break ;
    
        case T_STREAM:
           incRef(v.stream,stream) ;
           break ;
    
        case T_THREAD:
           incRef (v.thread, thread) ;
           break ;
           
        case T_MAP:
           incRef(v.m,map) ;
           break ;
    
        case T_OBJECT:
           if (v.object != NULL) {
               incRef(v.object,object) ;
           }
           break ;			// assignment of v.object to object was done above
    
        case T_CLASS:
           incRef (v.cls, class) ;
           break ;
    
        case T_CLOSURE:
           incRef (v.closure, closure) ;
           break ;
    
        case T_INTERFACE:
           incRef (v.iface, interface) ;
           break ;
    
        case T_MONITOR:
           incRef (v.block, monitor) ;
           break ;
    
        case T_PACKAGE:
           incRef (v.package, package) ;
           break ;
        }
    }
}

void decValueRef (Value &v) {
    if (isRefCounted (v.type)) {
         switch (v.type) {
         case T_MEMORY:
             if (decRef(v.mem,memory) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting raw memory " << (void*)v.mem << '\n' ;
                 }
                 GCDELETE(v.mem,memory) ;
             }
             break ;
         
         case T_POINTER:
             if (decRef(v.pointer,pointer) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting raw pointer " << (void*)v.pointer << '\n' ;
                 }
                 GCDELETE(v.pointer,pointer) ;
             }
             break ;
         
         case T_VECTOR:
             if (decRef(v.vec,vector) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting vector " << (void*)v.vec << '\n' ;
                 }
                 GCDELETE(v.vec,vector) ;
             }
             break ;
         case T_BYTEVECTOR:
             if (decRef(v.bytevec,bytevector) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting bytevector " << (void*)v.bytevec << '\n' ;
                 }
                 GCDELETE(v.bytevec,bytevector) ;
             }
             break ;
         
         case T_MAP:
             if (decRef(v.m,map) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting map " << (void*)v.m << '\n' ;
                 }
                 GCDELETE (v.m,map) ;
             }
             break ;
         case T_STRING:
             if (decRef(v.str,string) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting string \"" << *v.str << "\" (" << (void*)v.str << ")\n" ;
                 }
                 GCDELETE (v.str,string) ;
             }
             break ;
         case T_STREAM:
             if (decRef(v.stream,stream) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting stream " << (void*)v.stream << '\n' ;
                 }
                 GCDELETE (v.stream, stream) ;
             }
             break ;
         case T_OBJECT:
             if (v.object != NULL) {
                 if (decRef(v.object,object) == 1) {		// object always contains pointer to itself
                     v.object->block->p->freeMonitor (v.object) ;
                     v.object->ref = 100 ;			// prevent recursive calls by finalize
                     v.object->destruct() ;
                     if (verbosegc) {
                         std::cerr << "gc: deleting object " << (void*)v.object << " (instance of " << v.object->block->name << ")\n" ;
                     }
                     GCDELETE (v.object,object) ;
                 }
             }
             break ;

         case T_PACKAGE:
         case T_CLASS:
         case T_INTERFACE:
         case T_MONITOR:
         case T_FUNCTION:
         case T_THREAD:
             if (decRef(v.block,block) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting block " << v.block->name << '\n' ;
                 }
                 GCDELETE (v.block, block) ;
             }
             break ;

         case T_CLOSURE:
             if (decRef(v.closure,closure) == 0) {
                 if (verbosegc) {
                     std::cerr << "gc: deleting closure " << v.closure->block->name << '\n' ;
                 }
                 if (v.closure->vm_state != NULL) {
                     VirtualMachine::free_state (v.closure->vm_state) ;
                     v.closure->vm_state = NULL ;
                 }
                 GCDELETE (v.closure, closure) ;
             }
             break ;

         }
    }
}




}


