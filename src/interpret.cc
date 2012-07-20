/*
 * interpret.cc
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
 * Portions created by David Allison of PathScale Inc. are Copyright (C) PathScale Inc. 2004. All
 * Rights Reserved.
 * 
 * Closure support is Copyright (C) 2004 David Allison.  All rights reserved.
 *
 * Contributor(s): dallison
 *
 * Version:  1.30
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */

// CHANGE LOG
// 1/21/2004	David Allison of PathScale Inc.
//		Added insignalhandler checking in the signal handler
//		Allowed comparison of string and null
//		When reporting subscript errors, report the same order as the source code
//              Check for a null macro before instantiating
//              Fix this pointer in signal handler
// 10/21/2004   David Allison
//              Added closure support



#include "aikidointerpret.h"
#include "aikidodebug.h"
#include <signal.h>

#ifdef JAVA
#include "java/class.h"
#endif

#include <math.h>

bool tracecode ;

#ifdef JAVA
// assembly language function for call to JNI native functions
extern "C" int sysInvokeNative (void *jnienv, void *func, int *javastack, const char *descriptor,
                                int nargs, java::Class *cls) ;

namespace java {
extern void *jni_NativeInterface ;
}

#endif

namespace aikido {

extern bool interrupt ;
extern void interruptHit (int) ;

extern Monitor parserLock ;          // in native.cc

extern Object *parserDelegate;

using namespace codegen ;

Package *system ;
Class *pairClass ;
Class *regexClass ;
Function *objectFactory;

void printStaticLinks (StaticLink *slink) {
    int i = 0;
    do {
        std::cerr << "SLINK " << (i++) << ": next: " << slink->next << ", frame: " << slink->frame << " (" << slink->frame->block->name << ")\n";
        slink = slink->next;
    } while (slink != NULL && slink->frame->block->name != "main");
    if (slink != NULL) {
        std::cerr << "SLINK " << (i++) << ": next: " << slink->next << ", frame: " << slink->frame << " (" << slink->frame->block->name << ")\n";
    }
}

// decrement the reference count of an object and delete it if necessary

void decObjectRef (Object *obj) {
    if (decRef(obj,object) == 1) {              // object always contains pointer to itself
        obj->block->p->freeMonitor (obj) ;
        obj->ref = 100 ;                        // prevent recursive calls by finalize
        obj->destruct() ;
        if (verbosegc) {
            std::cerr << "gc: deleting object " << (void*)obj << " (instance of " << obj->block->name << ")\n" ;
        }
        GCDELETE (obj,object) ;
    }
}


//
// assignment to an object pointer (increments refcount)
//
Value &Value::operator= (Object *o) {
    if (o != NULL) {
        incRef(o,object) ;
    }
    destruct() ;
    object = o ;
    type = T_OBJECT ;
    return *this ;
}

//
// assignment type checking functions (various)
//

inline bool checkenum (const Value &val, Type t) {
    if (val.type == T_ENUMCONST && t == T_ENUM) {
        return true ;
    }
    return false ;
}

//
// this function is replicated for performance reasons (reduction of calls to constructors)
//

inline void VirtualMachine::checkType (const string &varname, const Value &val, Type t, const string &typen, Object *obj) {
    if (val.type != T_NONE) {
        if (val.type != t) {
            if (!checkenum (val, t) && (val.type & t & 1) == 0) {		// all integral types are ok
                runtimeError ("Can't assign this value (%s) to %s, it has a different type (%s)", typen.c_str(), varname.c_str(), typestring (val).c_str()) ;
            } 
        } else if (val.type == T_OBJECT) {
            if (val.object != NULL && obj != NULL) {
                Block *varblk = val.object->block ;
                Block *valblk = obj->block ;
                if (!blocksCompatible (varblk, valblk)) {
                    runtimeError ("Can't assign this value (%s) to %s, it has a different type (%s)", typen.c_str(), varname.c_str(), typestring (val).c_str()) ;
                }
            }

        }
  
    }
}

inline void VirtualMachine::checkType (const string &varname, const Value &val, Object *obj) {
    if (val.type != T_NONE) {
        if (val.type != T_OBJECT) {
            runtimeError ("Can't assign this value (%s) to %s, it has a different type (%s)", 
                obj == NULL ? "null" : obj->block->name.c_str(), varname.c_str(), typestring (val).c_str()) ;
        } else {
            if (val.object != NULL && obj != NULL) {
                Block *varblk = val.object->block ;
                Block *valblk = obj->block ;
                if (!blocksCompatible (varblk, valblk)) {
                    runtimeError ("Can't assign this value (%s) to %s, it has a different type (%s)", obj->block->name.c_str(), varname.c_str(), typestring (val).c_str()) ;
                }
            }
        }
    }
}

inline void VirtualMachine::checkType (const string &varname, const Value &val, Type t, const Value &typen, Object *obj) {
    if (val.type != T_NONE) {
        if (val.type != t) {
            if (!checkenum (val, t) && (val.type & t & 1) == 0) {		// all integral types are ok
                runtimeError ("Can't assign this value (%s) to %s, it has a different type (%s)", typestring (typen).c_str(), varname.c_str(), typestring (val).c_str()) ;
            } 
        } else if (val.type == T_OBJECT) {
            if (val.object != NULL && obj != NULL) {
                Block *varblk = val.object->block ;
                Block *valblk = obj->block ;
                if (!blocksCompatible (varblk, valblk)) {
                    runtimeError ("Can't assign this value (%s) to %s, it has a different type (%s)", typestring (typen).c_str(), varname.c_str(), typestring (val).c_str()) ;
                }
            }

        }
  
    }
}

inline void VirtualMachine::checkType (const string &varname, const Value &val, Type t, const char *typen, Object *obj) {
    if (val.type != T_NONE) {
        if (val.type != t) {
            if (!checkenum (val, t) && (val.type & t & 1) == 0) {		// all integral types are ok
                runtimeError ("Can't assign this value (%s) to %s, it has a different type (%s)", typen, varname.c_str(), typestring (val).c_str()) ;
            } 
        } else if (val.type == T_OBJECT) {
            if (val.object != NULL && obj != NULL) {
                Block *varblk = val.object->block ;
                Block *valblk = obj->block ;
                if (!blocksCompatible (varblk, valblk)) {
                    runtimeError ("Can't assign this value (%s) to %s, it has a different type (%s)", typen, varname.c_str(), typestring (val).c_str()) ;
                }
            }

        }
  
    }
}

//
// set a value in a variable, checking types
//

inline void VirtualMachine::setVariable (Variable *var, StackFrame *stk, const Value &v, bool checkconst, bool checktype) {
    if (checkconst && (var->flags & VAR_CONST)) {
        runtimeError ("Can't assign value to %s, it is a constant", var->name.c_str()) ;
    }
    if (checktype && !(var->flags & VAR_GENERIC)) {
        if (var->flags & VAR_TYPEFIXED) {
            Value &oldval = var->getValue (stk) ;
            checkType (var->name, oldval, v.type, v) ;
        } else if (/*v.type != T_NONE && */var->flags & VAR_TYPESET) {
            Value &oldval = var->getValue (stk) ;
            if (!(oldval.type == T_CLOSURE && oldval.closure->vm_state != NULL)) {
                checkType (var->name, oldval, v.type,  v, v.object) ;
            }
        }
    }
    var->setValue (v, stk) ;
}

inline void VirtualMachine::setVariable (Variable *var, StackFrame *stk, Object *obj, bool checkconst, bool checktype) {
    if (checkconst && (var->flags & VAR_CONST)) {
        runtimeError ("Can't assign value to %s, it is a constant", var->name.c_str()) ;
    }
    if (checktype && !(var->flags & VAR_GENERIC)) {
        if (var->flags & VAR_TYPEFIXED) {
            Value &oldval = var->getValue (stk) ;
            checkType (var->name, oldval, obj) ;
        } else if (/*v.type != T_NONE && */var->flags & VAR_TYPESET) {
            Value &oldval = var->getValue (stk) ;
            if (!(oldval.type == T_CLOSURE && oldval.closure->vm_state != NULL)) {
                checkType (var->name, oldval, obj) ;
            }
        }
    }
    var->setValue (obj, stk) ;
}

//
// set a value in a destination.  May be register or variable
//

inline void VirtualMachine::set (Operand *dest, const Value &v, bool checkconst, bool checktype) {
    if (dest == NULL) {
        return ;
    }
    if (dest->type == tREGISTER) {
        regfile[dest->val.integer] = v ;
    } else if (dest->type == tUNKNOWN) {
        dest->val = v ;
    } else {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        int level = (int)dest->val.type - 100 ;
        StackFrame *stk = dest->type == tLOCALVAR ? frame : getStack (level) ;
        setVariable (var, stk, v, checkconst, checktype) ;
    }
}


#if 0
inline void VirtualMachine::set (Operand *dest, Object *obj, bool checkconst, bool checktype) {
    if (dest == NULL) {
        return ;
    }
    if (dest->type == tREGISTER) {
        regfile[dest->val.integer] = obj ;
    } else if (dest->type == tUNKNOWN) {
        dest->val = obj ;
    } else {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        int level = (int)dest->val.type - 100 ;
        StackFrame *stk = dest->type == tLOCALVAR ? frame : getStack (level) ;
        setVariable (var, stk, obj, checkconst, checktype) ;
    }
}
#endif


//
// illegal operation reporting functions
//
void VirtualMachine::illegalop (const Value &v1, const Value &v2, const char *op) {
    runtimeError ("Illegal operation: %s %s %s", typestring (v1).c_str(), op, typestring (v2).c_str()) ;
}

void VirtualMachine::illegalop (const Value &v, const char *op) {
    runtimeError ("Illegal operation: %s %s", op, typestring (v).c_str()) ;
}


//
// is the argument an integral type
//
bool VirtualMachine::isIntegral (const Value &v) {
    return v.type == T_INTEGER || v.type == T_CHAR || v.type == T_ENUMCONST || v.type == T_BYTE ;
}

//
// the argument must be intregal (isIntegral must be true)
//

INTEGER getInt (const Value &v) {
    if (v.type == T_ENUMCONST) {
        return v.ec->value ;
    } else {
        return v.integer ;
    }
}


//
// ************************************************************************************
// operations on values.
//
// Each takes 2 values and possibly other arguments.
//
// ************************************************************************************
//

bool VirtualMachine::cmpeq (const Value &v1, const Value &v2, const char *op) {
    InterpretedBlock *opfunc ;

    // allow comparing a value to none
    if (v2.type == T_NONE) {
        return v1.type == T_NONE;
    }

    switch (v1.type) {
    case T_INTEGER: case T_CHAR: case T_BYTE:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            return v1.integer == v2.integer ;
        
        case T_STRING:
            return v1.toString() == *v2.str ;
   
        case T_REAL:
            return v1.integer == v2.real ;

        case T_ENUMCONST:
            return v1.integer == getInt (v2) ;

        }
        break ;

    case T_REAL:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            return v1.real == v2.integer ;
        
        case T_STRING:
            return v1.toString() == *v2.str ;
   
        case T_REAL:
            return v1.real == v2.real ;

        }
        break ;

    case T_STRING:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_REAL: case T_BYTE:
            return *v1.str == v2.toString() ;
        
        case T_STRING:
            return *v1.str == *v2.str ;
   
        case T_OBJECT:		// allow comparison of string to null
            if (v2.object == NULL) {
                return false ;		// strings compared to null always false
            }
        }
        break ;

    case T_VECTOR:
        if (v2.type == T_VECTOR) {
            return *v1.vec == *v2.vec ;
        } 
        break ;

    case T_BYTEVECTOR:
        if (v2.type == T_BYTEVECTOR) {
            return *v1.bytevec == *v2.bytevec ;
        } 
        break ;


    case T_STREAM:
        if (v2.type == T_STREAM) {
            return v1.stream == v2.stream ;
        }
        break ;

    case T_MAP:
        if (v2.type == T_MAP) {
            return *v1.m == *v2.m ;
        }
        break ;

    case T_ENUMCONST:
        if (v2.type == T_ENUMCONST) {
            return v1.ec->en == v2.ec->en && v1.ec->offset == v2.ec->offset ;
        } else if (v2.type == T_INTEGER || v2.type == T_CHAR || v2.type == T_BYTE) {
            return getInt (v1) == v2.integer ;
        }
        break ;

    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD:
    case T_ENUM:
        switch (v2.type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_FUNCTION:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
            return v1.block == v2.block ;
        case T_CLOSURE:
            return v1.block == v2.closure->block ;
        default:
            return false ;		// need to allow typeof (x) == "y" where x is a class
        }
        break ;
        
    case T_CLOSURE:
        switch (v2.type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_FUNCTION:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
            return v1.closure->block == v2.block ;
        case T_CLOSURE:
            return v1.closure->block == v2.closure->block ;
        default:
            return false ;
        }
        break ;

    case T_OBJECT:
        if (v1.object != NULL && (opfunc = checkForOperator (v1, EQUAL)) != NULL) {
            if (v2.type == T_OBJECT) {
                if (v2.object != NULL) {
                    Operand res ; ;
                    callFunction (&res, opfunc, v1, v2) ;
                    return static_cast<bool>(res.val) ;
                }
            } else {
                Operand res ; ;
                callFunction (&res, opfunc, v1, v2) ;
                return static_cast<bool>(res.val) ;
            }
        }
        if (v2.type == T_STRING) {			// allow null == string
           if (v1.object == NULL) {
               return false ;				// null == string always false
           }
        }
        if (v2.type == T_OBJECT) {
            return v1.object == v2.object ;
        }
        break ;

    case T_NONE:
        return v2.type == T_NONE ;
                
    }

    // if we don't have a match and the right type is a class (etc) then we need to return false
    // this is to allow y == typeof (x) where x is a class

    switch (v2.type) {
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD:
    case T_ENUM:
    case T_CLOSURE:
        return false ;
    }

    illegalop (v1, v2, op) ;
}

bool VirtualMachine::cmpne (const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;
    if (v1.type == T_OBJECT && v1.object != NULL && (opfunc = checkForOperator (v1, NOTEQ)) != NULL) {
        if (v2.type == T_OBJECT && v2.object != NULL) {
            Operand res ;
            callFunction (&res, opfunc, v1, v2) ;
            return static_cast<bool>(res.val) ;
        }
    }
    return !cmpeq (v1, v2, "!=") ;
}


bool VirtualMachine::cmplt (const Value &v1, const Value &v2, const char *op) {
    InterpretedBlock *opfunc ;

    switch (v1.type) {
    case T_INTEGER: case T_BYTE:
    case T_CHAR:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            return v1.integer < v2.integer ;
        
        case T_STRING:
            return v1.toString() < *v2.str ;
   
        case T_REAL:
            return (double)v1.integer < v2.real ;

        case T_ENUMCONST:
            return v1.integer < getInt (v2) ;

        case T_VECTOR:
            break ;
        }
        break ;
    case T_REAL:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            return v1.real < v2.integer ;
        
        case T_STRING:
            return v1.toString() < *v2.str ;
   
        case T_REAL:
            return v1.real < v2.real ;

        case T_VECTOR:
            break ;
        }
        break ;
    case T_STRING:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_REAL: case T_BYTE:
            return *v1.str < v2.toString() ;
        
        case T_STRING:
            return *v1.str < *v2.str ;

        case T_VECTOR:
            break ;
        }
        break ;
        
    case T_VECTOR:
        if (v2.type == T_VECTOR) {
            return *v1.vec < *v2.vec ;
        } 
        break ;

    case T_BYTEVECTOR:
        if (v2.type == T_BYTEVECTOR) {
            return *v1.bytevec < *v2.bytevec ;
        } 
        break ;

    case T_MAP:
        if (v2.type == T_MAP) {
            return *v1.m < *v2.m ;
        }
        break ;

    case T_ENUMCONST:
        if (v2.type == T_ENUMCONST) {
            return v1.ec->en == v2.ec->en && v1.ec->offset < v2.ec->offset ;
        } else if (v2.type == T_INTEGER || v2.type == T_CHAR || v2.type == T_BYTE) {
            return getInt (v1) < v2.integer ;
        }
        break ;

    case T_OBJECT:
        if (v1.object != NULL && (opfunc = checkForOperator (v1, LESS)) != NULL) {
            Operand res ;
            callFunction (&res, opfunc, v1, v2) ;
            return static_cast<bool>(res.val) ;
        }
        break ;

    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD:
    case T_ENUM:
        switch (v2.type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_FUNCTION:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
            return v1.block->isSuperBlock (v2.block) ;		// v2 is superblock of v1
        case T_CLOSURE:
            return v1.block->isSuperBlock (v2.closure->block) ;		// v2 is superblock of v1
        }
        break ;
    case T_CLOSURE:
        switch (v2.type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_FUNCTION:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
            return v1.closure->block->isSuperBlock (v2.block) ;		// v2 is superblock of v1
        case T_CLOSURE:
            return v1.closure->block->isSuperBlock (v2.closure->block) ;		// v2 is superblock of v1
        }
        break ;
        

    }
    illegalop (v1, v2, op) ;
}

bool VirtualMachine::cmple (const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;
    if (v1.type == T_OBJECT && v1.object != NULL && (opfunc = checkForOperator (v1, LESSEQ)) != NULL) {
        Operand res ;
        callFunction (&res, opfunc, v1, v2) ;
        return static_cast<bool>(res.val) ;
    }
    switch (v1.type) {
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD:
    case T_ENUM:
        switch (v2.type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_FUNCTION:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
            return v1.block == v2.block || v1.block->isSuperBlock (v2.block) ;		// v2 is superblock of v1
        case T_CLOSURE:
            return v1.block == v2.closure->block || v1.block->isSuperBlock (v2.closure->block) ;		// v2 is superblock of v1
        }
        break ;
    case T_CLOSURE:
        switch (v2.type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_FUNCTION:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
            return v1.closure->block == v2.block || v1.closure->block->isSuperBlock (v2.block) ;		// v2 is superblock of v1
        case T_CLOSURE:
            return v1.closure->block == v2.closure->block || v1.closure->block->isSuperBlock (v2.closure->block) ;		// v2 is superblock of v1
        }
        break ;
    }
    return !cmpgt (v1, v2, ">") ;
}


bool VirtualMachine::cmpgt (const Value &v1, const Value &v2, const char *op) {
    InterpretedBlock *opfunc ;

    switch (v1.type) {
    case T_INTEGER: case T_BYTE:
    case T_CHAR:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            return v1.integer > v2.integer ;
        
        case T_STRING:
            return v1.toString() > *v2.str ;
   
        case T_REAL:
            return (double)v1.integer > v2.real ;

        case T_ENUMCONST:
            return v1.integer < getInt (v2) ;

        case T_VECTOR:
            break ;
        }
        break ;
    case T_REAL:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            return v1.real > v2.integer ;
        
        case T_STRING:
            return v1.toString() > *v2.str ;
   
        case T_REAL:
            return v1.real > v2.real ;

        case T_VECTOR:
            break ;
        }
        break ;
    case T_STRING:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_REAL: case T_BYTE:
            return *v1.str > v2.toString() ;
        
        case T_STRING:
            return *v1.str > *v2.str ;
   
        case T_VECTOR:
            break ;
        }
        break ;
        
    case T_VECTOR:
        if (v2.type == T_VECTOR) {
            return *v1.vec > *v2.vec ;
        } 
        break ;

    case T_BYTEVECTOR:
        if (v2.type == T_BYTEVECTOR) {
            return *v1.bytevec > *v2.bytevec ;
        } 
        break ;

    case T_MAP:
        if (v2.type == T_MAP) {
            return *v1.m > *v2.m ;
        }
        break ;

    case T_ENUMCONST:
        if (v2.type == T_ENUMCONST) {
            return v1.ec->en == v2.ec->en && v1.ec->offset > v2.ec->offset ;
        } else if (v2.type == T_INTEGER || v2.type == T_CHAR || v2.type == T_BYTE) {
            return getInt (v1) > v2.integer ;
        }
        break ;

    case T_OBJECT:
        if (v1.object != NULL && (opfunc = checkForOperator (v1, GREATER)) != NULL) {
            Operand res ;
            callFunction (&res, opfunc, v1, v2) ;
            return static_cast<bool>(res.val) ;
        }
        break ;
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD:
    case T_ENUM:
        switch (v2.type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_FUNCTION:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
            return v2.block->isSuperBlock (v1.block) ;		// v1 is superblock of v2
        case T_CLOSURE:
            return v2.closure->block->isSuperBlock (v1.block) ;		// v1 is superblock of v2
        }
        break ;
    case T_CLOSURE:
        switch (v2.type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_FUNCTION:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
            return v2.block->isSuperBlock (v1.closure->block) ;		// v1 is superblock of v2
        case T_CLOSURE:
            return v2.closure->block->isSuperBlock (v1.closure->block) ;		// v1 is superblock of v2
        }
        break ;
        

    }
    illegalop (v1, v2, op) ;
}

bool VirtualMachine::cmpge (const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;
    if (v1.type == T_OBJECT && v1.object != NULL && (opfunc = checkForOperator (v1, GREATEREQ)) != NULL) {
        Operand res ;
        callFunction (&res, opfunc, v1, v2) ;
        return static_cast<bool>(res.val) ;
    }
    switch (v1.type) {
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD:
    case T_ENUM:
        switch (v2.type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_FUNCTION:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
            return v1.block == v2.block || v2.block->isSuperBlock (v1.block) ;		// v1 is superblock of v2
        case T_CLOSURE:
            return v1.block == v2.closure->block || v2.closure->block->isSuperBlock (v1.block) ;		// v1 is superblock of v2
        }
        break ;
    case T_CLOSURE:
        switch (v2.type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_FUNCTION:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
            return v1.closure->block == v2.block || v2.block->isSuperBlock (v1.closure->block) ;		// v1 is superblock of v2
        case T_CLOSURE:
            return v1.closure->block == v2.closure->block || v2.closure->block->isSuperBlock (v1.closure->block) ;		// v1 is superblock of v2
        }
        break ;
    }
    return !cmplt (v1, v2, ">=") ;
}

//
// given a level number, traverse up the static chain and return
// the found stackframe
//

inline StackFrame *VirtualMachine::getStack (int level) {
    if (level == 0) {
        return frame ;
    }
    if (level == 1) {
        return staticLink->frame ;
    }
    StaticLink *slink = staticLink ;
    while (slink != NULL && --level > 0) {
        slink = slink->next ;
    }
    if (slink == NULL) {
        std::cerr << "Static link sequence too short - aborting\n" ;
        ::abort() ;
        //throw "Static link sequence too short" ;
    }
    return slink->frame ;
}


void VirtualMachine::add (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;
    switch (v1.type) {
    case T_INTEGER: case T_CHAR: case T_BYTE:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            set (r, v1.integer + v2.integer) ;
            return ;
        
        case T_STRING:
            set (r,  v1.toString() + *v2.str) ;
            return ;
   
        case T_VECTOR: {
            Value::vector *vv = new Value::vector (v2.vec->size() + 1) ;
            vv->clear() ;
            *vv = *v2.vec ;
            vv->ref = 0 ;		// no references yet
            vv->push_front (v1) ;
            set (r, vv) ;
            }
            return ;

        case T_BYTEVECTOR: {
            Value::bytevector *vv = new Value::bytevector (v2.bytevec->size() + 1) ;
            vv->clear() ;
            *vv = *v2.bytevec ;
            vv->ref = 0 ;		// no references yet
            vv->push_front (v1.integer) ;
            set (r, vv) ;
            }
            return ;
            
        case T_ENUMCONST: {
            EnumConst *e = v2.ec->nextValue (v1.integer) ;
            if (e == NULL) {
                runtimeError ("Beyond range of enum") ;
            } else {
                set (r, e) ;
            }
            return ;
            }

        case T_REAL:
            set (r, v1.integer + v2.real) ;
            return ;

        }
        break ;
    case T_REAL:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            set (r, (v1.real + v2.integer)) ;
            return ;
        
        case T_STRING:
            set (r, (v1.toString() + *v2.str)) ;
            return ;
   
        case T_VECTOR: {
            Value::vector *vv = new Value::vector (v2.vec->size() + 1) ;
            vv->clear() ;
            *vv = *v2.vec ;
            vv->ref = 0 ;		// no references yet
            vv->push_front (v1) ;
            set (r, (vv)) ;
            }
            return ;
            
        case T_BYTEVECTOR: {
            Value::bytevector *vv = new Value::bytevector (v2.bytevec->size() + 1) ;
            vv->clear() ;
            *vv = *v2.bytevec ;
            vv->ref = 0 ;		// no references yet
            vv->push_front ((INTEGER)v1.real) ;
            set (r, vv) ;
            }
            return ;

        case T_REAL:
            set (r, (v1.real + v2.real)) ;
            return ;

        }
        break ;
    case T_STRING:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_REAL: case T_BYTE:
            set (r, (*v1.str + v2.toString())) ;
            return ;
        
        case T_STRING:
            set (r, (*v1.str + *v2.str)) ;
            return ;
   
        case T_VECTOR: {
            Value::vector *vv = new Value::vector (v2.vec->size() + 1) ;
            vv->clear() ;
            *vv = *v2.vec ;
            vv->ref = 0 ;		// no references yet
            vv->push_front (v1) ;
            set (r, (vv)) ;
            }
            return ;

        case T_ENUMCONST:
           set (r, (*v1.str + v2.ec->name)) ;
            return ;

        case T_OBJECT:
            if (v2.object == NULL) {
                set (r, *v1.str + "null") ;
            } else {
                Scope *scope ;
                Variable *var = v2.object->block->findVariable (string("toString"), scope, VAR_PUBLIC, ir->source, NULL) ;
                if (var == NULL) {
                    break ;
                } else {
                    Value &v = var->getValue(v2.object) ;
                    if (v.type != T_FUNCTION) {
                        break ;
                    } else {
                        Operand res ;
                        callFunction (&res, v.block, v2.object) ;
                        if (res.val.type != T_STRING) {
                            runtimeError ("Illegal return value type from toString") ;
                        }
                        set (r, *v1.str + *res.val.str) ;
                    }
                }
            }
            return ;

        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_PACKAGE:
        case T_THREAD:
        case T_FUNCTION:
        case T_ENUM:
            set (r, (*v1.str + v2.block->name)) ;
            return ;
        case T_CLOSURE:
            set (r, (*v1.str + v2.closure->block->name)) ;
            return ;
        case T_NONE:
            set (r, *v1.str + "none") ;
            return ;
        }
        break ;

    case T_VECTOR:{
        if (v2.type == T_VECTOR) {
            Value::vector *vv = new Value::vector (v1.vec->size() + v2.vec->size()) ;
            vv->clear() ;
            *vv = *v1.vec + *v2.vec ;
            vv->ref = 0 ;		// no references yet
            set (r, vv) ;
        } else {
            Value::vector *vv = new Value::vector (v1.vec->size() + 1) ;
            vv->clear() ;
            *vv = *v1.vec ;
            vv->ref = 0 ;		// no references yet
            vv->push_back (v2) ;
            set (r, vv) ;
        }
        return ;
        }

    case T_BYTEVECTOR:{
        if (v2.type == T_BYTEVECTOR) {
            Value::bytevector *vv = new Value::bytevector (v1.bytevec->size() + v2.bytevec->size()) ;
            vv->clear() ;
            *vv = *v1.bytevec + *v2.bytevec ;
            set (r, vv) ;
        } else {
            if (isIntegral (v2)) {
                Value::bytevector *vv = new Value::bytevector (v1.bytevec->size() + 1) ;
                vv->clear() ;
                *vv = *v1.bytevec ;
                vv->push_back (getInt (v2)) ;
                set (r, vv) ;
            }
        }
        return ;
        }
    
    case T_MAP: {
        if (v2.type == T_MAP) {
            map *mm = new map ;
            *mm = *v1.m + *v2.m ;
            mm->ref = 0 ;		// no references
            set (r, (mm)) ;
            return ;
        }
        break ;
        }
       
    case T_ENUMCONST:
        switch (v2.type) {
        case T_INTEGER: {
            EnumConst *e = v1.ec->nextValue (v2.integer) ;
            if (e == NULL) {
                runtimeError ("Beyond range of enum") ;
            } else {
                set (r, (e)) ;
            }
            return ;
            }
        case T_STRING:
            set (r, (v1.ec->name + *v2.str)) ;
            return ;

        case T_VECTOR: {
            Value::vector *vv = new Value::vector (v2.vec->size() + 1) ;
            vv->clear() ;
            *vv = *v2.vec ;
            vv->ref = 0 ;		// no references yet
            vv->push_front (v1) ;
            set (r, (vv)) ;
            }
            return ;

        case T_BYTEVECTOR: {
            Value::bytevector *vv = new Value::bytevector (v2.bytevec->size() + 1) ;
            vv->clear() ;
            *vv = *v2.bytevec ;
            vv->ref = 0 ;		// no references yet
            vv->push_front (getInt (v1)) ;
            set (r, vv) ;
            }
            return ;
        }
        break ;

    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_PACKAGE:
    case T_THREAD:
    case T_FUNCTION:
    case T_ENUM:
        switch (v2.type) {
        case T_STRING:
            set (r, (v1.block->name + *v2.str)) ;
            return ;
        case T_VECTOR: {
            Value::vector *vv = new Value::vector (v2.vec->size() + 1) ;
            vv->clear() ;
            *vv = *v2.vec ;
            vv->ref = 0 ;		// no references yet
            vv->push_front (v1) ;
            set (r, (vv)) ;
            }
            return ;
        }
        break ;
    case T_CLOSURE:
        switch (v2.type) {
        case T_STRING:
            set (r, (v1.closure->block->name + *v2.str)) ;
            return ;
        case T_VECTOR: {
            Value::vector *vv = new Value::vector (v2.vec->size() + 1) ;
            vv->clear() ;
            *vv = *v2.vec ;
            vv->ref = 0 ;		// no references yet
            vv->push_front (v1) ;
            set (r, (vv)) ;
            }
            return ;
        }
        break ;

    case T_OBJECT:
        if (v1.object != NULL && (opfunc = checkForOperator (v1, PLUS)) != NULL) {
            callFunction (r, opfunc, v1, v2) ;
            return ;
        }
        switch (v2.type) {
        case T_VECTOR: {
            Value::vector *vv = new Value::vector (v2.vec->size() + 1) ;
            vv->clear() ;
            *vv = *v2.vec ;
            vv->ref = 0 ;		// no references yet
            vv->push_front (v1) ;
            set (r, (vv)) ;
            }
            return ;
        }
        break ;
    case T_MEMORY: {
        if (!isIntegral (v2)) {
            illegalop (v1, v2, "+") ;
        }
        int offset = getInt (v2) ;
        if (offset < 0 || offset >= v1.mem->size) {
            runtimeError ("Pointer would be outside raw memory") ;
        }
        RawPointer *p = new RawPointer (v1.mem, offset) ;
        set (r, p) ;
        return ;
        }

    case T_POINTER:
        if (!isIntegral (v2)) {
            illegalop (v1, v2, "+") ;
        }
        int offset = v1.pointer->offset + getInt (v2) ;
        if (offset < 0 || offset >= v1.pointer->mem->size) {
            runtimeError ("Pointer would be outside raw memory") ;
        }
 
        RawPointer *p = new RawPointer (v1.pointer->mem, offset) ;
        set (r, p) ;
        return ;
    }
    illegalop (v1, v2, "+") ;
}


void VirtualMachine::sub (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;
    switch (v1.type) {
    case T_INTEGER: case T_CHAR: case T_BYTE:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            set (r, (v1.integer - v2.integer)) ;
            return ;
        
        case T_ENUMCONST: {
            EnumConst *e = v2.ec->prevValue (v1.integer) ;
            if (e == NULL) {
                runtimeError ("Beyond range of enum") ;
            } else {
                set (r, (e)) ;
            }
            return ;
            }

        case T_REAL:
            set (r, (v1.integer - v2.real)) ;
            return ;

        }
        break ;

    case T_REAL:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            set (r, (v1.real - v2.integer)) ;
            return ;
        
        case T_REAL:
            set (r, (v1.real - v2.real)) ;
            return ;

        }
        break ;

    case T_ENUMCONST:
        if (v2.type == T_INTEGER) {
            EnumConst *e = v1.ec->prevValue (v2.integer) ;
            if (e == NULL) {
                runtimeError ("Beyond range of enum") ;
            } else {
                set (r, (e)) ;
            }
            return ;
        }
        break ;

    case T_VECTOR:
        if (v2.type == T_VECTOR) {
            Value::vector *rs = new Value::vector (v1.vec->size()) ;
            rs->clear() ;
            *rs = *v1.vec - *v2.vec ;
            rs->ref = 0 ;		// no references yet
            set (r, (rs)) ;
            return ;
        }
        break ;

    case T_BYTEVECTOR:
        if (v2.type == T_BYTEVECTOR) {
            Value::bytevector *rs = new Value::bytevector (v1.bytevec->size()) ;
            rs->clear() ;
            *rs = *v1.bytevec - *v2.bytevec ;
            rs->ref = 0 ;		// no references yet
            set (r, rs) ;
            return ;
        }
        break ;

    case T_OBJECT:
        if (v1.object != NULL && (opfunc = checkForOperator (v1, MINUS)) != NULL) {
            callFunction (r, opfunc, v1, v2) ;
            return ;
        }

    case T_MEMORY: {
        if (!isIntegral (v2)) {
            illegalop (v1, v2, "-") ;
        }
        int offset = getInt (v2) ;
        if (offset < 0 || offset >= v1.mem->size) {
            runtimeError ("Pointer would be outside raw memory") ;
        }
        RawPointer *p = new RawPointer (v1.mem, offset) ;
        set (r, p) ;
        return ;
        }

    case T_POINTER:
        if (!isIntegral (v2)) {
            illegalop (v1, v2, "-") ;
        }
        int offset = v1.pointer->offset + getInt (v2) ;
        if (offset < 0 || offset >= v1.pointer->mem->size) {
            runtimeError ("Pointer would be outside raw memory") ;
        }
 
        RawPointer *p = new RawPointer (v1.pointer->mem, offset) ;
        set (r, p) ;
        return ;
    }
    illegalop (v1, v2, "-") ;
}



void VirtualMachine::mul (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;

    switch (v1.type) {
    case T_INTEGER: case T_CHAR: case T_BYTE:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            set (r, (v1.integer * v2.integer)) ;
            return ;
        case T_REAL:
            set (r, (v1.integer * v2.real)) ;
            return ;
        }
        break ;
    case T_REAL:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            set (r, (v1.real * v2.integer)) ;
            return ;
        case T_REAL:
            set (r, (v1.real * v2.real)) ;
            return ;
        }
        break ;

    case T_OBJECT:
        if (v1.object != NULL && (opfunc = checkForOperator (v1, STAR)) != NULL) {
            callFunction (r, opfunc, v1, v2) ;
            return ;
        }
    } 
    illegalop (v1, v2, "*") ;
}

inline void VirtualMachine::checkZero (INTEGER v) {
    if (v == 0) {
        runtimeError ("Division by zero") ;
    }
}


inline void VirtualMachine::checkZero (double v) {
    if (v == 0) {
        runtimeError ("Division by zero") ;
    }
}


void VirtualMachine::div (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;

    switch (v1.type) {
    case T_INTEGER: case T_CHAR: case T_BYTE:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            checkZero (v2.integer) ;
            set (r, (v1.integer / v2.integer)) ;
            return ;
        case T_REAL:
            checkZero (v2.real) ;
            set (r, (v1.integer / v2.real)) ;
            return ;
        }
        break ;
    case T_REAL:
        switch (v2.type) {
        case T_INTEGER: case T_CHAR: case T_BYTE:
            checkZero (v2.integer) ;
            set (r, (v1.real / v2.integer)) ;
            return ;
        case T_REAL:
            checkZero (v2.real) ;
            set (r, (v1.real / v2.real)) ;
            return ;
        }
        break ;

    case T_OBJECT:
        if (v1.object != NULL && (opfunc = checkForOperator (v1, SLASH)) != NULL) {
            callFunction (r, opfunc, v1, v2) ;
            return ;
        }
    } 
    illegalop (v1, v2, "/") ;
}

void VirtualMachine::mod (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;

    if (v1.type == T_OBJECT && v1.object != NULL && (opfunc = checkForOperator (v1, PERCENT)) != NULL) {
        callFunction (r, opfunc, v1, v2) ;
        return ;
    }
    if (isIntegral (v1) && isIntegral (v2)) {
        INTEGER v2val = getInt (v2) ;
        checkZero (v2val) ;
        set (r, (getInt (v1) % v2val)) ;
    } else {
        if (v1.type == T_REAL) {
            if (isIntegral (v2)) {
                set (r, fmod (v1.real, (double)v2.integer)) ;
            } else if (v2.type == T_REAL) {
                set (r, fmod (v1.real, v2.real)) ;
            } else {
                illegalop (v1, v2, "%") ;
            }
        } else {
            illegalop (v1, v2, "%") ;
        }
    }
}


void VirtualMachine::sra (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;

    switch (v1.type) {
    case T_INTEGER: case T_CHAR: case T_BYTE:
        set (r, (v1.integer >> v2.integer)) ;
        return ;
    case T_STRING: {
        string *copy = new string (*v1.str) ;
        int s = 0, e = 0 ;
        int n = v2.integer ;
        e = n - 1 ;
        if (n >= copy->size()) {
            copy->str.clear() ;
            set (r, copy) ;
        } else {
            s = copy->size() - n ;
            set (r, (copy->erase (s,s+e))) ;
            delete copy ;
        }
        return ;
        }
    case T_VECTOR: {
        Value::vector *copy = new Value::vector (v1.vec->size()) ;
        copy->clear() ;
        *copy = *v1.vec ;
        copy->ref = 0 ;		// no references yet
        ValueVec::iterator s, e ;
        e = copy->vec.end() ;
        int n = v2.integer ;
        if (n >= copy->size()) {
            copy->clear() ;
            set (r, copy) ;
        } else {
            s = copy->vec.end() - n ;
            set (r, (copy->erase (s, e))) ;
        }
        return ;
        }
    case T_BYTEVECTOR: {
        Value::bytevector *copy = new Value::bytevector (v1.bytevec->size()) ;
        copy->clear() ;
        *copy = *v1.bytevec ;
        copy->ref = 0 ;		// no references yet
        ByteVec::iterator s, e ;
        e = copy->vec.end() ;
        int n = v2.integer ;
        if (n >= copy->size()) {
            copy->clear() ;
            set (r, copy) ;
        } else {
            s = copy->vec.end() - n ;
            set (r, (copy->erase (s, e))) ;
        }
        return ;
        }

    case T_OBJECT:
        if (v1.object != NULL && (opfunc = checkForOperator (v1, RSHIFT)) != NULL) {
            callFunction (r, opfunc, v1, v2) ;
            return ;
        }
    default:
        illegalop (v1, v2, ">>") ;
    }
}

void VirtualMachine::srl (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;

    switch (v1.type) {
    case T_INTEGER: case T_CHAR: case T_BYTE:
        set (r, ((UINTEGER)v1.integer >> v2.integer)) ;
        return ;
    case T_STRING: {
        string *copy = new string (*v1.str) ;
        int s = 0, e = 0 ;
        int n = v2.integer ;
        e = n - 1 ;
        if (n >= copy->size()) {
            copy->str.clear() ;
            set (r, copy) ;
        } else {
            s = copy->size() - n ;
            set (r, (copy->erase (s,s+e))) ;
            delete copy ;
        }
        return ;
        }
    case T_VECTOR: {
        Value::vector *copy = new Value::vector (v1.vec->size()) ;
        copy->clear() ;
        *copy = *v1.vec ;
        copy->ref = 0 ;		// no references yet
        ValueVec::iterator s, e ;
        e = copy->vec.end() ;
        int n = v2.integer ;
        if (n >= copy->size()) {
            copy->clear() ;
            set (r, copy) ;
        } else {
            s = copy->vec.end() - n ;
            set (r, (copy->erase (s, e))) ;
        }
        return ;
        }
    case T_BYTEVECTOR: {
        Value::bytevector *copy = new Value::bytevector (v1.bytevec->size()) ;
        copy->clear() ;
        *copy = *v1.bytevec ;
        copy->ref = 0 ;		// no references yet
        ByteVec::iterator s, e ;
        e = copy->vec.end() ;
        int n = v2.integer ;
        if (n >= copy->size()) {
            copy->clear() ;
            set (r, copy) ;
        } else {
            s = copy->vec.end() - n ;
            set (r, (copy->erase (s, e))) ;
        }
        return ;
        }

    case T_OBJECT:
        if (v1.object != NULL && (opfunc = checkForOperator (v1, ZRSHIFT)) != NULL) {
            callFunction (r, opfunc, v1, v2) ;
            return ;
        }
    default:
        illegalop (v1, v2, ">>>") ;
    }
}

void VirtualMachine::sll (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;

    switch (v1.type) {
    case T_INTEGER: case T_CHAR: case T_BYTE:
        set (r, (v1.integer << v2.integer)) ;
        return ;
    case T_STRING: {
        string *copy = new string (*v1.str) ;
        int s = 0, e = 0 ;
        int n = v2.integer ;
        if (n >= copy->size()) {
            e = copy->size() ;
        } else {
            e = s + n ;
        }
        set (r, (copy->erase (s, e))) ;
        delete copy ;
        return ;
        }
    case T_VECTOR: {
        Value::vector *copy = new Value::vector (v1.vec->size()) ;
        copy->clear() ;
        *copy = *v1.vec ;
        copy->ref = 0 ;		// no references yet
        ValueVec::iterator s, e ;
        s = copy->vec.begin() ;
        int n = v2.integer ;
        if (n >= copy->size()) {
            e = copy->vec.end() ;
        } else {
            e = s + n ;
        }
        set (r, (copy->erase (s, e))) ;
        return ;
        }
    case T_BYTEVECTOR: {
        Value::bytevector *copy = new Value::bytevector (v1.bytevec->size()) ;
        copy->clear() ;
        *copy = *v1.bytevec ;
        copy->ref = 0 ;		// no references yet
        ByteVec::iterator s, e ;
        s = copy->vec.begin() ;
        int n = v2.integer ;
        if (n >= copy->size()) {
            e = copy->vec.end() ;
        } else {
            e = s + n ;
        }
        set (r, (copy->erase (s, e))) ;
        return ;
        }

    case T_OBJECT:
        if (v1.object != NULL && (opfunc = checkForOperator (v1, LSHIFT)) != NULL) {
            callFunction (r, opfunc, v1, v2) ;
            return ;
        }

    default:
        illegalop (v1, v2, "<<") ;
    }
}

void VirtualMachine::bitwiseand (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;

    if (v1.type == T_OBJECT && v1.object != NULL && (opfunc = checkForOperator (v1, AMPERSAND)) != NULL) {
        callFunction (r, opfunc, v1, v2) ;
        return ;
    }
    if (v1.type == T_VECTOR && v2.type == T_VECTOR) {
        Value::vector *rs = new Value::vector (1) ;
        rs->clear() ;
        *rs = *v1.vec & *v2.vec ;
        rs->ref = 0 ;		// no references yet
        set (r, (rs)) ;
    } else if (v1.type == T_BYTEVECTOR && v2.type == T_BYTEVECTOR) {
        Value::bytevector *rs = new Value::bytevector (1) ;
        rs->clear() ;
        *rs = *v1.bytevec & *v2.bytevec ;
        rs->ref = 0 ;		// no references yet
        set (r, (rs)) ;
    } else if (isIntegral (v1) && isIntegral (v2)) {
        set (r, (getInt(v1) & getInt (v2))) ;
    } else {
        illegalop (v1, v2, "&") ;
    }
}

void VirtualMachine::bitwiseor (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;

    if (v1.type == T_OBJECT && v1.object != NULL && (opfunc = checkForOperator (v1, BITOR)) != NULL) {
        callFunction (r, opfunc, v1, v2) ;
        return ;
    }
    if (v1.type == T_VECTOR && v2.type == T_VECTOR) {
        Value::vector *rs = new Value::vector (1) ;
        rs->clear() ;
        *rs = *v1.vec | *v2.vec ;
        rs->ref = 0 ;		// no references yet
        set (r, (rs)) ;
    } else if (v1.type == T_BYTEVECTOR && v2.type == T_BYTEVECTOR) {
        Value::bytevector *rs = new Value::bytevector (1) ;
        rs->clear() ;
        *rs = *v1.bytevec | *v2.bytevec ;
        rs->ref = 0 ;		// no references yet
        set (r, (rs)) ;
    } else if (isIntegral (v1) && isIntegral (v2)) {
        set (r, (getInt(v1) | getInt (v2))) ;
    } else {
        illegalop (v1, v2, "|") ;
    }
}

void VirtualMachine::bitwisexor (Operand *r, const Value &v1, const Value &v2) {
    InterpretedBlock *opfunc ;

    if (v1.type == T_OBJECT && v1.object != NULL && (opfunc = checkForOperator (v1, CARET)) != NULL) {
        callFunction (r, opfunc, v1, v2) ;
        return ;
    } else if (isIntegral (v1) && isIntegral (v2)) {
        set (r, (getInt(v1) ^ getInt (v2))) ;
    } else {
        illegalop (v1, v2, "^") ;
    }
}

void VirtualMachine::uminus (Operand *r, const Value &v1) {
    InterpretedBlock *opfunc ;

    if (v1.type == T_OBJECT && v1.object != NULL && (opfunc = checkForOperator (v1, MINUS)) != NULL) {
        callFunction (r, opfunc, v1) ;
        return ;
    }
    if (v1.type == T_REAL) {
        set (r, (-v1.real)) ;
    } else if (isIntegral (v1)) {
        set (r, (-getInt (v1))) ;
    } else {
        illegalop (v1, "-") ;
    }
}

void VirtualMachine::boolnot (Operand *r, const Value &v1) {
    InterpretedBlock *opfunc ;

    if (v1.type == T_OBJECT && v1.object != NULL && (opfunc = checkForOperator (v1, BANG)) != NULL) {
        callFunction (r, opfunc, v1) ;
        return ;
    }
    if (v1.type == T_REAL) {
        set (r, (!v1.real)) ;
    } else if (isIntegral (v1)) {
        set (r, (!getInt (v1))) ;
    } else {
        illegalop (v1, "!") ;
    }
}

void VirtualMachine::comp (Operand *r, const Value &v1) {
    InterpretedBlock *opfunc ;

    if (v1.type == T_OBJECT && v1.object != NULL && (opfunc = checkForOperator (v1, TILDE)) != NULL) {
        callFunction (r, opfunc, v1) ;
        return ;
    } else if (isIntegral (v1)) {
        set (r, (~getInt (v1))) ;
    } else {
        illegalop (v1, "~") ;
    }
}


//
// the stack is exhausted, grow it
//
void VirtualMachine::growStack() {
    int newsize = currstacksize * 2 ;
    Value *newstack = new Value[newsize] ;
    for (int i = 0 ; i < currstacksize ; i++) {
        newstack[i] = stack[i] ;
    }
    delete [] stack ;
    currstacksize = newsize ;
    stack = newstack ;
}


//
// constructor for a virtual machine
//

VirtualMachine::VirtualMachine (Aikido *a) : aikido (a) {
    parserLock.acquire(true) ;
    // initalize runtime variables
    registers = new Value [MAXREGS] ;
    currstacksize = MAXSTACK ;
    stack = new Value [MAXSTACK] ;
    scopeSP = 0 ;
    sp = 0 ;
    regs = 0 ;
    frame = NULL ;
    staticLink = NULL ;
    pc = 0 ;
    ir = NULL ;
    currentCode = NULL ;
    debugState = RUN ;
    regfile = &registers[regs] ;
    lastSystemResult = 0 ;
    output = a->mystdout ;      // default the output to the main stream
    input = a->mystdin ;      // default the input to the main stream

    // set the system variables (first time only)
    if (system == NULL) {
        Tag *sys = aikido->findTag (string (".System")) ;
        if (sys == NULL) {
            throw "Can't find system package" ;
        }
        Scope *scope ;
        system = (Package *)sys->block ;
        Tag *pairtag = (Tag *)system->findVariable (string (".Pair"), scope, VAR_PUBLIC, NULL, NULL) ;
        if (pairtag == NULL) {
            parserLock.release(true) ;
            throw "Can't find System.Pair" ;
        }
        pairClass = (Class *)pairtag->block ;
        Tag *regextag = (Tag *)system->findVariable (string (".Regex"), scope, VAR_PUBLIC, NULL, NULL) ;
         if (regextag == NULL) {
             parserLock.release(true) ;
             throw "Can't find System.Regex" ;
         }
         regexClass = (Class *)regextag->block ;

         Tag *facttag = (Tag *)system->findVariable (string (".newObject"), scope, VAR_PUBLIC, NULL, NULL) ;
         if (facttag == NULL) {
             throw "Can't find System.newObject" ;
         }
         objectFactory = (Function *)facttag->block ;


    }
    if (aikido->properties & DEBUG) {
        aikido->debugger->registerThread (this) ;
    }
    initBuiltinMethods() ;
    parserLock.release(true) ;
}

VirtualMachine::~VirtualMachine() {
    if (aikido->properties & DEBUG) {
        aikido->debugger->unregisterThread (this) ;
    }
    delete [] registers ;
    delete [] stack ;

    for (RegExMap::iterator r = regexMap.begin() ; r != regexMap.end() ; r++) {
        OS::regexFree (r->second) ;
        delete r->second ;
    }
}

//
// show the position of this VM on the stream
//

void VirtualMachine::showPosition (std::ostream &os) {
    os << frame->block->name << "(" << ir->source->filename << ":" << ir->source->lineno << ")" ;
}

//
// read the current position variables for the debugger
//
void VirtualMachine::getPosition (Scope *&scope, int &level, StackFrame *&stack, StaticLink *&slink, Instruction *&inst) {
    scope = scopeStack[scopeSP-1] ;
    level - scopeStack[scopeSP-1]->majorScope->level ;
    stack = frame ;
    slink - staticLink ;
    inst = ir ;
}

//
// get the address of an operand
//

inline Value *VirtualMachine::getaddr (Operand *op) {
    if (op->type == tLOCALVAR) {
        return static_cast<Variable*>(op->val.ptr)->getAddress(frame) ;
    } else if (op->type == tREGISTER) {
        return &regfile[op->val.integer] ;
    } else {
        return static_cast<Variable*>(op->val.ptr)->getAddress(getStack ((int)op->val.type - 100)) ;
    }
}


//
// get a value from an operand
//

inline Value &VirtualMachine::get (Operand *op) {
    return (op == NULL) ? none : 
        (op->type == tREGISTER ? regfile[op->val.integer] :
        (op->type == tLOCALVAR ? static_cast<Variable*>(op->val.ptr)->getValue (frame) :
        (op->type == tVALUE || op->type == tUNKNOWN ? op->val :
        static_cast<Variable*>(op->val.ptr)->getValue (getStack ((int)op->val.type - 100))))) ;
}

// override a variable with a new value.  This is used for virtual function assignment

void VirtualMachine::overassign (Operand *dest, Value &src) {
    if (dest->type != tLOCALVAR) {
        runtimeError ("Cannot override this value") ;
    }
    Variable *var = static_cast<Variable*>(dest->val.ptr) ;
    //int level = (int)dest->val.type - 100 ;
    //Value *stk = getStack (frame, level) ;
    var->oldValue = var->getValue (frame) ;
    var->flags |= VAR_OLDVALUE ;
    var->setValue (src, frame) ;
}


//
// set a boolean into a register or variable
//

inline void VirtualMachine::setbool (Operand *dest, bool v) {
    Value *val ;
    OpType t = dest->type ;
    if (t == tREGISTER) {
        val = &regfile[dest->val.integer] ;
    } else if (t == tLOCALVAR) {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        val = var->getAddress (frame) ;
        checkType (var->name, *val, T_INTEGER, "integer") ;
    } else {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        int level = (int)dest->val.type - 100 ;
        StackFrame *stk = getStack (level) ;
        val = var->getAddress (stk) ;
        checkType (var->name, *val, T_INTEGER, "integer") ;
    }
    val->destruct() ;
    val->integer = v ;
    val->type = T_INTEGER ;
}


//
// set an integer into a register or variable
//

inline void VirtualMachine::setint (Operand *dest, INTEGER v) {
    Value *val ;
    OpType t = dest->type ;
    if (t == tREGISTER) {
        val = &regfile[dest->val.integer] ;
    } else if (t == tLOCALVAR) {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        val = var->getAddress (frame) ;
        checkType (var->name, *val, T_INTEGER, "integer") ;
    } else {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        int level = (int)dest->val.type - 100 ;
        StackFrame *stk = getStack (level) ;
        val = var->getAddress (stk) ;
        checkType (var->name, *val, T_INTEGER, "integer") ;
    }
    val->destruct() ;
    val->integer = v ;
    val->type = T_INTEGER ;
}


//
// set a real into a register or variable
//

inline void VirtualMachine::setreal (Operand *dest, double v) {
    Value *val ;
    OpType t = dest->type ;
    if (t == tREGISTER) {
        val = &regfile[dest->val.integer] ;
    } else if (t == tLOCALVAR) {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        val = var->getAddress (frame) ;
        checkType (var->name, *val, T_REAL, "real") ;
    } else {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        int level = (int)dest->val.type - 100 ;
        StackFrame *stk = getStack (level) ;
        val = var->getAddress (stk) ;
        checkType (var->name, *val, T_REAL, "real") ;
    }
    val->destruct() ;
    val->real = v ;
    val->type = T_REAL ;
}

inline void VirtualMachine::setstring (Operand *dest, string *s) {
    Value *val ;
    OpType t = dest->type ;
    if (t == tREGISTER) {
        val = &regfile[dest->val.integer] ;
    } else if (t == tLOCALVAR) {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        val = var->getAddress (frame) ;
        checkType (var->name, *val, T_STRING, "string") ;
    } else {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        int level = (int)dest->val.type - 100 ;
        StackFrame *stk = getStack (level) ;
        val = var->getAddress (stk) ;
        checkType (var->name, *val, T_STRING, "string") ;
    }
    val->destruct() ;
    val->str = s ;
    incRef (s, string) ;
    val->type = T_STRING ;
}


inline void VirtualMachine::setvector (Operand *dest, Value::vector *v) {
    Value *val ;
    OpType t = dest->type ;
    if (t == tREGISTER) {
        val = &regfile[dest->val.integer] ;
    } else if (t == tLOCALVAR) {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        val = var->getAddress (frame) ;
        checkType (var->name, *val, T_VECTOR, "vector") ;
    } else {
        Variable *var = static_cast<Variable*>(dest->val.ptr) ;
        int level = (int)dest->val.type - 100 ;
        StackFrame *stk = getStack (level) ;
        val = var->getAddress (stk) ;
        checkType (var->name, *val, T_VECTOR, "vector") ;
    }
    val->destruct() ;
    val->vec = v ;
    incRef (v, vector) ;
    val->type = T_VECTOR ;
}



//
// make a copy of a value
//
Value VirtualMachine::copy (const Value &v, bool replacevars) {
    switch (v.type) {
    case T_STRING: {
        if (replacevars) {
           std::string news;
           std::string &s = v.str->str;
           std::string::size_type length = v.str->str.size();
           std::string::size_type i = 0;
           std::string expr;

           while (i < length) {
               if (s[i] == '#') {
                   i++;
                   if (s[i] == '#') {
                      // ## is #
                      news += '#';
                      i++;
                   } else {
                       expr = "";
                       if (s[i] == '{') {
                          i++;
                          int nbra = 1;
                          while (i < length) {
                              if (s[i] == '{') {
                                  nbra++;
                                  expr += s[i++];
                              } else if (s[i] == '}') {
                                  nbra--;
                                  if (nbra == 0) {
                                      i++;
                                      break;
                                  } else {
                                      expr += s[i++];
                                  }
                              } else {
                                  expr += s[i++];
                              }
                          }
                       } else {
                           // not #{...}
                           news += "#";
                           continue;        // 'i' still points to the character after the #
                       }
 
                       // we have an expression, evaluate it
           evalexpr:
                        Value value ("");

#if defined(_CC_GCC) && __GNUC__ == 2
                        std::strstream stream ;
#else
                        std::stringstream stream ;
#endif
                        stream << expr ;
                        string filename (ir->source->filename) ;

                        Context ctx (stream, filename, ir->source->lineno) ;
                        parserLock.acquire(true);
                        aikido->currentContext = &ctx ;
                        aikido->readLine() ;
                        stream.clear() ;
                        aikido->nextToken() ;

                        StackFrame *parentStack = frame->dlink ;        // parent stack frame is caller of eval
                        //Block *parentBlock = parentStack->block ;     // want to be in this block
                        aikido->currentStack = parentStack ;

                        aikido->currentScope = scopeStack[scopeSP-1];
                        aikido->currentScopeLevel =  scopeStack[scopeSP-1]->majorScope->level;
                        aikido->currentMajorScope = scopeStack[scopeSP-1]->majorScope ;
                        aikido->currentClass = NULL ;
                        try {
                            Node *node = aikido->expression() ;
                            Node *tree = node ;
                            aikido->currentContext = ctx.getPrevious() ;

                            if (node != NULL) {
                                node = new Node (aikido, RETURN, node) ;
                                codegen::CodeSequence *code = aikido->codegen->generate (node) ;
                                execute (code) ;
                                aikido->currentStack = NULL ;
                                aikido->deleteTree (tree) ;
                                parserLock.release(true) ;
                                Value result = retval ;
                                retval.destruct() ;
                                delete code ;
                                convertType (result, value);
                            } else {
                                aikido->currentStack = NULL ;
                                aikido->deleteTree (tree) ;
                                parserLock.release(true) ;
                                value = Value("none");
                            }
                        } catch (...) {
                            parserLock.release(true) ;
                            throw ;
                        }

                       news += value.str->str;
                   }
               } else if (s[i] == '<') {        // <%...%> is replaced by the output from the code
                   if (s[i+1] == '<') {         // <<% is escaped (replaced verbatim)
                       news += "<<";
                       i += 2;
                   } else if (s[i+1] != '%') {
                       news += s[i++];
                   } else {
                       i += 2;
                       bool isexpr = false;
                       if (s[i] == '=') {
                          // <%= is an expression
                          isexpr = true;
                          i++;
                       }
                       std::string code;
                       while (i < length) {
                           if (s[i] == '%' && s[i+1] == '>') {
                               i += 2;
                               break;
                           }
                           code += s[i++];
                       }
                       if (i == length) {
                           runtimeError ("Missing close %>") ;
                       }

                       if (isexpr) {
                           expr = code;
                           goto evalexpr;       // XXX: don't be such a lazy-arse
                       }

#if defined(_CC_GCC) && __GNUC__ == 2
                        std::strstream stream, outstream ;
#else
                        std::stringstream stream, outstream ;
#endif
                        stream << code << "\n";
                        string filename (ir->source->filename) ;

                        parserLock.acquire(true) ;
                        StaticLink newslink (frame->slink, frame) ;

                        Block *parentBlock = frame->block ;
                        int level = parentBlock->level + 1 ;

                        Block *block = new Class (aikido, "<inline>", level, parentBlock) ;
                        block->stacksize = 1 ;      // room for this

                        Variable *thisvar = new Variable ("this", 0) ;
                        thisvar->setAccess (PROTECTED) ;
                        block->variables["this"] = thisvar ;

                        block->setParentBlock (parentBlock) ;

                        // create a temp stack frame so that variables can be created in it.  The real stack
                        // frame will be an object allocated to the appropriate size when the number
                        // of variables is known
                        StackFrame *currframe = new (1) StackFrame (&newslink, frame, NULL, block, 1, true) ;

                        aikido->currentStack = currframe ;    /// parentStack ;       // we want variables to be created in object,
                        aikido->currentMajorScope = block ;
                        aikido->currentScope = block ;
                        aikido->currentScopeLevel = block->level ;
                        aikido->currentClass = (Class*)block ;

                        Context ctx (stream, filename, ir->source->lineno, aikido->currentContext) ;
                        aikido->currentContext = &ctx ;

                        Node *tree ;

                        try {
                            aikido->importedTree = NULL ;
                            tree = aikido->parseStream (stream) ;

                            block->body = new Node (aikido, SEMICOLON, aikido->importedTree, tree) ;
                            block->code = aikido->codegen->generate (static_cast<InterpretedBlock*>(block)) ;

                            aikido->currentContext = ctx.getPrevious() ;

                            aikido->currentStack = NULL ;
                        } catch (...) {
                            parserLock.release(true) ;
                            throw ;
                        }
                        parserLock.release(true) ;

                        Object *obj = new (block->stacksize) Object (block, &newslink, frame, &block->code->code[0]) ;
                        obj->ref++ ;
                        obj->varstack[0] = Value (obj) ;

                        VirtualMachine newvm (aikido);
                        newvm.output = new StdOutStream (&outstream) ;
                        newvm.output.stream->setAutoFlush (true) ;

                        newvm.execute (static_cast<InterpretedBlock*>(block), obj, &newslink, 0) ;

                        news += outstream.str();
                   }
               } else {
                   news += s[i++];
               }
           }
           return new string (news);
        } else {
            // faster
            return  new string (v.str->str) ;
        }
        }
    case T_VECTOR: {
        Value::vector *vec = new Value::vector (v.vec->size()) ;
        for (int i = 0 ; i < v.vec->size() ; i++) {
            (*vec)[i] = (*v.vec)[i] ;
        }
        return vec ;
        break ;
        }
    case T_MAP: {		// XXX: complete this
        break ;
        }
    default:
        return v ;
    }
}


//
// we have caught a signal.  Process it.  If a signal is caught and there
// is no signal handler for it an error is generated, otherwise the
// handler is called.
//

void VirtualMachine::processSignal (int sig) {
    Closure *handler = signals[sig] ;
    if (handler == NULL) {
       runtimeError ("No handler for signal %d", sig) ;
    } else {
        Operand dest ;
        CodeSequence *oldcode = currentCode ;
        Instruction *oldir = ir ;
        StackFrame *oldframe ;
        StaticLink *oldstaticlink ;
        oldframe = frame ;
        oldstaticlink = staticLink ;
        frame = getMainStack() ;
        staticLink = handler->slink ;
    
        Value thisptr ;
        thisptr.type = T_OBJECT ;
        thisptr.object = NULL ;
        stack[sp++] = sig ;
        stack[sp++] = &thisptr ;		// dummy thisptr
        currentCode = handler->block->code ;
        ir = &currentCode->code[0] ;
        try {
	    insignalhandler = true ;
            callFunction (&dest, 2, (Function*)handler->block, true) ;          // this is a closure
        } catch (...) {
            frame = oldframe ;
            staticLink = oldstaticlink ;
            currentCode = oldcode ;
            ir = oldir ;
            insignalhandler = false ;
            throw ;
        }
        insignalhandler = false ;
        frame = oldframe ;
        staticLink = oldstaticlink ;
        currentCode = oldcode ;
        ir = oldir ;
    }
}


//
// *************************************************************************************
// Execution functions
// *************************************************************************************
//

bool VirtualMachine::execute (InterpretedBlock *block, StackFrame *f, StaticLink *slink, int startaddr) {
    int oldscopeSP = scopeSP ;
    scopeStack[scopeSP++] = block ;
    bool b ;
    try {
         b = execute (block->code, f, slink, startaddr) ;
    } catch (Exception e) {
#ifdef DEBUGON
        if (aikido->properties & DEBUG) {
            aikido->reportException (e) ;
            for (;;) {
                aikido->debugger->run (this, ir, scopeStack, scopeSP, frame, staticLink) ;
                if (debugState == RERUN) {
                    throw RerunException() ;
                }
                std::cerr << "Can't continue execution from a runtime error\n" ;
            }
        } 
#endif
        scopeSP = oldscopeSP ;
        throw ;
    } catch (...) {
        scopeSP = oldscopeSP ;
        throw ;
    }
    scopeSP = oldscopeSP ;
    return b ;
}

bool VirtualMachine::execute (CodeSequence *code) {
    return  execute (code, frame, staticLink, 0) ;
}

bool VirtualMachine::execute (InterpretedBlock *block, CodeSequence *code, StackFrame *f, StaticLink *slink, int startaddr) {
    int oldscopeSP = scopeSP ;
    scopeStack[scopeSP++] = block ;
    bool b ;
    try {
         b = execute (code, f, slink, startaddr) ;
    } catch (...) {
        scopeSP = oldscopeSP ;
        throw ;
    }
    scopeSP = oldscopeSP ;
    return b ;
}

bool VirtualMachine::execute (CodeSequence *code, StackFrame *f, StaticLink *slink, int startaddr) {
    Value *oldregfile = regfile ;
    regfile = &registers[regs] ;
    int oldregs = regs ;			// the caller's register file
    regs += code->nregs ;			// set for next call
    StackFrame *oldframe;
    StaticLink *oldstaticlink ;
    oldframe = frame ;
    oldstaticlink = staticLink ;
    frame = f ;
    staticLink = slink ;
    int oldpc = pc ;
    Instruction *oldir = ir ;
    CodeSequence *oldcode = currentCode ;
    currentCode = code ;
    bool b ;
    try {
        b = execute (startaddr) ;
    } catch (YieldValue &y) {
        // restore caller's register files
        // don't destruct the register values
        regs = oldregs ;
        regfile = oldregfile ;
        frame = oldframe ;
        staticLink = oldstaticlink ;
        pc = oldpc ;
        ir = oldir ;
        currentCode = oldcode ;
        throw ;

    } catch (...) {
        for (int i = oldregs ; i < regs ; i++) {
            registers[i].destruct() ;
        }

        // restore caller's register files
        regs = oldregs ;
        regfile = oldregfile ;
        frame = oldframe ;
        staticLink = oldstaticlink ;
        pc = oldpc ;
        ir = oldir ;
        currentCode = oldcode ;
        throw ;
    }

    for (int i = oldregs ; i < regs ; i++) {
        registers[i].destruct() ;
    }

    // restore caller's register files
    regs = oldregs ;
    regfile = oldregfile ;
    frame = oldframe ;
    staticLink = oldstaticlink ;
    pc = oldpc ;
    ir = oldir ;
    currentCode = oldcode ;
    return b ;
}

// execute a subroutine of code up until an END, RET or RETVAL
// time critical function.  The interpreter spends most of its
// time in this function.

bool VirtualMachine::execute (int startaddr) {
    Instruction *oldir = ir ;			// set instruction register
    pc = startaddr ;				// set pc
    std::vector<Instruction> &code = currentCode->code ;
    InterpretedBlock *opfunc ;
    std::vector<Instruction>::iterator cp = code.begin() ;
    bool endReturn = true ;

    // exits when END, RET or RETVAL instuction is executed (or exception)
    for (;;) {
        if (interrupted) {
            interrupted = false ;
            throw Exception ("Interrupt") ;
            break ;
        }

        ir = static_cast<Instruction*>(&(*(code.begin() + pc++))) ;
        Instruction *ir2 = ir ;
        if (signalnum >= 0) {
#ifndef _OS_WINDOWS
            if (insignalhandler) {			// signal inside a signal?
                signal (signalnum, SIG_DFL) ;
                kill (getpid(), signalnum) ;
            }
#endif
            int s = signalnum ;
            signalnum = -1 ;
            processSignal(s) ;
        }
#ifdef DEBUGON
        if (aikido->properties & DEBUG) {
            if (aikido->debugger->isStopped()) {
                aikido->debugger->blockThread() ;		// block this thread until debugger starts again (may write degugState)
            } else {
                // CAUTION: the Debugger::run method writes directly to the debugState variable so
                // that we can switch threads
                if (interrupt) {
                    interrupt = false ;
                    aikido->debugger->run (this, ir, scopeStack, scopeSP-1, frame, staticLink) ;
       
                } else if (debugState == NEXT || debugState == STEP) {
                   if (ir->flags & INST_STATEMENT) {
                        aikido->debugger->run (this, ir, scopeStack, scopeSP, frame, staticLink) ;
                   }
                } else if (debugState == NEXTI || debugState == STEPI) {
                      std::cout << (pc - 1) << "\t" ;
                      ir->print (std::cout) ;
                      aikido->debugger->run (this, ir, scopeStack, scopeSP, frame, staticLink) ;
                }
                signal (SIGINT, interruptHit) ;
            }
        }
#endif  // DEBUGON
        Opcode op = ir->opcode ;
    executebp:
#ifdef DEBUGON
        if (debugState == RERUN) {
            throw RerunException() ;
        }
        if (tracecode) {
            std::cerr << (pc - 1) << "\t" ;
            ir->print (std::cerr) ;
        }
#endif // DEBUGON
        switch (op) {
        case opBREAKPOINT:
            if (debugState != RUN) {
                op = static_cast<Opcode>(aikido->debugger->findBreakpointOp (ir)) ;                // yes, get real op
            } else {
                aikido->debugger->breakpointHit (this, ir, scopeStack, scopeSP, frame, staticLink, (int&)op) ;
            }
            goto executebp ;            // try the switch again with the real operation
            break ;
        case opNOP:
            break ;
        case opMOV:
            set (ir->dest, get (ir->src[0]), true, true) ;
            break ;
        case opMOVC:
            set (ir->dest, get (ir->src[0]), false, true) ;
            break ;
        case opMOVF:
            set (ir->dest, get (ir->src[0]), false, false) ;
            break ;
        case opMOVO:				// override assign
            overassign (ir->dest, get (ir->src[0])) ;
            break ;
        case opLD: {
            Value *ptr = get(ir->src[0]).addr ;
            set (ir->dest, *ptr) ;
            break ;
            }
        case opST: {
            Value *ptr = get(ir->src[0]).addr ;
            *ptr = get (ir->src[1]) ;
            set (ir->dest, *ptr) ;
            break ;
            }
        case opCOPY: {		// only used for strings
            set (ir->dest, copy (get(ir->src[0]), (ir->flags & INST_REPLACEVARS) != 0)) ;
            //string *s = new string (get(ir->src[0]).str->str) ;
            //incRef (s, string) ;
            //setstring (ir->dest, s) ;
            //decRef (s, string) ;
            break ;
            }

        case opADD:
            add (ir->dest, get(ir->src[0]), get (ir->src[1])) ;
            break ;
        case opADD_I:
            setint (ir->dest, get(ir->src[0]).integer + get(ir->src[1]).integer) ;
            break ;
        case opADD_R:
            setreal (ir->dest, get(ir->src[0]).real + get(ir->src[1]).real) ;
            break ;
        case opADD_S:  {	// due to bug in exception handling in Sun CC, need to enclose this in try
            try {
                const string &s1 = *get(ir->src[0]).str ;
                const string &s2 = *get(ir->src[1]).str ;
                string *s = new string (s1+ s2) ;
                setstring (ir->dest, s) ;
            } catch (...) {
                throw ;
            }
            break ;
            }
        case opADD_V: {
            Value::vector *v1 = get (ir->src[0]).vec ;
            Value::vector *v2 = get (ir->src[1]).vec ;
            Value::vector *vv = new Value::vector (v1->size() + v2->size()) ;
            vv->clear() ;
            *vv = *v1 + *v2 ;
            vv->ref = 0 ;		// no references yet
            setvector (ir->dest, vv) ;
            break ;
            }

        case opSUB:
            sub (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            break ;
        case opSUB_I:
            setint (ir->dest, get(ir->src[0]).integer - get(ir->src[1]).integer) ;
            break ;
        case opSUB_R:
            setreal (ir->dest, get(ir->src[0]).real - get(ir->src[1]).real) ;
            break ;

        case opMUL:
            mul (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            break ;
        case opMUL_I:
            setint (ir->dest, get(ir->src[0]).integer * get(ir->src[1]).integer) ;
            break ;
        case opMUL_R:
            setreal (ir->dest, get(ir->src[0]).real * get(ir->src[1]).real) ;
            break ;

        case opDIV:
            try {
                div (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            } catch (...) {
                throw ;
            }
            break ;
        case opDIV_I:
            if (get(ir->src[1]).integer == 0) {
                runtimeError ("Division by zero") ;
            }
            setint (ir->dest, get(ir->src[0]).integer / get(ir->src[1]).integer) ;
            break ;
        case opDIV_R:
            if (get(ir->src[1]).real == 0) {
                runtimeError ("Division by zero") ;
            }
            setreal (ir->dest, get(ir->src[0]).real / get(ir->src[1]).real) ;
            break ;

        case opMOD:
            mod (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            break ;
        case opMOD_I:
            if (ir->src[1]->val.integer == 0) {
                runtimeError ("Division by zero") ;
            }
            setint (ir->dest, get(ir->src[0]).integer % get(ir->src[1]).integer) ;
            break ;
        case opMOD_R: 
            if (get(ir->src[1]).real == 0) {
                runtimeError ("Division by zero") ;
            }
            setreal (ir->dest, fmod (get(ir->src[0]).real, get(ir->src[1]).real)) ;
            break ;

        case opSLL:
            sll (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            break ;
        case opSLL_I:
            setint (ir->dest, get(ir->src[0]).integer << get(ir->src[1]).integer) ;
            break ;

        case opSRL:
            srl (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            break ;
        case opSRL_I:
            setint (ir->dest, (UINTEGER)get(ir->src[0]).integer >> get(ir->src[1]).integer) ;
            break ;

        case opSRA:
            sra (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            break ;
        case opSRA_I:
            setint (ir->dest, get(ir->src[0]).integer >> get(ir->src[1]).integer) ;
            break ;

        case opOR:
            bitwiseor (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            break ;
        case opOR_I:
            setint (ir->dest, get(ir->src[0]).integer | get(ir->src[1]).integer) ;
            break ;

        case opXOR:
            bitwisexor (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            break ;
        case opXOR_I:
            setint (ir->dest, get(ir->src[0]).integer ^ get(ir->src[1]).integer) ;
            break ;

        case opAND:
            bitwiseand (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            break ;
        case opAND_I:
            setint (ir->dest, get(ir->src[0]).integer & get(ir->src[1]).integer) ;
            break ;
        case opAND_V: {
            Value::vector *v1 = get (ir->src[0]).vec ;
            Value::vector *v2 = get (ir->src[1]).vec ;
            Value::vector *rs = new Value::vector (1) ;
            rs->clear() ;
            *rs = *v1 & *v2 ;
            rs->ref = 0 ;		// no references yet
            setvector (ir->dest, rs) ;
            break ;
            }

        case opCOMP:
            comp (ir->dest, get (ir->src[0])) ;
            break ;
        case opCOMP_I:
            setint (ir->dest, ~get(ir->src[0]).integer) ;
            break ;

        case opNOT:
            boolnot (ir->dest, get (ir->src[0])) ;
            break ;
        case opNOT_I:
            setbool (ir->dest, !get(ir->src[0]).integer) ;
            break ;
        case opNOT_R:
            setbool (ir->dest, !get(ir->src[0]).real) ;
            break ;

        case opUMINUS:
            uminus (ir->dest, get (ir->src[0])) ;
            break ;
        case opUMINUS_I:
            setint (ir->dest, -get(ir->src[0]).integer) ;
            break ;
        case opUMINUS_R:
            setreal (ir->dest, -get(ir->src[0]).real) ;
            break ;

        case opSIZEOF:
            dosizeof (ir->dest, ir->src[0]) ;
            break ;
        case opTYPEOF:
            dotypeof (ir->dest, ir->src[0]) ;
            break ;
        case opTHROW:
            throw Exception (get (ir->src[0]), frame, staticLink, scopeStack[scopeSP-1], scopeStack[scopeSP-1]->majorScope->level, ir->source, frame->block) ;
            break ;
        case opCAST:
            cast (ir->dest, ir->src[0], ir->src[1], ir->src[2]) ;
            break ;
        case opCMPEQ:
            setbool (ir->dest, cmpeq (get(ir->src[0]), get(ir->src[1]))) ;
            break ;
        case opCMPEQ_I:
            setbool (ir->dest, get(ir->src[0]).integer == get(ir->src[1]).integer) ;
            break ;
        case opCMPEQ_R:
            setbool (ir->dest, get(ir->src[0]).real == get(ir->src[1]).real) ;
            break ;
        case opCMPEQ_S:
            setbool (ir->dest, *get(ir->src[0]).str == *get(ir->src[1]).str) ;
            break ;
    
        case opCMPNE:
            setbool (ir->dest, cmpne (get(ir->src[0]), get(ir->src[1]))) ;
            break ;
        case opCMPNE_I:
            setbool (ir->dest, get(ir->src[0]).integer != get(ir->src[1]).integer) ;
            break ;
        case opCMPNE_R:
            setbool (ir->dest, get(ir->src[0]).real != get(ir->src[1]).real) ;
            break ;
        case opCMPNE_S:
            setbool (ir->dest, *get(ir->src[0]).str != *get(ir->src[1]).str) ;
            break ;

        case opCMPLT:
            setbool (ir->dest, cmplt (get(ir->src[0]), get(ir->src[1]))) ;
            break ;
        case opCMPLT_I:
            setbool (ir->dest, get(ir->src[0]).integer < get(ir->src[1]).integer) ;
            break ;
        case opCMPLT_R:
            setbool (ir->dest, get(ir->src[0]).real < get(ir->src[1]).real) ;
            break ;
        case opCMPLT_S:
            setbool (ir->dest, *get(ir->src[0]).str < *get(ir->src[1]).str) ;
            break ;

        case opCMPLE:
            setbool (ir->dest, cmple (get(ir->src[0]), get(ir->src[1]))) ;
            break ;
        case opCMPLE_I:
            setbool (ir->dest, get(ir->src[0]).integer <= get(ir->src[1]).integer) ;
            break ;
        case opCMPLE_R:
            setbool (ir->dest, get(ir->src[0]).real <= get(ir->src[1]).real) ;
            break ;
        case opCMPLE_S:
            setbool (ir->dest, *get(ir->src[0]).str <= *get(ir->src[1]).str) ;
            break ;

        case opCMPGT:
            setbool (ir->dest, cmpgt (get(ir->src[0]), get(ir->src[1]))) ;
            break ;
        case opCMPGT_I:
            setbool (ir->dest, get(ir->src[0]).integer > get(ir->src[1]).integer) ;
            break ;
        case opCMPGT_R:
            setbool (ir->dest, get(ir->src[0]).real > get(ir->src[1]).real) ;
            break ;
        case opCMPGT_S:
            setbool (ir->dest, *get(ir->src[0]).str > *get(ir->src[1]).str) ;
            break ;

        case opCMPGE:
            setbool (ir->dest, cmpge (get(ir->src[0]), get(ir->src[1]))) ;
            break ;
        case opCMPGE_I:
            setbool (ir->dest, get(ir->src[0]).integer >= get(ir->src[1]).integer) ;
            break ;
        case opCMPGE_R:
            setbool (ir->dest, get(ir->src[0]).real >= get(ir->src[1]).real) ;
            break ;
        case opCMPGE_S:
            setbool (ir->dest, *get(ir->src[0]).str >= *get(ir->src[1]).str) ;
            break ;

        case opB:
            pc = ir->src[0]->val.integer ;
            break ;
        case opBT:
            if (get (ir->src[0]).integer != 0) {
                pc = ir->src[1]->val.integer ;
            }
            break ;
        case opBF:
            if (get (ir->src[0]).integer == 0) {
                pc = ir->src[1]->val.integer ;
            }
            break ;
        case opCALL:
            call (ir->dest, ir->src[0], ir->src[1]) ;
            break ;
        case opSUPERCALL:
            if (!supercall (ir->src[0], ir->src[1], ir->src[2])) {
                 endReturn = false ;
                 goto endloop ;
            }
            break ;
        case opYIELD:
            throw YieldValue (pc, scopeSP, regs, currentCode, Value()) ;
            break ;
        case opYIELDVAL:
            throw YieldValue (pc, scopeSP, regs, currentCode, get(ir->src[0])) ;
            break ;
        case opANNOTATE:
            getAnnotations (ir->dest, ir->src[0]);
            break;
        case opRET:
            endReturn = false ;
            goto endloop ;
            break ;
        case opRETVAL:
            make_closure (retval, ir->src[0]) ;
            endReturn = false ;
            goto endloop ;
            break ;
        case opRETVAL_I:
            retval.integer = get (ir->src[0]).integer ;
            retval.type = T_INTEGER ;
            goto endloop ;
            break ;
        case opRETVAL_R:
            retval.real = get (ir->src[0]).real ;
            retval.type = T_REAL ;
            goto endloop ;
            break ;
        case opRETVAL_S:
            retval.str = get (ir->src[0]).str ;
            retval.type = T_STRING ;
            incRef (retval.str, string) ;
            goto endloop ;
            break ;
        case opRETVAL_V:
            retval.vec = get (ir->src[0]).vec ;
            retval.type = T_VECTOR ;
            incRef (retval.vec, vector) ;
            goto endloop ;
            break ;

        case opTRY:
            dotry (ir->src[0], ir->src[1], ir->src[2]) ;
            break ;
        case opCATCH:
            set (ir->src[0], exception) ;
            exception.destruct() ;
            break ;
        case opNEW:
            call (ir->dest, ir->src[0], ir->src[1], ir->src[2]) ;
            break ;
        case opNEWVECTOR: {
            int ctstart = ir->src[1]->val.integer ;
            Value firstelement ;
            if (ctstart != 0) {
                execute (ctstart) ;			// find the type of the constructor
                firstelement = retval ;
                retval.destruct() ;
                if (firstelement.type == T_BYTE) {
                    newByteVector (ir->dest, sp - 1, ir->src[0]->val.integer, ctstart, ir->src[2]->val.integer, firstelement.integer) ;
                } else {
                    newVector (ir->dest, sp - 1, ir->src[0]->val.integer, ctstart, ir->src[2]->val.integer, firstelement) ;
                }
                firstelement.destruct() ;
            } else {
                newVector (ir->dest, sp - 1, ir->src[0]->val.integer) ;
            }
            sp -= ir->src[0]->val.integer ;
            if (ir->src[2]->val.integer != 0) {
                pc = ir->src[2]->val.integer ;
            }
            break ;
            }
        case opDELETE:
            dodelete (get (ir->src[0])) ;
            break ;
        case opSTREAM: 
            stream (ir->dest, get (ir->src[0]), get (ir->src[1])) ;
            break ;
        case opFINDV:
            findValue (ir->dest, ir->src[0], ir->src[1]) ;
            break ;
        case opPUSHV: {     // push the first operand then find a value 
            if (sp == (currstacksize - 1)) {
                growStack() ;
            }
            make_closure (stack[sp++], ir->src[0]) ;
            findValue (ir->dest, ir->src[0], ir->src[1]) ;
            break ;
            }
        case opSTV:
            storeValue (ir->dest, ir->src[0], ir->src[1], ir->src[2]) ;
            break ;
        case opFINDA:
            findAddress (ir->dest, ir->src[0], ir->src[1]) ;
            break ;
        case opGETOBJ: {
            Value &v = get (ir->src[0]) ;
            set (ir->dest, v) ;
            break ;
            }
        case opPUSHSCOPE: 
            scopeStack[scopeSP++] = static_cast<Scope*>(ir->src[0]->val.ptr) ;
            break ;
        case opPOPSCOPE: {
            int ns = ir->src[0]->val.integer ;
            int nt = ir->src[1]->val.integer ;
            int nf = ir->src[2]->val.integer ;
            scopeSP -= ns ;
            if (scopeSP < 0) {
                throw "scope stack pointer problem" ;
            }
            while (nf-- > 0) {
                delete forStack.top() ;
                forStack.pop() ;
            }
            if (nt > 0) {
                throw nt ;		// pop the try stack
            }
            break ;
            }

        case opADDR: {
            set (ir->dest, getaddr (ir->src[0])) ;
            break ;
            }
        // can't get the address of a subscript range.
        case opADDRSUB:
            if (ir->src[2] != NULL) {
                 getsub (ir->dest, get (ir->src[0]), get(ir->src[1]), get(ir->src[2])) ;
            } else {
                 addrsub (ir->dest, get (ir->src[0]), get(ir->src[1])) ;
            }
            break ;
        case opGETSUB:
            if (ir->src[2] != NULL) {
                getsub (ir->dest, get (ir->src[0]), get(ir->src[1]), get(ir->src[2])) ;
            } else {
                getsub (ir->dest, get (ir->src[0]), get(ir->src[1])) ;
            }
            break ;
        case opSETSUB:
            if (ir->src[3] != NULL) {
                setsub (ir->dest, get (ir->src[0]), get(ir->src[1]), get(ir->src[2]), get(ir->src[3])) ;
            } else {
                setsub (ir->dest, get (ir->src[0]), get(ir->src[1]), get(ir->src[2])) ;
            }
            break ;
        case opDELSUB:
            if (ir->src[2] != NULL) {
                delsub (get (ir->src[0]), get(ir->src[1]), get(ir->src[2])) ;
            } else {
                delsub (get (ir->src[0]), get(ir->src[1])) ;
            }
            break ;
        case opIN:
            if (ir->src[2] != NULL) {
                inrange (ir->dest, ir->src[0], ir->src[1], ir->src[2]) ;
            } else {
                in (ir->dest, ir->src[0], ir->src[1]) ;
            }
            break ;
        case opEND:
            endReturn = true ;
            goto endloop ;
            break ;
        case opMUX:
            if (!cmpeq (get (ir->src[0]), 0)) {
                set (ir->dest, get (ir->src[1])) ;
            } else {
                set (ir->dest, get (ir->src[2])) ;
            }
            break ;
        case opFOREACH:	
            if (ir->src[3] != NULL) {
                foreach (getaddr (ir->src[0]), get (ir->src[1]), get (ir->src[3]), ir->src[2]->val.integer) ;
            } else {
                foreach (getaddr (ir->src[0]), get (ir->src[1]), ir->src[2]->val.integer) ;
            }
            break ;
        case opNEXT:
            next() ;
            break ;
        case opPUSH: {
            if (sp == (currstacksize - 1)) {
                growStack() ;
            }
            make_closure (stack[sp++], ir->src[0]) ;
            break ;
            }
        case opPUSHADDR:
            if (sp == (currstacksize - 1)) {
                growStack() ;
            }
            make_closure_addr (stack[sp++], ir->src[0]) ;
            break ;
        case opPOP:
            sp-- ;
            break ;
        case opGETTHIS: {    // and push value onto stack
            Object *t = getthis (ir->src[0]) ;
            if (sp == (currstacksize - 1)) {
                growStack() ;
            }
            make_closure (stack[sp++], t) ;
            break ;
            }
        case opFOREIGN:
            delegate (get (ir->src[0])) ;
            break ;
        case opMKVECTOR:
            makeVector (ir->dest, ir->src[0]) ;
            break ;
        case opMKMAP:
            makeMap (ir->dest, ir->src[0]) ;
            break ;
        case opMACRO:
            instantiateMacro (static_cast<Macro*>(ir->src[0]->val.ptr), ir->src[1]) ;
            break ;
        case opENUM:
            doenum (get (ir->src[0])) ;
            break ;
        case opINLINE:
            doinline(ir->dest, ir->src[0]) ;
            break ;
        case opINSTANCEOF:
            instanceof (ir->dest, ir->src[0], ir->src[1]) ;
            break ;
        case opMONITORENTER:
            monitorEnter (ir->src[0]) ;
            break ;
        case opMONITOREXIT:
            monitorExit (ir->src[0]) ;
            break ;
#ifdef JAVA
        case opCALLJAVA:
            calljava (ir->dest, ir->src[0], ir->src[1]->val.integer) ;
            break ;
        case opCALLJNI:
            calljni (ir->dest, ir->src[0]->val.integer, ir->src[1]->val.ptr) ;
            break ;
        case opCHECKCAST:
            checkcast (ir->dest, ir->src[0], ir->src[1]) ;
            break ;
        case opREALCOMPARE:
            realcompare (ir->dest, ir->src[0], ir->src[1], ir->src[2]->val.integer) ;
            break ;
        case opLOOKUPSWITCH:
            lookupswitch (ir->src[0], ir->src[1]->val.integer) ;
            break ;
        case opTABLESWITCH:
            tableswitch (ir->src[0], ir->src[1]->val.integer, ir->src[2]->val.integer) ;
            break ;
        case opJMPT:
            if (get (ir->src[0]).integer != 0) {
                pc = ir->src[1]->val.integer ;
                int nt = ir->src[2]->val.integer ;
                if (nt > 0) {
                    throw nt ;		// pop the try stack
                }
            }
            break ;
#endif
        default:
            runtimeError ("Illegal instruction opcode") ;
        }
    }
endloop:
    ir = oldir ;
    return endReturn ;
}



//
// type to string conversion
//

string VirtualMachine::typestring (const Value &v) {
    switch (v.type) {
    case T_INTEGER:
        return "integer" ;

    case T_BYTE:
        return "byte" ;
        
    case T_REAL:
        return "real" ;
        
    case T_CHAR:
        return "char" ;
        
    case T_STRING:
        return "string" ;
        
    case T_VECTOR:
        return "vector" ;

    case T_BYTEVECTOR:
        return "bytevector" ;

    case T_MAP:
        return "map" ;

    case T_STREAM:
        return "stream" ;

    case T_MEMORY:
        return "memory" ;

    case T_POINTER:
        return "pointer" ;

    case T_OBJECT: {
        if (v.object == NULL) {
            return "null" ;
        }
        Block *block = v.object->block ;
        return block->name ;
        }

    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD: 
    case T_ENUM:
        return v.block->name ;

    case T_CLOSURE:
        if (v.closure->vm_state != NULL) {
            return "closure" ;
        } else {
            return v.closure->block->name ;
        }

    case T_NONE:
        return "none" ;

    case T_ENUMCONST:
        return v.ec->en->name ;
        
    default:
        return "unknown" ;
    }
}



//
// sizeof operator
//

void VirtualMachine::dosizeof (Operand *dest, Operand *op) {
    Value &v = get (op) ;
    switch (v.type) {
    case T_INTEGER:
    case T_REAL:
        set (dest, 8) ;
        break ;

    case T_MEMORY:
        set (dest, v.mem->size) ;
        break ;

    case T_POINTER:
        set (dest, v.pointer->mem->size - v.pointer->offset) ;
        break ;

    case T_CHAR:
    case T_BYTE:
        set (dest, 1) ;
        break ;
        
    case T_STRING:
        set (dest, (INTEGER)v.str->size()) ;
        break ;
        
    case T_VECTOR:
        set (dest, (INTEGER)v.vec->size()) ;
        break ;

    case T_BYTEVECTOR:
        set (dest, (INTEGER)v.bytevec->size()) ;
        break ;

    case T_MAP:
        set (dest, (INTEGER)v.m->size()) ;
        break ;

    case T_OBJECT: {
        InterpretedBlock *opfunc ;

        if (v.object == NULL) {
            set (dest, 0) ;
        } else if ((opfunc = checkForOperator (v, SIZEOF)) != NULL) {
            callFunction (dest, opfunc, v) ;
        } else {
            Block *block = v.object->block ;
            set (dest, block->stacksize) ;
        }
        break ;
        }

    case T_MONITOR: 
    case T_INTERFACE: 
    case T_CLASS:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD: 
        runtimeError ("Cannot use sizeof operator on a value of type %s", typestring (v).c_str()) ;
        break ;

    case T_CLOSURE:
        if (v.closure->type == T_ENUM) {
            set (dest, ((Enum*)v.closure->block)->consts.size()) ;
        } else {
            runtimeError ("Cannot use sizeof operator on a value of type %s", typestring (v).c_str()) ;
        }
        break ;

    case T_ENUM: 
        set (dest, v.en->consts.size()) ;
        break ;
        
    default:
        set (dest, 0) ;
        break ;
    }
}

//
// typeof operator
//

void VirtualMachine::dotypeof (Operand *dest, Operand *op) {
    Value &v = get (op) ;
    switch (v.type) {
    case T_INTEGER:
        set (dest, "integer") ;
        break ;

    case T_BYTE:
        set (dest, "byte") ;
        break ;
        
    case T_REAL:
        set (dest, "real") ;
        break ;
        
    case T_CHAR:
        set (dest, "char") ;
        break ;
        
    case T_STRING:
        set (dest, "string") ;
        break ;
        
    case T_VECTOR:
        set (dest, "vector") ;
        break ;
        
    case T_MEMORY:
        set (dest, "memory") ;
        break ;
        
    case T_POINTER:
        set (dest, "pointer") ;
        break ;
        
    case T_BYTEVECTOR:
        set (dest, "bytevector") ;
        break ;
        
    case T_MAP:
        set (dest, "map") ;
        break ;
        
    case T_STREAM:
        set (dest, "stream") ;
        break ;
        
    case T_OBJECT: {
        InterpretedBlock *opfunc ;

        if (v.object == NULL) {
            set (dest, "null") ;
        } else if ((opfunc = checkForOperator (v, TYPEOF)) != NULL) {
            callFunction (dest, opfunc, v) ;
        } else {
            Block *block = v.object->block ;
            switch (block->type) {
            case T_FUNCTION:
                set (dest, (Function*)block) ;
                break ;
            case T_THREAD:
                set (dest, (Thread*)block) ;
                break ;
            case T_CLASS:
                set (dest, (Class*)block) ;
                break ;
            case T_INTERFACE:
                set (dest, (Interface*)block) ;
                break ;
            case T_MONITOR:
                set (dest, (MonitorBlock*)block) ;
                break ;
            case T_PACKAGE:
                set (dest, (Package*)block) ;
                break ;
            case T_ENUM:
                set (dest, (Enum*)block) ;
                break ;
            }
        }
        break ;
        }
    
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD: 
    case T_ENUM:
    case T_CLOSURE:
        set (dest, v) ;
        break ;
        
    case T_NONE:
        set (dest, "none") ;
        break ;
        
    case T_ENUMCONST:
        set (dest, v.ec->en) ;
        break ;
        
    default:
        set (dest, "unknown") ;
        break ;
    }
}


//
// call a block
//

// the 't' operand may be NULL.  It is the number of defined types
void VirtualMachine::call (Operand *dest, Operand *n, Operand *f, Operand *t) {
    int nargs = n->val.integer ;		// guarnteed to be constant
    int ntypes = t == NULL ? 0 : t->val.integer;

    Value &val = get (f) ;
    InterpretedBlock *opfunc ;

#ifdef DEBUGON
    DebugState oldstate = debugState ;
    if (debugState == NEXT || debugState == NEXTI) {
        debugState = NEXTRUN ;
    }
#endif
    switch (val.type) {
    case T_FUNCTION:
        callFunction (dest, nargs, val.func) ;
        break ;
    case T_THREAD:
        callThread (dest, nargs, val.func) ;
        break ;
    case T_OBJECT:
        if (val.object != NULL && (opfunc = checkForOperator (val, LBRACK)) != NULL) {
            callFunction (dest, nargs, opfunc) ;
        } else {
            runtimeError ("Can't call this object, there is no operator() defined for it") ;
        }
        break ;
    case T_INTERFACE:
        runtimeError ("Cannot create an instance of an interface") ;
        break ;

    case T_MONITOR:
    case T_CLASS:
        newObject (dest, nargs, val.block->type, val.cls, true, ntypes) ;
        break ;
    case T_PACKAGE:
        runtimeError ("Cannot create an instance of a package") ;
        break ;
    case T_CLOSURE: {
        StaticLink *oldslink = staticLink ;             // save current slink
        staticLink = val.closure->slink ;               // set closure's slink
        try {
            switch (val.closure->type) {
            case T_FUNCTION:
            case T_THREAD:
                callFunction (dest, nargs, (Function*)val.closure->block, true) ;
                break ;
            case T_INTERFACE:
                runtimeError ("Cannot create an instance of an interface") ;
                break ;
            case T_CLASS:
            case T_MONITOR:
                newObject (dest, nargs, val.closure->block->type, (Class*)val.closure->block, true) ;
                break ;
            case T_PACKAGE:
                runtimeError ("Cannot create an instance of a package") ;
                break ;
            }
        } catch (...) {
            staticLink = oldslink ;
            throw ;
        }
        staticLink = oldslink ;
        break ;
        }
    case T_NONE: {
        if (ir->opcode == opCALL) {			// calling an integer (eg) is illegal
            runtimeError ("Illegal call, cannot call a value of type %s", typestring (val.type).c_str()) ;
        }
        Variable *var = static_cast<Variable*>(f->val.ptr);
        if (var->flags & VAR_INVENTED) {
            // here we want to invoke the factory object creator in the system class 
            stack[sp-1] = Value (static_cast<Variable*>(f->val.ptr)->name);
            stack[sp] = stack[sp-1];
            sp++;
            nargs++;
            callFunction (dest, nargs, objectFactory) ;
            sp -= 2;
        } else {
            set (dest, val);
            sp--;
        }
        break;
        }

    default:
        if (ir->opcode == opCALL) {			// calling an integer (eg) is illegal
            runtimeError ("Illegal call, cannot call a value of type %s", typestring (val.type).c_str()) ;
        }
	set (dest, val) ;					// but newing one is OK
        sp-- ;						// need to pop the this para
        break ;
    }
#ifdef DEBUGON
    if (debugState == NEXTRUN) {
        debugState = oldstate ;
    } else if (debugState == STEPUP) {		// upon return from function we want to stop
        if (oldstate != STEPUP) {		// if state was STEPUP on entry we keep it the same
            debugState = STEP ;
        }
    }
#endif
}


// call a function with the arguments on the stack
void VirtualMachine::callFunction (Operand *dest, int nargs, Function *func, bool isclosure) {
    Object *thisobj = NULL ;
    StaticLink *slink = staticLink ;
    StaticLink newslink ;

    Value &stacktop = stack[sp-1] ;
    InterpretedBlock *caller = (InterpretedBlock*)frame->block ;
    bool callerIsStatic = caller->flags & BLOCK_STATIC ;

    //std::cerr << "Calling function " << func->name << "\n";

    if (nargs > 0) {
        // this sequence of code works out the static link for the call.
        //
        // builtin methods are in the main block, so we need to adust the static
        // link to be right for them
        if (func->flags & BLOCK_BUILTINMETHOD) {
            int n = scopeStack[scopeSP-1]->majorScope->level - func->level ;	// regular function
            //std::cerr << "calling builtin method at level " << n << "\n";
            slink = staticLink ;
            while (n-- > 0) {
                //std::cerr << "parent static frame: " << slink->frame->block->name << "\n";
                slink = slink->next ;
            }
            //std::cerr << "final parent static frame: " << slink->frame->block->name << "\n";
        } else if (!isclosure && !callerIsStatic && !(func->flags & BLOCK_STATIC) && func->parentClass != NULL) {
            if (stacktop.type == T_OBJECT) {
                thisobj = stacktop.object ;		// top element on stack
            } else {
                thisobj = frame->varstack[0].object ;
            }
            newslink.next = &thisobj->objslink ;                
            newslink.frame = thisobj ;
            slink = &newslink ;
            //std::cerr << "parent static frame: " << slink->frame->block->name << "\n";
            // if we are calling a function in the same object and we had better
            // adjust the static chain to make sure that the function gets
            // what it is expecting.   This can happen if we inherit from
            // something at a lower scope level.   Same for newObject
            if (scopeSP > 0 && frame->size > 0 && frame->varstack[0].type == T_OBJECT && frame->varstack[0].object == thisobj) {
                // although the function is in the same class, it may be at a lower static level
                int n = thisobj->block->level - func->level + 1 ;
                while (n-- > 0) {
                    //thisobj->slink = thisobj->slink->slink ;
                    newslink.next = newslink.next->next ;
                }
            }
            
        } else if (!isclosure && !callerIsStatic && !(func->flags & BLOCK_STATIC) && func->parentBlock == frame->block) {		// nested function?
            if (stacktop.type == T_OBJECT) {
                thisobj = stacktop.object ;		// top element on stack
            } else {
                thisobj = frame->varstack[0].object ;
            }
            newslink.next = staticLink ;
            newslink.frame = frame ;
            slink = &newslink ;
        } else if (isclosure) {
            slink = staticLink ;
        } else {
            int n = scopeStack[scopeSP-1]->majorScope->level - func->level ;	// regular function
            slink = staticLink ;
            while (n-- > 0) {
                slink = slink->next ;
            }
        }
    }
    // check that the static link is the same block as the parent block
    // of the function being called

    if (!func->isRaw() && slink != NULL) {
        if (!slink->frame->block->isSubclassOrImplements (func->parentBlock)) {
            runtimeError ("Cannot call this function, expected parent %s, got %s", func->parentBlock->name.c_str(), slink->frame->block->name.c_str()) ;
        }
    }

    if (func->isNative()) {			// calling an native function?

        StackFrame *newframe = new (func->stacksize) StackFrame (slink, frame, ir, func, func->stacksize) ;
        incRef (newframe, stackframe) ;
        NativeFunction *nfunc = static_cast<NativeFunction*>(func) ;
        bool skipthis = false ;
        if (nfunc->isRaw() && func->parentClass != NULL) {	
            nargs-- ;			// pop off the thisptr 
            sp-- ;
            skipthis = true ;
        }
        ValueVec parameters (nargs) ;
        if (!assignNativeParameters (func, newframe, nargs, !skipthis, parameters)) {
            sp -= nargs ;			// delete args from stack
            if (decRef (newframe, stackframe) == 0) {
                if (verbosegc) {
                   std::cerr << "gc: deleting stack frame " << newframe << " (" << newframe->block->name << ")\n" ;
                }
                delete newframe ;
            }
            runtimeError ("Incorrect number of parameters for call to native function %s", func->name.c_str()) ;
        } else {
            sp -= nargs ;			// delete args from stack
            if (thisobj != NULL) {
                thisobj->acquire(func->flags & BLOCK_SYNCHRONIZED) ;
                incRef (thisobj, object) ;
            }
            try {
                func->call (&retval, this, newframe, slink, scopeStack[scopeSP-1], scopeStack[scopeSP-1]->majorScope->level, parameters) ;
            } catch (...) {
                if (thisobj != NULL) {
                    thisobj->release(func->flags & BLOCK_SYNCHRONIZED) ;
                    decObjectRef (thisobj) ;
                }
                if (decRef (newframe, stackframe) == 0) {
                    if (verbosegc) {
                       std::cerr << "gc: deleting stack frame " << newframe << " (" << newframe->block->name << ")\n" ;
                    }
                    delete newframe ;
                }
                throw ;
            }

            if (thisobj != NULL) {
                thisobj->release(func->flags & BLOCK_SYNCHRONIZED) ;
                decObjectRef (thisobj) ;
            }

            if (dest != NULL) {
                set (dest, retval) ;
            }

            retval.destruct() ;
            if (decRef (newframe, stackframe) == 0) {
                if (verbosegc) {
                   std::cerr << "gc: deleting stack frame " << newframe << " (" << newframe->block->name << ")\n" ;
                }
                delete newframe ;
            }
        }
    } else {
        // make new stack frame
        StackFrame *newframe = new (func->stacksize) StackFrame (slink, frame, ir, func, func->stacksize) ;
        incRef (newframe, stackframe) ;

        // for a builtin method, duplicate the top stack entry to fake an extra parameter
        if (func->flags & BLOCK_BUILTINMETHOD) {
            stack[sp++] = stacktop ;
            nargs++ ;
        }
        if (!assignParameters (func, newframe, nargs, true)) {
            if (ir->flags & INST_PASSBYNAME) {
                sp -= (nargs - 1) * 2 + 1;
            } else {
                sp -= nargs ;			// delete args from stack
            }
            if (decRef (newframe, stackframe) == 0) {
                if (verbosegc) {
                   std::cerr << "gc: deleting stack frame " << newframe << " (" << newframe->block->name << ")\n" ;
                }
                delete newframe ;
            }
            runtimeError ("Incorrect number of parameters for call to %s", func->name.c_str()) ;
        } else {
            if (ir->flags & INST_PASSBYNAME) {
                sp -= (nargs - 1) * 2 + 1;
            } else {
                sp -= nargs ;			// delete args from stack
            }
            int nform = static_cast<InterpretedBlock*>(func)->parameters.size() ;
            if (thisobj == NULL && !(func->flags & BLOCK_BUILTINMETHOD)) {		// no this object, add one to compensate for dummy this
                nform++ ;
            }
#if 0
            int offset = nform - nargs ;
            if (offset < 0 || ir->flags & INST_PASSBYNAME) {
                offset = 0 ;
            }
#else
            int offset = 0;     // pass-by-name semantics
#endif
            if (thisobj != NULL) {
                thisobj->acquire(func->flags & BLOCK_SYNCHRONIZED) ;
                incRef (thisobj, object) ;
            }

            int currforsp = forStack.size() ;
            try {
                execute (static_cast<InterpretedBlock*>(func), newframe, slink, offset) ;
            } catch (YieldValue &y) {
                if (dest != NULL) {
                    if (thisobj != NULL) {
                        thisobj->release(func->flags & BLOCK_SYNCHRONIZED) ;
                        // don't decrement the reference count as it is held by the vm state
                    }
                    // caught for a yield opcode execution
                    // Yield object contains sp, scopesp and reg pointer.  Use these to
                    // save the current state
                    Closure *c = new Closure (T_FUNCTION, func, slink) ;
                    c->vm_state = save_state (y.pc, y.scopeSP, y.regs, forStack.size() - currforsp, newframe, thisobj, y.code) ;
                    c->value = y.value ;
                    y.value.destruct() ;
                    set (dest, c) ;
                    return ;
                } else {
                    // no destination specified, throw an exception
                    if (thisobj != NULL) {
                        thisobj->release(func->flags & BLOCK_SYNCHRONIZED) ;
                        decObjectRef (thisobj) ;
                    }
                    if (decRef (newframe, stackframe) == 0) {
                        if (verbosegc) {
                           std::cerr << "gc: deleting stack frame " << newframe << " (" << newframe->block->name << ")\n" ;
                        }
                        delete newframe ;
                    }
                    runtimeError ("Invalid yield operation - nowhere to go") ;
                }
            } catch (...) {
                if (thisobj != NULL) {
                    thisobj->release(func->flags & BLOCK_SYNCHRONIZED) ;
                    decObjectRef (thisobj) ;
                }
                if (decRef (newframe, stackframe) == 0) {
                    if (verbosegc) {
                       std::cerr << "gc: deleting stack frame " << newframe << " (" << newframe->block->name << ")\n" ;
                    }
                    delete newframe ;
                }
                throw ;
            }
            if (thisobj != NULL) {
                thisobj->release(func->flags & BLOCK_SYNCHRONIZED) ;
                decObjectRef (thisobj) ;
            }

            if (dest != NULL) {
                set (dest, retval) ;
            }

            retval.destruct() ;
            if (decRef (newframe, stackframe) == 0) {
                if (verbosegc) {
                   std::cerr << "gc: deleting stack frame " << newframe << " (" << newframe->block->name << ")\n" ;
                }
                delete newframe ;
            }
        }
    }
}

// call function with no args
void VirtualMachine::callFunction (Operand *dest, Block *func, const Value &thisptr) {
    stack[sp++] = thisptr ;
    callFunction (dest, 1, static_cast<Function*>(func)) ;
}

void VirtualMachine::callFunction (Operand *dest, Block *func, Object *thisptr) {
    stack[sp++] = thisptr ;
    callFunction (dest, 1, static_cast<Function*>(func)) ;
}


// call function with 1 arg
void VirtualMachine::callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg) {
    stack[sp++] = arg ;
    stack[sp++] = thisptr ;
    callFunction (dest, 2, static_cast<Function*>(func)) ;
}

void VirtualMachine::callFunction (Operand *dest, Block *func, StaticLink *slink, const Value &thisptr, const Value &arg) {
    StaticLink *oldslink = staticLink ;
    staticLink = slink ;

    stack[sp++] = arg ;
    stack[sp++] = thisptr ;
    try {
        callFunction (dest, 2, static_cast<Function*>(func), true) ;
    } catch (...) {
        staticLink = oldslink ;
        throw ;
    }
    staticLink = oldslink ;
}

// call function with 2 args
void VirtualMachine::callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg1, const Value &arg2) {
    stack[sp++] = arg2 ;
    stack[sp++] = arg1 ;
    stack[sp++] = thisptr ;
    callFunction (dest, 3, static_cast<Function*>(func)) ;
}

// call function with 3 args
void VirtualMachine::callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg1, const Value &arg2, 
    const Value &arg3) {
    stack[sp++] = arg3 ;
    stack[sp++] = arg2 ;
    stack[sp++] = arg1 ;
    stack[sp++] = thisptr ;
    callFunction (dest, 4, static_cast<Function*>(func)) ;
}
// call function with 4 args
void VirtualMachine::callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg1, const Value &arg2, 
    const Value &arg3, const Value &arg4) {
    stack[sp++] = arg4 ;
    stack[sp++] = arg3 ;
    stack[sp++] = arg2 ;
    stack[sp++] = arg1 ;
    stack[sp++] = thisptr ;
    callFunction (dest, 5, static_cast<Function*>(func)) ;
}

// call function with 5 args
void VirtualMachine::callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg1, const Value &arg2, 
    const Value &arg3, const Value &arg4, const Value &arg5) {
    stack[sp++] = arg5 ;
    stack[sp++] = arg4 ;
    stack[sp++] = arg3 ;
    stack[sp++] = arg2 ;
    stack[sp++] = arg1 ;
    stack[sp++] = thisptr ;
    callFunction (dest, 6, static_cast<Function*>(func)) ;
}

// call function with 6 args
void VirtualMachine::callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg1, const Value &arg2, 
    const Value &arg3, const Value &arg4, const Value &arg5, const Value &arg6) {
    stack[sp++] = arg6 ;
    stack[sp++] = arg5 ;
    stack[sp++] = arg4 ;
    stack[sp++] = arg4 ;
    stack[sp++] = arg3 ;
    stack[sp++] = arg2 ;
    stack[sp++] = arg1 ;
    stack[sp++] = thisptr ;
    callFunction (dest, 7, static_cast<Function*>(func)) ;
}

//
// call a block and return a value.  It is expected this will be invoked from
// native code that wants to call a function in the interpreter
//

Value VirtualMachine::call (Block *func, StackFrame *f, StaticLink *slink, int nargs, Value *args) {
    for (int i = nargs-1 ; i >= 0 ; i--) {
        stack[sp++] = args[i] ;
    }
    CodeSequence *oldcode = currentCode ;
    Instruction *oldir = ir ;
    StackFrame *oldframe ;
    StaticLink *oldstaticlink ;
    oldframe = frame ;
    oldstaticlink = staticLink ;
    frame = f ;
    staticLink = slink ;

    InterpretedBlock *iblock = static_cast<InterpretedBlock*>(func) ;

    currentCode = iblock->code ;
    ir = &currentCode->code[0] ;
    Operand result ;

    try {
        callFunction (&result, nargs, iblock) ;
    } catch (...) {
        frame = oldframe ;
        staticLink = oldstaticlink ;
        currentCode = oldcode ;
        ir = oldir ;
        throw ;
    }
    frame = oldframe ;
    staticLink = oldstaticlink ;
    currentCode = oldcode ;
    ir = oldir ;
    return result.val ;
}

Value VirtualMachine::newObject (Block *block, StackFrame *f, StaticLink *slink, std::vector<Value> &args) {
    for (int i = args.size()-1 ; i >= 0 ; i--) {
        stack[sp++] = args[i] ;
    }
    CodeSequence *oldcode = currentCode ;
    Instruction *oldir = ir ;
    StackFrame *oldframe ;
    StaticLink *oldstaticlink ;
    oldframe = frame ;
    oldstaticlink = staticLink ;
    frame = f ;
    staticLink = slink ;

    InterpretedBlock *iblock = static_cast<InterpretedBlock*>(block) ;

    currentCode = iblock->code ;
    ir = &currentCode->code[0] ;
    Operand result ;

    // need to push a scope
    scopeStack[scopeSP++] = block;
    try {
        newObject (&result, args.size(), iblock->type, iblock, false) ;
    } catch (...) {
        frame = oldframe ;
        staticLink = oldstaticlink ;
        currentCode = oldcode ;
        ir = oldir ;
        scopeSP--;
        throw ;
    }
    frame = oldframe ;
    staticLink = oldstaticlink ;
    currentCode = oldcode ;
    ir = oldir ;
    scopeSP--;
    return result.val ;
}


// make a call to a function from outside the normal execution environment
void VirtualMachine::randomCall (Operand *dest, Block *func, StackFrame *f, StaticLink *slink,
        const Value &thisptr, const Value &arg, bool isclosure) {
    stack[sp++] = arg ;
    stack[sp++] = thisptr ;
    CodeSequence *oldcode = currentCode ;
    Instruction *oldir = ir ;
    StackFrame *oldframe ;
    StaticLink *oldstaticlink ;
    oldframe = frame ;
    oldstaticlink = staticLink ;
    frame = f ;
    staticLink = slink ;

    InterpretedBlock *iblock = static_cast<InterpretedBlock*>(func) ;

    currentCode = iblock->code ;
    ir = &currentCode->code[0] ;
    try {
        callFunction (dest, 2, iblock, isclosure) ;
    } catch (...) {
        frame = oldframe ;
        staticLink = oldstaticlink ;
        currentCode = oldcode ;
        ir = oldir ;
        throw ;
    }
    frame = oldframe ;
    staticLink = oldstaticlink ;
    currentCode = oldcode ;
    ir = oldir ;
}

void VirtualMachine::randomCall (Operand *dest, Block *func, StackFrame *f, StaticLink *slink,
        const Value &thisptr, const Value &arg1, const Value &arg2, bool isclosure) {
    stack[sp++] = arg2 ;
    stack[sp++] = arg1 ;
    stack[sp++] = thisptr ;
    CodeSequence *oldcode = currentCode ;
    Instruction *oldir = ir ;
    StackFrame *oldframe ;
    StaticLink *oldstaticlink ;
    oldframe = frame ;
    oldstaticlink = staticLink ;
    frame = f ;
    staticLink = slink ;

    InterpretedBlock *iblock = static_cast<InterpretedBlock*>(func) ;

    currentCode = iblock->code ;
    ir = &currentCode->code[0] ;
    try {
        callFunction (dest, 3, iblock, isclosure) ;
    } catch (...) {
        frame = oldframe ;
        staticLink = oldstaticlink ;
        currentCode = oldcode ;
        ir = oldir ;
        throw ;
    }
    frame = oldframe ;
    staticLink = oldstaticlink ;
    currentCode = oldcode ;
    ir = oldir ;
}

void VirtualMachine::randomCall (Operand *dest, Block *func, StackFrame *f, StaticLink *slink,
        const Value &thisptr, const std::vector<Value> &args, bool isclosure) {
    for (int i = args.size() - 1 ; i >= 0 ; i--) {
        stack[sp++] = args[i] ;
    }
    stack[sp++] = thisptr ;
    CodeSequence *oldcode = currentCode ;
    Instruction *oldir = ir ;
    StackFrame *oldframe ;
    StaticLink *oldstaticlink ;
    oldframe = frame ;
    oldstaticlink = staticLink ;
    frame = f ;
    staticLink = slink ;

    InterpretedBlock *iblock = static_cast<InterpretedBlock*>(func) ;

    currentCode = iblock->code ;
    ir = &currentCode->code[0] ;
    try {
        callFunction (dest, args.size() + 1, iblock, isclosure) ;
    } catch (...) {
        frame = oldframe ;
        staticLink = oldstaticlink ;
        currentCode = oldcode ;
        ir = oldir ;
        throw ;
    }
    frame = oldframe ;
    staticLink = oldstaticlink ;
    currentCode = oldcode ;
    ir = oldir ;
}

void VirtualMachine::randomCall (Operand *dest, Block *func, StackFrame *f, StaticLink *slink, bool isclosure) {
    CodeSequence *oldcode = currentCode ;
    Instruction *oldir = ir ;
    StackFrame *oldframe ;
    StaticLink *oldstaticlink ;
    oldframe = frame ;
    oldstaticlink = staticLink ;
    frame = f ;
    staticLink = slink ;

    InterpretedBlock *iblock = static_cast<InterpretedBlock*>(func) ;

    currentCode = iblock->code ;
    ir = &currentCode->code[0] ;
    try {
        callFunction (dest, 0, iblock, isclosure) ;
    } catch (...) {
        frame = oldframe ;
        staticLink = oldstaticlink ;
        currentCode = oldcode ;
        ir = oldir ;
        throw ;
    }
    frame = oldframe ;
    staticLink = oldstaticlink ;
    currentCode = oldcode ;
    ir = oldir ;
}

void VirtualMachine::randomCall (Operand *dest, Block *func, Object *thisptr, StackFrame *f, StaticLink *slink, bool isclosure) {
    CodeSequence *oldcode = currentCode ;
    Instruction *oldir = ir ;
    StackFrame *oldframe ;
    StaticLink *oldstaticlink ;
    oldframe = frame ;
    oldstaticlink = staticLink ;
    frame = f ;
    staticLink = slink ;

    InterpretedBlock *iblock = static_cast<InterpretedBlock*>(func) ;

    currentCode = iblock->code ;
    ir = &currentCode->code[0] ;
    try {
        stack[sp++] = thisptr ;
        callFunction (dest, 1, static_cast<Function*>(iblock), isclosure) ;
    } catch (...) {
        frame = oldframe ;
        staticLink = oldstaticlink ;
        currentCode = oldcode ;
        ir = oldir ;
        throw ;
    }
    frame = oldframe ;
    staticLink = oldstaticlink ;
    currentCode = oldcode ;
    ir = oldir ;
}

// holder for information passed to thread

struct ThreadParas {
    Thread *thread ;
    StackFrame *stack ;
    StaticLink staticLink ;
    Aikido *p ;
    int offset ;
    StaticLink *threadslink ;
    ThreadStream *mystream ;
} ;

extern "C" void *execThread (void *arg) {
    ThreadParas *a = (ThreadParas *)arg ;
    Thread *thread = a->thread ;
   // std::cout << "starting thread " << thread->name << '\n' ;
    VirtualMachine vm (a->p) ;
    try {
        vm.execute (thread, a->stack, &a->staticLink, a->offset) ;

        if (thread->finalize != NULL) {
            Value r ;
            vm.callFunction (NULL, thread->finalize, r) ;
        }
    } catch (RerunException e) {
        // do nothing, let the code below free the thread stuff
    } catch (aikido::Exception ex) {
        a->p->reportException (ex) ;
        //std::cerr << "Runtime Error: " << e.value << '\n' ;
        ::exit (1) ;
    }
    if (decRef (a->stack, stackframe) == 0) {
        if (verbosegc) {
           std::cerr << "gc: deleting stack frame " << a->stack << "\n" ;
        }
        delete a->stack ;
    }
    if (decRef (a->mystream, stream) == 0) {
        if (verbosegc) {
           std::cerr << "gc: deleting thread stream " << a->mystream << "\n" ;
        }
        delete a->mystream ;
    }
    OSThread_t id = OSThread::self() ;
    a->p->deleteThread (id) ;
    delete a ;
    return NULL ;
}

void VirtualMachine::callThread (Operand *dest, int nargs, Function *func) {
    Object *thisobj = NULL ;
    StaticLink *slink = staticLink ;
    Value &stacktop = stack[sp-1] ;
    StaticLink newslink ;

    if (!(func->flags & BLOCK_STATIC) && func->parentClass != NULL) {
        if (stacktop.type == T_OBJECT) {
            thisobj = stacktop.object ;		// top element on stack
        } else {
            thisobj = frame->varstack[0].object ;
        }
        newslink.next = &thisobj->objslink ;
        newslink.frame = thisobj ;
        slink = &newslink ;
        if (frame->varstack[0].type == T_OBJECT && frame->varstack[0].object == thisobj) {      
            // although the thread is in the same class, it may be at a lower static level
            int n = scopeStack[scopeSP-1]->majorScope->level - func->level ;	
            if (n > 0) {
                runtimeError ("Cannot start a thread at a lower static level in the same object") ;
            }
        }
    } else if (!(func->flags & BLOCK_STATIC) && func->parentBlock == frame->block) {		// nested function?
        if (stacktop.type == T_OBJECT) {
            thisobj = stacktop.object ;		// top element on stack
        } else {
            thisobj = frame->varstack[0].object ;
        }
        newslink.next = staticLink ;
        newslink.frame = frame ;
        slink = &newslink ;
    } else {
        int n = scopeStack[scopeSP-1]->majorScope->level - func->level ;	// regular function
        slink = staticLink ;
        while (n-- > 0) {
            slink = slink->next ;
        }
    }
    // make new stack frame
    StackFrame *newframe = new (func->stacksize) StackFrame (slink, NULL, ir, func, func->stacksize) ;
    incRef (newframe, stackframe) ;
    if (!assignParameters (func, newframe, nargs, true)) {
        if (decRef (newframe, stackframe) == 0) {
            if (verbosegc) {
               std::cerr << "gc: deleting stack frame " << newframe << "\n" ;
            }
            delete newframe ;
        }
        if (ir->flags & INST_PASSBYNAME) {
            sp -= (nargs - 1) * 2 + 1;
        } else {
            sp -= nargs ;			// delete args from stack
        }
        runtimeError ("Incorrect number of parameters for thread %s", func->name.c_str()) ;
    } else {
        if (ir->flags & INST_PASSBYNAME) {
            sp -= (nargs - 1) * 2 + 1;
        } else {
            sp -= nargs ;			// delete args from stack
        }
        int nform = static_cast<InterpretedBlock*>(func)->parameters.size() ;
        if (thisobj == NULL) {		// no this object, add one to compensate for dummy this
            nform++ ;
        }
#if 0
        int offset = nform - nargs ;
        if (offset < 0) {
            offset = 0 ;
        }
#else
        int offset = 0;
#endif

        Thread *thread = static_cast<Thread*>(func) ;

        ThreadParas *arg = new ThreadParas ;
        arg->thread = thread ;
        arg->stack = newframe ;
        arg->staticLink = *slink ;
        arg->offset = offset ;
      
        arg->p = aikido ;

        ThreadStream *threadStream = new ThreadStream (aikido, NULL, thread, newframe) ;
        ThreadStream *callerStream = new ThreadStream (aikido, threadStream, NULL, NULL) ;
        threadStream->peer = callerStream ;
        incRef (threadStream, stream) ;
        callerStream->ref++ ;
        arg->mystream = threadStream ;
        OSThread *t ;
        
        // failure to create the thread results in an exception.  Make sure to tidy up before issuing
        // the error
        try {
            t = new OSThread (execThread, arg) ;
        } catch (const char *s) {
            delete threadStream ;
            delete callerStream ;
            delete arg ;
            if (decRef (newframe, stackframe) == 0) {
                if (verbosegc) {
                   std::cerr << "gc: deleting stack frame " << newframe << "\n" ;
                }
                delete newframe ;
            }

            runtimeError (s) ;
        }
        
        threadStream->setThreadID (t->id) ;
        callerStream->setThreadID (t->id) ;
        aikido->insertThread (t->id, t) ;

        set (dest, (Stream*)callerStream) ;
        callerStream->ref-- ;
    }
}

inline Value &getarg (Value &arg) {
    if (arg.type == T_ADDRESS) {
        return *arg.addr ;
    } else {
        return arg ;
    }
}

// check and assign parameters:
// case 1: more actuals than formals - rest put in varargs if present
// case 2: more formals than actuals - if default for next formal then ok, otherwise error

// also handle pass-by-name calls.  This has a flag in the instruction (INST_PASSBYNAME).  There are twice the 
// number of values on the stack.  After the 'this' value, there is a set of strings, one per actual that are
// the names of the actuals.  Then the real actuals follow.
//
// Have to be careful to pop the correct number of args off the stack in the caller

bool VirtualMachine::assignParameters (Function *func, StackFrame *newframe, int nactuals, bool thispresent, int ntypes) {
    InterpretedBlock *ifunc ;
    int nformals ;
    bool varargs = false ;

    ifunc = (InterpretedBlock*)func ;
    nformals = ifunc->parameters.size() ;
    varargs = (func->flags & BLOCK_VARARGS) != 0 ;

    bool passbyname = ir->flags & INST_PASSBYNAME;

    Value *actuals = &stack[sp - 1] ;			// top value on the stack
    int form = 0 ;
    int act = 0 ;
    int num_names = nactuals;

    if (thispresent) {
        if (!(func->flags & BLOCK_STATIC) && func->parentClass != NULL) {
            Value *thisptr = actuals ;
            //if (thisptr->type != T_OBJECT) {
                //thisptr = &frame->varstack[0] ;
            //}
            ifunc->parameters[0]->setValue(*thisptr, newframe) ;
            form++ ;
        }
        actuals-- ;
        act++ ;
        num_names--;
    }

    if (ntypes > 0) {
        // there are types, assign them
        if (ntypes != ifunc->types.size()) {
            runtimeError ("Incorrect number of types passed to constructor for %s (expected %d, got %d)", func->name.c_str(), ifunc->types.size(), ntypes);
        }
        int t = 0;
        while (ntypes > 0) {
            Value &val = *actuals ;
            ifunc->types[t]->setValue(val, newframe) ;
            actuals--;
            ntypes--;
            t++;
        }

        // the 'actuals' pointer has been increased by ntypes
    }

    if (passbyname) {
        // collect the actual names (names of the formal args)
        typedef std::map<std::string, Value> ActualMap;
        ActualMap actualmap;

        std::vector<std::string> names;
        for (int i = 0; i < num_names ; i++) {
            Value &val = *actuals--;
            names.push_back (val.str->str);
        }
        // now collect the actual values
        for (int i = 0; i < num_names ; i++) {
            Value &val = *actuals--;
            actualmap[names[i]] = val;
        }
       
        typedef std::map<std::string, bool> FoundActuals;
        FoundActuals found_actuals;

        std::vector<Parameter*> missing_formals;

        while (form < nformals) {
            Parameter *para = static_cast<Parameter*>(ifunc->parameters[form]) ;
            // find the actual value based on formal name
            ActualMap::iterator actit = actualmap.find (para->name.str);
            if (actit != actualmap.end()) {
                found_actuals[actit->first] = true;
                Value &val = actit->second;
                if (para->isReference()) {
                    if (val.type != T_ADDRESS) {
                        runtimeError ("Invalid actual parameter for reference parameter %s", para->name.c_str()) ;
                    } else {
                        Reference *ref = static_cast<Reference*>(para) ;
                        ref->setReference (val.addr, newframe) ;
                    }
                } else {
                    para->setValue (getarg (val), newframe) ;
                }
            } else {
                missing_formals.push_back (para);
            }
            form++;
        }

        // check if we found all the formal parameters (too many actuals or mismatched name)
        std::vector<std::string> bad_actuals;
        for (ActualMap::iterator i = actualmap.begin() ; i != actualmap.end() ; i++) {
            FoundActuals::iterator f = found_actuals.find (i->first);
            if (f == found_actuals.end()) {
                bad_actuals.push_back (i->first);
            }
        }
        if (bad_actuals.size() > 0) {
            bool comma = false;
            std::string s;
            int n = 0;
            for (unsigned int i = 0 ; i < bad_actuals.size() ; i++) {
               if (comma) s += ", ";
               s += bad_actuals[i];
               comma = true;
               n++;
            }
            runtimeError ("The parameter%s '%s' %s not present in %s", n==1?"":"s", s.c_str(), n==1?"is":"are", func->name.c_str());
        }
        // assign all the missing formals with the value none
        for (unsigned int i = 0 ; i < missing_formals.size() ; i++) {
            Parameter *para = missing_formals[i];
            para->setValue (Value(), newframe);
        }
        return true;
    }

    // too many actuals and no varargs?
    if (!varargs && (nactuals - act) > nformals) {
        return false ;
    }

    while (form < nformals && act < nactuals) {
        Value &val = *actuals ;
        Parameter *para = static_cast<Parameter*>(ifunc->parameters[form]) ;
        if (para->isReference()) {
            if (val.type != T_ADDRESS) {
printf ("Invalid actual parameter for reference parameter\n");
                runtimeError ("Invalid actual parameter for reference parameter %s", para->name.c_str()) ;
            } else {
                Reference *ref = static_cast<Reference*>(para) ;
                ref->setReference (val.addr, newframe) ;
            }
        } else {
            para->setValue (getarg (val), newframe) ;
        }
        act++ ;
        form++ ;
        actuals-- ;
    }

    Value::vector *argsvec = NULL ;

    // always need the args vector set up for varargs blocks
    if (varargs) {
        argsvec = new Value::vector ;
        func->varargs->setValue (Value (argsvec), newframe) ;
    }

    if (act < nactuals) {		// too many actual values?
        if (varargs) {
            while (act < nactuals) {
                Value &val = *actuals ;
                argsvec->push_back (getarg (val)) ;
                act++ ;
                actuals-- ;
            }
        } else {
            return false ;
        }
    } else if (form < nformals) {		// more formal than actuals?
        Parameter *para = static_cast<Parameter*>(ifunc->parameters[form]) ;
        if (para->def == NULL) {
            return false ;
        }
    }
    return true ;
}

//
// assign the parameters of a native call
//

bool VirtualMachine::assignNativeParameters (Function *func, StackFrame *newframe, int nactuals, bool thispresent, ValueVec &parameters) {
    NativeFunction *nfunc ;
    int nformals ;
    bool varargs = false ;

    nfunc = (NativeFunction*)func ;
    nformals = nfunc->nparas ;
    if (nformals < 0) {
        nformals = -nformals ;
        varargs = true ;
    }
    if (nfunc->isRaw() && func->parentClass != NULL) {
        nformals-- ;
    }
    Value *actuals = &stack[sp - 1] ;			// top value on the stack

    int form = 0 ;
    int act = 0 ;
    if (thispresent) {
        if (!(func->flags & BLOCK_STATIC) && func->parentClass != NULL) {
            Value *thisptr = actuals ;
            //if (thisptr->type != T_OBJECT) {
                //thisptr = &frame->varstack[0] ;
            //}
            parameters[0] = *thisptr ;
            form++ ;
        }
        actuals-- ;
        act++ ;
    }

    // too many actuals and no varargs?
    if (!varargs && (nactuals - act) > nformals) {
        return false ;
    }

    while (form < nformals && act < nactuals) {
        Value &val = *actuals ;
        if (nfunc->paramask & (1 << form)) {		// reference parameter?
            parameters[form] = val ;		// assign address
        } else {
            parameters[form] = getarg (val) ;
        }
        act++ ;
        form++ ;
        actuals-- ;
    }

    if (act < nactuals) {		// too many actual values?
        if (varargs) {
            bool isref = nfunc->paramask & (1 << form) ;
            while (act < nactuals) {
                Value &val = *actuals ;
                if (isref) {		// reference parameter?
                    parameters[form++] = val ;
                } else {
                    parameters[form++] = getarg (val) ;
                }
                act++ ;
                actuals-- ;
            }
        } else {
            return false ;
        }
    } else if (form < nformals) {		// more formal than actuals?
        return false ;				// no defaults for native
    }
    return true ;
}



//
// allocate a new object and set its address in *dest
//
void VirtualMachine::newObject (Operand *dest, int nargs, Type type, InterpretedBlock *block, bool thispresent, int ntypes) {
    if (block->isAbstract()) {
#if defined(_CC_GCC) && __GNUC__ == 2
        std::strstream info ;
#else
        std::stringstream info ;
#endif
        block->printUndefinedMembers (info) ;
#if defined(_CC_GCC) && __GNUC__ == 2
        runtimeError ("Cannot create instance of %s, is it abstract\n%s", block->name.c_str(), info.str()) ;
#else
        runtimeError ("Cannot create instance of %s, is it abstract\n%s", block->name.c_str(), info.str().c_str()) ;
#endif

    }
#ifdef DEBUGON
    DebugState oldstate = debugState ;
    if (debugState == NEXT || debugState == NEXTI) {
        debugState = NEXTRUN ;
    }
#endif

    Object *obj ;
    StaticLink *slink = staticLink ;
    StaticLink newslink ;

    Value &stacktop = stack[sp-1] ;
    InterpretedBlock *caller = (InterpretedBlock*)frame->block ;
    bool callerIsStatic = caller->flags & BLOCK_STATIC ;

    if (!callerIsStatic && thispresent && !(block->flags & BLOCK_STATIC) && block->parentBlock == frame->block) {		// nested class?
        newslink.frame = frame ;
        newslink.next = staticLink ;
        slink = &newslink ;
    } else if (!callerIsStatic && thispresent && !(block->flags & BLOCK_STATIC) && block->parentClass != NULL) {
        if (stacktop.type == T_OBJECT) {
            newslink.next = &stacktop.object->objslink ;
            newslink.frame = stacktop.object ;
            slink = &newslink ;
        } else {
            newslink.next = &frame->varstack[0].object->objslink ;
            newslink.frame = frame->varstack[0].object ;
            slink = &newslink ;
        }

        if (frame->varstack[0].type == T_OBJECT && frame->varstack[0].object == slink->frame) {      
            // although the object is in the same class, it may be at a lower static level
            int n = slink->frame->block->level - block->level + 1 ;
            while (n-- > 0) {
                slink->next = slink->next->next ;
            }
        }
    } else {
        int n = scopeStack[scopeSP-1]->majorScope->level - block->level ;	// regular class
        slink = staticLink ;
        while (n-- > 0) {
            if (slink->next == NULL) {
                break;
            }
            slink = slink->next ;
        }
    }
//    printStaticLinks (slink);

    if (type == T_MONITOR) {	
       obj = (Object *)new (block->stacksize) Monitor (block, slink, frame, ir) ;    // allocate the object
    } else {
         obj = new (block->stacksize) Object (block, slink, frame, ir) ;       // allocate the object
    }
    obj->ref++ ;			// fool the gc
    obj->varstack[0] = obj ;           // set the node ptr in the first memory location

    // remove the this parameter
    if (thispresent) {
        sp-- ;
        nargs-- ;
    }
    if (!assignParameters (block, obj, nargs, false, ntypes)) {		
        if (ir->flags & INST_PASSBYNAME) {
            sp -= ntypes + nargs * 2;
        } else {
            sp -= ntypes + nargs ;			// delete args from stack
        }
        delete obj ;

        runtimeError ("Incorrect number of parameters for constructor call for %s", block->name.c_str()) ;
    } else {
        if (ir->flags & INST_PASSBYNAME) {
            sp -= ntypes + nargs * 2;
        } else {
            sp -= ntypes + nargs ;			// delete args from stack
        }
#if 0
        int offset = block->parameters.size() - nargs ;
        if (offset < 0 || ir->flags & INST_PASSBYNAME) {
            offset = 0 ;
        }
#else
        int offset = 0;
#endif
        obj->acquire(block->flags & BLOCK_SYNCHRONIZED) ;

        try {
            // execute the object's constructor.  We want the static
            // link for anything called by the constructor to be
            // the object itself.  We cannot pass 'slink' here as
            // it is on the stack and the object lives longer than
            // this call
            execute (block, obj, &obj->objslink, offset) ;		        
        } catch (...) {
            obj->release(block->flags & BLOCK_SYNCHRONIZED) ;
            decRef (obj, object) ;
            throw ;
        }

        obj->release(block->flags & BLOCK_SYNCHRONIZED) ;
    }
    set (dest, obj) ;
#ifdef DEBUGON
    if (debugState == NEXTRUN) {
        debugState = oldstate ;
    }
#endif
    decRef (obj, object) ;

}

//
// raise a runtime error exception
//

void VirtualMachine::runtimeError (SourceLocation *source, const char *s,...) {
    char str[1024] ;
    va_list arg ;
    va_start (arg, s) ;
    vsprintf (str, s, arg) ;
    aikido->numRuntimeErrors++ ;
#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream error ;
#else
    std::stringstream error ;
#endif
    error << '\"' << source->filename << "\", line " << source->lineno << ": " << str ;
#ifdef DEBUGON
    if (aikido->properties & DEBUG && aikido->debugger->stopThrow()) {
        std::cout << "stopped on exception: " << error.str() << "\n" ;
        aikido->debugger->run (this, ir, scopeStack, scopeSP, frame, staticLink) ;

    }
    if (debugState == RERUN) {
        throw RerunException() ;
    }
#endif
    throw Exception (string (error.str()), frame, staticLink, scopeStack[scopeSP-1], scopeStack[scopeSP-1]->majorScope->level, ir->source, frame->block) ;
}

void VirtualMachine::runtimeError (const char *s,...) {
    char str[1024] ;
    va_list arg ;
    va_start (arg, s) ;
    vsprintf (str, s, arg) ;
    aikido->numRuntimeErrors++ ;
#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream error ;
#else
    std::stringstream error ;
#endif
    error << '\"' << ir->source->filename << "\", line " << ir->source->lineno << ": " << str ;
#ifdef DEBUGON
    if (aikido->properties & DEBUG && aikido->debugger->stopThrow()) {
        std::cout << "stopped on exception: " << error.str() << "\n" ;
        aikido->debugger->run (this, ir, scopeStack, scopeSP, frame, staticLink) ;

    }
    if (debugState == RERUN) {
        throw RerunException() ;
    }
#endif
    throw Exception (string (error.str()), frame, staticLink, scopeStack[scopeSP-1], scopeStack[scopeSP-1]->majorScope->level, ir->source, frame->block) ;
}


// construct a superblock object.  Parameters are on stack
//
// If the superblock is up the static chain we need to adjust the static link
// so that the code executed has the correct parent


bool VirtualMachine::supercall (Operand *n, Operand *f, Operand *t) {
    int nargs = n->val.integer ;		// guarnteed to be constant
    int ntypes = t == NULL ? 0 : t->val.integer;
    Value *val ;
    int level = 0 ;
    // check for old value override
    if (f->type == tLOCALVAR || f->type == tVARIABLE) {
        Variable *var = static_cast<Variable*>(f->val.ptr) ;
        if (var->flags & VAR_OLDVALUE) {
            val = &var->oldValue ;
        } else {
            level = (int)f->val.type - 100 ;
            StackFrame *stk = f->type == tLOCALVAR ? frame : getStack (level) ;
            val = var->getAddress (stk) ;
        }
    } else {
        val = &get (f) ;
    }

    if (!(val->type == T_CLASS || val->type == T_MONITOR ||
          val->type == T_FUNCTION || val->type == T_THREAD || val->type == T_PACKAGE)) {
        runtimeError ("Cannot construct superblock object, it is not a block") ;
        sp -= nargs ;			// delete args from stack
    }
       
    InterpretedBlock *block = static_cast<InterpretedBlock*>(val->cls) ;

    // adust the static link to that of the destination
    int mylevel = frame->block->level ;
    int hislevel = block->level ;

    StaticLink *slink = staticLink ;
    int nlevels = mylevel - hislevel ;
    while (slink != NULL && nlevels-- > 0) {
        slink = slink->next ;
    }


    if (!assignParameters (block, frame, nargs, false, ntypes)) {
        sp -= ntypes + nargs ;			// delete args from stack
        runtimeError ("Incorrect number of parameters for superconstructor call for %s", block->name.c_str()) ;
    } else {
        sp -= ntypes + nargs ;			// delete args from stack
#if 0
        int offset = block->parameters.size() - nargs ;
        if (offset < 0) {
            offset = 0 ;
        }
#else
        int offset = 0;
#endif
        return execute (block, frame, slink, offset) ;
    }
}

// the src operand is an expression value that is a potential source of a this

Object *VirtualMachine::getthis (Operand *src) {
    Value &val = get (src) ;
    Object *res = NULL ;

    if (val.type == T_FUNCTION || val.type == T_THREAD || val.type == T_CLASS || val.type == T_PACKAGE || val.type == T_MONITOR) {
        if (val.type == T_PACKAGE) {		// XXX: I don't think this will evern happen now
            res = val.package->object ;
            if (res != NULL) {
                incRef (res, pobject) ;
            }
        } else {
            Function *func = val.func ;
            if (func->parentClass != NULL) {
                if (src->type == tVARIABLE) {
                    Value &v = getStack ((int)src->val.type - 100)->varstack[0] ;
                    //if (v.type == T_OBJECT) {
                        res = v.object ;
                    //}
                } else if (src->type == tLOCALVAR) {
                    Value &v = frame->varstack[0] ;
                    //if (v.type == T_OBJECT) {
                        res = v.object ;
                    //}
                }
                //if (res != NULL) {
                    //incRef (res, pobject) ;
                //}
            }
        }
    } else if (val.type == T_OBJECT) {
        res = val.object ;
    }
    return res ;
}


// find a member in a block using a cache

Variable *VirtualMachine::findMember (Value &val, Block *block, StackFrame *stk, bool directParent) {
    Variable *mem ;
    Scope *scope = NULL ;
    VariableName *var = (VariableName*)val.ptr ;
    int access = VAR_PUBLIC ;
    if (directParent || block == stk->block) {
        access = VAR_ACCESSFULL ;
    }
    mem = var->lookup (block, ir->source, access) ;				// fast static lookup
    if (mem == NULL) {                                          // not found, look it up
        mem = block->findVariable (var->name, scope, access, ir->source, NULL) ;
    }
    return mem ;
}

//
// has the object an operator function of the given type?   The value left must be an object
//

InterpretedBlock *VirtualMachine::checkForOperator (const Value &left, Token tok) {
    if (left.type == T_OBJECT) {
        if (left.object->block->flags & BLOCK_HASOPERATORS) {
            Class *cls = (Class *)left.object->block ;
            return cls->findOperator (tok) ;
        }
    } else if (left.type == T_CLASS || left.type == T_MONITOR) {
        if (left.cls->flags & BLOCK_HASOPERATORS) {
            Class *cls = (Class *)left.cls ;
            return cls->findOperator (tok) ;
        }
    }
    return NULL ;
}

InterpretedBlock *Class::findOperator (Token tok) {
    OperatorMap::iterator result = operators.find (tok) ;
    if (result == operators.end()) {
        return NULL ;
    }
    return (*result).second ;
}



//
// start an exception handler block
//
void VirtualMachine::dotry (Operand *catchop, Operand *endop, Operand *finallyop) {
    int oldscopeSP = scopeSP ;		// size of scope stack
    int forstackSize = forStack.size() ;
    try {
        execute (pc) ;
        if (finallyop != NULL) {
            // execute the finally
            execute (finallyop->val.integer);
        }
        pc = endop->val.integer ;			// jump to after handler
    } catch (Exception e) {
        exception = e.value ;			// store for CATCH to pick up
        scopeSP = oldscopeSP ;
        while (forStack.size() > forstackSize) {
            delete forStack.top() ;
            forStack.pop() ;
        }
        if (finallyop != NULL) {
            // execute catch code
            try {
                execute (catchop->val.integer);
            } catch (Exception e) {
                execute (finallyop->val.integer);
                throw e;
            }

            // execute the finally
            execute (finallyop->val.integer);
            pc = endop->val.integer ;			// jump to after handler
        } else {
            pc = catchop->val.integer ;		// jump to handler
        }
    } catch (int i) {				// this happens when the POPSCOPE is executed
	int savedpc = pc;
        if (finallyop != NULL) {
            execute (finallyop->val.integer);
        }
        i-- ;
        if (i > 0) {
            pc = savedpc;
            throw i ;
        }
        pc = savedpc;
    } catch (...) {
        // other exception
        scopeSP = oldscopeSP ;
        while (forStack.size() > forstackSize) {
            delete forStack.top() ;
            forStack.pop() ;
        }
        throw ;
    }
}


//
// make a vector with given number of elements.  The values to be placed in
// the vector are on the stack in reverse order
//

void VirtualMachine::makeVector (Operand *dest, Operand *n) {
    int nelems = n->val.integer ;
    Value *elems = &stack[sp - 1] ;
    sp -= nelems ;
    Value::vector *vec = new Value::vector (nelems) ;
    vec->clear() ;
    while (nelems > 0) {
        vec->push_back (*elems) ;
        elems->destruct() ;
        nelems-- ;
        elems-- ;
    }
    set (dest, vec) ;
}

//
// make a new map.  The values to be inserted are on the stack in
// reverse order
//

void VirtualMachine::makeMap (Operand *dest, Operand *n) {
    int nelems = n->val.integer ;
    Value *elems = &stack[sp - 1] ;
    sp -= nelems * 2 ;
    map *m = new map ;
    while (nelems > 0) {
        Value &key = *elems ;
        Value &val = *(elems - 1) ;
        m->insert (key, val) ;
        key.destruct() ;
        val.destruct() ;
        nelems-- ;
        elems -= 2 ;
    }
    set (dest, m) ;
}



/*
 * this is the -> operator.  The left is copied to the right and conversions are performed 
 * according to the following table:
 *
 * Output type: integer
 *	Input type:
 *		integer		no change
 *		string		atoi conversion is possible, 0 otherwise
 *		vector:		first element converted to integer
 *		map: 		first element converted to integer
 * 		char:		converted to integer
 *		function:	integer is set to address
 * 		thread:		integer is set to address
 *		class:		integer is set to address
 *		package:	integer is set to address
 * 		enum:		integer is set to address
 *		enumconst:	index into enum
 * 		object:		toInteger() method called if present, otherwise address of object
 *		stream:		one integer read from stream
 *		
 * Output type: string
 *	Input type: 
 *		integer		converted to string
 *		string		no change
 *		vector:		each element appended to string
 *		map: 		each element appended to string
 * 		char:		converted to string
 *		function:	name
 * 		thread:		name
 *		class:		name
 *		package:	name
 * 		enum:		name
 *		enumconst:	name of const
 * 		object:		toString method called, or "<blockname>@address"
 *		stream:		one line read from stream
 *		
 * Output type: char
 *	Input type: 
 *		integer		truncated to char
 *		string		first char in string
 *		vector:		first element converted to char
 *		map: 		first element converted to char
 * 		char:		no change to string
 *		function:	first char of name
 * 		thread:		first char of name
 *		class:		first char of name
 *		package:	first char of name
 * 		enum:		first char of name
 *		enumconst:	'A' = first const, 'B' = second, etc
 * 		object:		toChar method called, or runtime error
 *		stream:		one char read from stream
 *		
 * Output type: vector
 * 	All input types are appended to the vector except:
 * 		object: toVector() method called is present, otherwise it is appended
 *
 * Output type: map
 * 	All input types are appended to the map as {x = x}.  Object as in vector, except function
 *	is toMap()
 *
 * Output type: function
 *	All single value inputs cause function to be called with a single parameter.  Multivalue
 *	inputs (vector and map) cause function to be called multiple times.  For vector, one
 *	parameter is passed, for map, 2 are passed
 *
 * Output type: thread
 *	Just like function
 *
 * Output type: class
 *	Just like function, except new object is created for each
 *
 * Output type: package
 *	Just like function, except new object is created for each
 *
 * Output type: enum
 *	runtime error
 *
 * Output type: enumconst
 *	runtime error
 *
 * Output type: object
 *	runtime error
 *
 * Output type: stream
 *	input type is written to the stream
 *
 * 
 *		
 */

void VirtualMachine::stream (Operand *dest, Value &left, Value &rightaddr) {
    InterpretedBlock *opfunc ;
    Value &right = *rightaddr.addr ;
    Value *destaddr = rightaddr.addr ;

    if (left.type == T_OBJECT && left.object != NULL && (opfunc = checkForOperator (left, ARROW)) != NULL) {
        callFunction (dest, opfunc, left, right, false) ;
    } else if (right.type == T_OBJECT && right.object != NULL && (opfunc = checkForOperator (right, ARROW)) != NULL) {
        callFunction (dest, opfunc, right, left, true) ;
    } else if (left.type == T_OBJECT && left.object != NULL && right.type == T_STREAM) {
        streamObject (dest, left, right) ;
    } else {
        streamCopy (dest, left, right) ;
        Value &v = get(dest) ;
        if (v.type == T_INTEGER || v.type == T_CHAR || v.type == T_REAL || v.type == T_BYTE) {
            *destaddr = right ;
        }
    }
}

// write an object that doesn't contain an overload for -> to a stream.  The object is
// written as a series of name, value pairs
void VirtualMachine::streamObject (Operand *dest, Value &left, Value &right) {
}

void VirtualMachine::streamCopy (Operand *dest, Value &left, Value &right) {

    // complexity here:  if left is none and right is a stream (or none) then this is an output operation of the value none
    if (left.type == T_STREAM || (left.type == T_NONE && right.type != T_STREAM && right.type != T_NONE)) {           // input from stream (or default input)
        Stream *stream = left.type == T_NONE ? input.stream : left.stream ;
        Block *block = right.block ;
       
        switch (right.type) {
        case T_CLOSURE:
            block = right.closure->block ;
            // fall through

        case T_FUNCTION:
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_PACKAGE:
        case T_THREAD: {		// for each of these, perform it once for each line in the input
            Value::vector *vec = new Value::vector ;
            char line[1024] ;
            char *p = line ;
            char c ;
            while (!stream->eof()) {
                c = stream->get() ;
                if (!stream->eof()) {
                    *p++ = c ;
                    if (c == '\n') {
                        *p++ = 0 ;
                        Value v = string (line) ;
                        Operand res ;
                        callFunction (&res, block, right.object, v) ;
                        vec->push_back (res.val) ;
                        p = line ;
                    }
                }
            }
            set (dest, (vec)) ;
            return ;
            }
        case T_NONE: {
            Value out = &output ;
            this->stream (dest, left, out) ;
            break ;
            }
        default:
            stream->readValue (right) ;
        }
    } else if (right.type == T_STREAM) {   // output to stream
        if (left.type == T_VECTOR) {
            bool hasobjects = false ;
            int size = left.vec->size() ;
            // optimization: check if there are any objects in the vector
            for (int i = 0 ; i < size ; i++) {
                Value &v = (*left.vec)[i] ;
                if (v.type == T_OBJECT && v.object != NULL) {
                    hasobjects = true ;
                    break ;
                }
            }
            if (hasobjects) {
                for (int i = 0 ; i < size ; i++) {
                    Value &v = (*left.vec)[i] ;
                    InterpretedBlock *opfunc ;
                    if (v.type == T_OBJECT && v.object != NULL && (opfunc = checkForOperator (v, ARROW)) != NULL) {
                        callFunction (dest, opfunc, v, right, false) ;
                    } else {
                        right.stream->writeValue (v) ;
                    }
                }
            } else {
                right.stream->writeValue (left) ;
            }
        } else {
            right.stream->writeValue (left) ;
        }
    } else {
        Scope *scope ;
        switch (right.type) {
        case T_NONE: {               // special case.  Streaming to none is equivalent to the the VM output stream
            //printf ("streaming to vm output\n") ;
            Value out = &output ;
            stream (dest, left, out) ;
            return ;
            }
        case T_INTEGER: {
            switch (left.type) {
            case T_INTEGER: {
                right.integer = left.integer ;
                break ;
                }
            case T_REAL: {
                right.integer = (INTEGER)left.real ;
                break ;
                }
            case T_STRING: {
                if (!convertStringToInt (*left.str, right.integer)) {
                    runtimeError ("Illegal stream operation.  This string doesn't contain an integer") ;
                }
                break ;
                }
            case T_CHAR: {
                right.integer = left.integer ;
                break ;
                }
            case T_BYTE: {
                right.integer = left.integer ;
                break ;
                }
            case T_VECTOR: {
                right.integer = (*left.vec)[0] ;
                break ;
                }
            case T_BYTEVECTOR: {
                right.integer = (*left.bytevec)[0] ;
                break ;
                }
            case T_MAP: {
                right.integer = (*left.m)[0] ;
                break ;
                }
            case T_FUNCTION: 
            case T_THREAD: 
            case T_CLASS: 
            case T_INTERFACE: 
            case T_MONITOR: 
            case T_PACKAGE: 
            case T_ENUM: {
                right.integer = reinterpret_cast<INTEGER>(left.block) ;
                break ;
                }
            case T_CLOSURE:
                right.integer = reinterpret_cast<INTEGER>(left.closure->block) ;
                break ;
            case T_ENUMCONST: {
                break ;
                }
            case T_OBJECT: {
                if (left.object == NULL) {
                    right.integer = 0 ;
                } else {
                    Variable *var = left.object->block->findVariable (string("toInteger"), scope, VAR_PUBLIC, ir->source, NULL) ;
                    if (var == NULL) {
                        right.integer = reinterpret_cast<INTEGER>(left.object) ;
                    } else {
                        Value &v = var->getValue(left.object) ;
                        if (v.type != T_FUNCTION) {
                            right.integer = reinterpret_cast<INTEGER>(left.object) ;
                        } else {
                            callFunction (dest, v.block, left.object) ;
                            return ;
                        }
                    }
                }
                break ;
                }
            default:;
            }
            break ;
            }
        case T_REAL: {
            switch (left.type) {
            case T_INTEGER: {
                right.real = left.integer ;
                break ;
                }
            case T_REAL: {
                right.real = left.integer ;
                break ;
                }
            case T_STRING: {
                if (sscanf (left.str->c_str(), "%lg", &right.real) != 1) {
                    runtimeError ("Illegal stream operation.  This string doesn't contain a real") ;
                }
                break ;
                }
            case T_CHAR: {
                right.real = left.integer ;
                break ;
                }
            case T_BYTE: {
                right.real = left.integer ;
                break ;
                }
            case T_VECTOR: {
                right.real = (*left.vec)[0] ;
                break ;
                }
            case T_BYTEVECTOR: {
                right.real = (*left.bytevec)[0] ;
                break ;
                }
            case T_MAP: {
                right.real = (*left.m)[0] ;
                break ;
                }
            case T_FUNCTION: 
            case T_THREAD: 
            case T_CLASS: 
            case T_INTERFACE: 
            case T_MONITOR: 
            case T_PACKAGE: 
            case T_ENUM: 
            case T_CLOSURE:
            case T_ENUMCONST: {
                runtimeError ("Illegal stream operation.  Can't convert this to real") ;
                break ;
                }
            case T_OBJECT: {
                if (left.object == NULL) {
                    right.integer = 0 ;
                } else {
                    Variable *var = left.object->block->findVariable (string("toReal"), scope, VAR_PUBLIC, ir->source, NULL) ;
                    if (var == NULL) {
                        runtimeError ("Illegal stream operation.  Can't convert object to real") ;
                    } else {
                        Value &v = var->getValue(left.object) ;
                        if (v.type != T_FUNCTION) {
                            runtimeError ("Illegal stream operation.  Can't convert object to real") ;
                        } else {
                            callFunction (dest, v.block, left.object) ;
                            return ;
                        }
                    }
                }
                break ;
                }
            default:;
            }
            break ;
            }
        case T_STRING: {
            switch (left.type) {
            case T_INTEGER: {
                char buf[32] ;
                sprintf (buf, "%lld", left.integer) ;
                right.str->replace (0, right.str->length(), string (buf)) ;
                break ;
                }
            case T_REAL: {
                char buf[32] ;
                sprintf (buf, "%g", left.real) ;
                right.str->replace (0, right.str->length(), string (buf)) ;
                break ;
                }
            case T_STRING: {
                //right.str = left.str ;
                right.str->replace (0, right.str->length(), *left.str) ;
                break ;
                }
            case T_CHAR: {
                char buf[32] ;
                sprintf (buf, "%c", (char)left.integer) ;
                right.str->replace (0, right.str->length(), string (buf)) ;
                break ;
                }
            case T_BYTE: {
                char buf[32] ;
                sprintf (buf, "0x%x", (unsigned char)left.integer) ;
                right.str->replace (0, right.str->length(), string (buf)) ;
                break ;
                }
            case T_VECTOR: {
#if defined(_CC_GCC) && __GNUC__ == 2
                std::strstream *s = new std::strstream();
#else
                std::stringstream *s = new std::stringstream();
#endif

                StdStream *str = new StdStream (s) ;
                Value sval (str) ;
                Operand sop = sval ;
                for (int i = 0 ; i < left.vec->size() ; i++) {
                    Value &v = (*left.vec)[i] ;
                    streamCopy (&sop, v, sval) ;
                }
                right.str->replace (0, right.str->length(), s->str()) ;
                break ;
                }
            case T_BYTEVECTOR: {
#if defined(_CC_GCC) && __GNUC__ == 2
                std::strstream *s = new std::strstream();
#else
                std::stringstream *s = new std::stringstream();
#endif

                StdStream *str = new StdStream (s) ;
                Value sval (str) ;
                Operand sop = sval ;
                for (int i = 0 ; i < left.bytevec->size() ; i++) {
                    Value v = (*left.bytevec)[i] ;		// T_BYTE value
                    streamCopy (&sop, v, sval) ;
                }
                right.str->replace (0, right.str->length(), s->str()) ;
                break ;
                }
            case T_MAP: {
                break ;
                }
            case T_FUNCTION: 
            case T_THREAD: 
            case T_CLASS: 
            case T_INTERFACE: 
            case T_MONITOR: 
            case T_PACKAGE: 
            case T_ENUM: {
                right.str->replace (0, right.str->length(), left.block->name) ;
                break ;
                }
            case T_CLOSURE:
                right.str->replace (0, right.str->length(), left.closure->block->name) ;
                break ;

            case T_ENUMCONST: {
                break ;
                }
            case T_OBJECT: {
                if (left.object == NULL) {
                    right.str->replace (0, right.str->length(), string ("null")) ;
                } else {
                    Variable *var = left.object->block->findVariable (string("toString"), scope, VAR_PUBLIC, ir->source, NULL) ;
                    if (var == NULL) {
                        right.str->replace (0, right.str->length(), string (left.object->block->name + "@")) ;
                        char buf[32] ;
                        sprintf (buf, "%llx", reinterpret_cast<INTEGER>(left.object)) ;
                        *right.str += buf ;
                    } else {
                        Value &v = var->getValue(left.object) ;
                        if (v.type != T_FUNCTION) {
		            right.str->replace (0, right.str->length(), string (left.object->block->name + "@")) ;
			    char buf[32] ;
			    sprintf (buf, "%llx", reinterpret_cast<INTEGER>(left.object)) ;
			    *right.str += buf ;
                        } else {
                            callFunction (dest, v.block, left.object) ;
                            return ;
                        }
                    }
                }
                break ;
                }
            default:;
            }
            break ;
            }
        case T_CHAR: {
            switch (left.type) {
            case T_INTEGER: {
                right.integer = left.integer ;
                break ;
                }
            case T_REAL: {
                right.integer = (INTEGER)left.real ;
                break ;
                }
            case T_STRING: {
                right.integer = (*left.str)[0] ;
                break ;
                }
            case T_CHAR: {
                right.integer = left.integer ;
                break ;
                }
            case T_VECTOR: {
                right.integer = (*left.vec)[0] ;
                break ;
                }
            case T_BYTEVECTOR: {
                right.integer = (*left.bytevec)[0] ;
                break ;
                }
            case T_MAP: {
                right.integer = (*left.m)[0] ;
                break ;
                }
            case T_FUNCTION: 
            case T_THREAD: 
            case T_CLASS: 
            case T_INTERFACE: 
            case T_MONITOR: 
            case T_PACKAGE: 
            case T_CLOSURE: 
            case T_ENUM: {
                runtimeError ("Illegal stream operation.  Can't convert this to char") ;
                break ;
                }
            case T_ENUMCONST: {
                break ;
                }
            case T_OBJECT: {
                if (left.object == NULL) {
                    right.integer = 0 ;
                } else {
                    Variable *var = left.object->block->findVariable (string("toChar"), scope, VAR_PUBLIC, ir->source, NULL) ;
                    if (var == NULL) {
                        right.integer = reinterpret_cast<INTEGER>(left.object) ;
                    } else {
                        Value &v = var->getValue(left.object) ;
                        if (v.type != T_FUNCTION) {
                            right.integer = reinterpret_cast<INTEGER>(left.object) ;
                        } else {
                            callFunction (dest, v.block, left.object) ;
                            return ;
                        }
                    }
                }
                break ;
                }
            default:;
            }
            break ;
            }
        case T_BYTE: {
            switch (left.type) {
            case T_INTEGER: {
                right.integer = left.integer ;
                break ;
                }
            case T_REAL: {
                right.integer = (INTEGER)left.real ;
                break ;
                }
            case T_STRING: {
                right.integer = (*left.str)[0] ;
                break ;
                }
            case T_CHAR: {
                right.integer = left.integer ;
                break ;
                }
            case T_BYTE: {
                right.integer = left.integer ;
                break ;
                }
            case T_VECTOR: {
                right.integer = (*left.vec)[0] ;
                break ;
                }
            case T_BYTEVECTOR: {
                right.integer = (*left.bytevec)[0] ;
                break ;
                }
            case T_MAP: {
                right.integer = (*left.m)[0] ;
                break ;
                }
            case T_FUNCTION: 
            case T_THREAD: 
            case T_CLASS: 
            case T_CLOSURE: 
            case T_INTERFACE: 
            case T_MONITOR: 
            case T_PACKAGE: 
            case T_ENUM: {
                runtimeError ("Illegal stream operation.  Can't convert this to byte") ;
                break ;
                }
            case T_ENUMCONST: {
                break ;
                }
            case T_OBJECT: {
                if (left.object == NULL) {
                    right.integer = 0 ;
                } else {
                    Variable *var = left.object->block->findVariable (string("toByte"), scope, VAR_PUBLIC, ir->source, NULL) ;
                    if (var == NULL) {
                        right.integer = reinterpret_cast<INTEGER>(left.object) ;
                    } else {
                        Value &v = var->getValue(left.object) ;
                        if (v.type != T_FUNCTION) {
                            right.integer = reinterpret_cast<INTEGER>(left.object) ;
                        } else {
                            callFunction (dest, v.block, left.object) ;
                            return ;
                        }
                    }
                }
                break ;
                }
            default:;
            }
            break ;
            }
        case T_VECTOR: {
            if (left.type != T_OBJECT) {
                if (left.type == T_VECTOR) {		// for vector -> vector, add all elements of left
                    for (int i = 0 ; i < left.vec->size() ; i++) {
                        right.vec->push_back ((*left.vec)[i]) ;
                    }
                } else {
                    right.vec->push_back (left) ;
                }
            } else {
                if (left.object == NULL) {
                    right.vec->push_back (left) ;
                } else {
                    Variable *var = left.object->block->findVariable (string("toVector"), scope, VAR_PUBLIC, ir->source, NULL) ;
                    if (var == NULL) {
                        right.vec->push_back (left) ;
                    } else {
                        Value &v = var->getValue(left.object) ;
                        if (v.type != T_FUNCTION) {
                            right.vec->push_back (left) ;
                        } else {
                            callFunction (dest, v.block, left.object) ;
                            return ;
                        }
                    }
                }
            }
            break ;
            }
        case T_BYTEVECTOR: {
            if (left.type != T_OBJECT) {
                if (left.type == T_BYTEVECTOR) {		// for bytevector -> bytevector, add all elements of left
                    for (int i = 0 ; i < left.vec->size() ; i++) {
                        right.bytevec->push_back ((*left.vec)[i]) ;
                    }
                } else {
                    if (isIntegral (left)) {
                        right.bytevec->push_back (getInt (left)) ;
                    } else {
                        runtimeError ("Cannot stream a value of type %s to a bytevector", typestring (left).c_str()) ;
                    }
                }
            } else {
                if (left.object == NULL) {
                    right.bytevec->push_back (0) ;
                } else {
                    Variable *var = left.object->block->findVariable (string("toByteVector"), scope, VAR_PUBLIC, ir->source, NULL) ;
                    if (var == NULL) {
                        if (isIntegral (left)) {
                            right.bytevec->push_back (getInt (left)) ;
                        } else {
                            runtimeError ("Cannot stream a value of type %s to a bytevector", typestring (left).c_str()) ;
                        }
                    } else {
                        Value &v = var->getValue(left.object) ;
                        if (v.type != T_FUNCTION) {
                            if (isIntegral (left)) {
                                right.bytevec->push_back (getInt (left)) ;
                            } else {
                                runtimeError ("Cannot stream a value of type %s to a bytevector", typestring (left).c_str()) ;
                            }
                        } else {
                            callFunction (dest, v.block, left.object) ;
                            return ;
                        }
                    }
                }
            }
            break ;
            }
        case T_MAP: {
            if (left.type != T_OBJECT) {
                if (left.type == T_VECTOR) {		// for vector -> map, add all elements of left
                    for (int i = 0 ; i < left.vec->size() ; i++) {
                        Value &elem = (*left.vec)[i] ;
                        right.m->insert (elem, elem) ;
                    }
                } else {
                    right.m->insert (left, left) ;
                }
            } else {
                if (left.object == NULL) {
                    right.m->insert (left, left) ;
                } else {
                    Variable *var = left.object->block->findVariable (string("toMap"), scope, VAR_PUBLIC, ir->source, NULL) ;
                    if (var == NULL) {
                        right.m->insert (left, left) ;
                    } else {
                        Value &v = var->getValue(left.object) ;
                        if (v.type != T_FUNCTION) {
                            right.m->insert (left, left) ;
                        } else {
                            callFunction (dest, v.block, left.object) ;
                            return ;
                        }
                    }
                }
            }
            break ;
            }
        case T_FUNCTION: 
        case T_THREAD: 
        case T_CLASS: 
        case T_INTERFACE: 
        case T_MONITOR: 
        case T_CLOSURE:
        case T_PACKAGE: {
            Value result ;
            Block *block = right.type == T_CLOSURE ? right.closure->block : right.block ;
            if (left.type == T_VECTOR) {
                Value::vector *vec = new Value::vector (left.vec->size()) ;
                vec->clear() ;
                for (int i = 0 ; i < left.vec->size() ; i++) {
                    Value &v = (*left.vec)[i] ;
                    Operand res ;
                    callFunction (&res, block, right.object) ;
                    vec->push_back (res.val) ;
                }
                set (dest, (vec)) ;
                return ;
            } else if (left.type == T_MAP) {
                runtimeError ("Illegal stream operation.  Map -> block not implemented") ;
            } else {
                callFunction (dest, block, right.object) ;
                return ;
            }
            break ;
            }
        case T_ENUM: {
            runtimeError ("Illegal stream operation.  No output conversions to enum are possible") ;
            break ;
            }
        case T_ENUMCONST: {
            runtimeError ("Illegal stream operation.  No output conversions to enum constant are possible") ;
            break ;
            }
        case T_OBJECT: {
            runtimeError ("Illegal stream operation.  No output conversions to objects are possible") ;
            break ;
            }
        default:
            runtimeError ("Illegal stream operation: No conversion to this type is possible") ;
        }
    }
    if (dest != NULL && dest->type != tVALUE) {
        set (dest, right) ;
    }
}

bool VirtualMachine::convertStringToInt (string str, INTEGER &r) {
    int base = 10 ;
    int index = 0 ;
    bool neg = false ;
    while (isspace (str[index])) index++ ;
    if (!isdigit (str[index])) {
       if (str[index] == '-') {
           neg = true ;
           index++ ;
       } else {
           return false ;
       }
    }
    if (str[index] == '0') {
       if (str[index+1] == 'x' || str[index + 1] == 'X') {
           base = 16 ;
       } else if (str[index + 1] == 'b' || str[index + 1] == 'B') {
           base = 2 ;
           index += 2 ;
       } else {
           base = 8 ;
       }
    }
    r = OS::strtoll (str.c_str() + index, NULL, base) ;
    if (neg) {
        r = -r ;
    }
    return true ;
}


//
// cast one value to another.  Similar to the stream operator
//

// dest might be the same location as the from or to operands so we can't overwrite
// it until after the conversion is done
void VirtualMachine::cast (Operand *dest, Operand *to, Operand *from, Operand *pnum) {
    InterpretedBlock *opfunc ;
    Value &f = get (from) ;
    Value tmp = get (to) ;
    if (f.type == T_OBJECT && f.object != NULL && (opfunc = checkForOperator (f, CAST)) != NULL) {
        callFunction (dest, opfunc, f, tmp) ;
        return ;
    }
    if (!convertType (f, tmp)) {
        if (pnum == NULL) {
            runtimeError ("Illegal cast: cannot convert %s to %s", typestring (f).c_str(), typestring (tmp).c_str()) ;
        } else {
            runtimeError ("Illegal type for argument #%lld: cannot convert %s to %s", get(pnum).integer + 1, typestring (f).c_str(), typestring (tmp).c_str()) ;
        }
    }
    set (dest, tmp) ;
}

//
// convert the type of value (from) to the value (to)
//

bool VirtualMachine::convertType (const Value &from, Value &to) {
    Scope *scope ;
    if (to.type == from.type) {		// no conversion necessary?
       to = from ;	
       return true ;
    }
    switch (to.type) {
    case T_INTEGER: {
        switch (from.type) {
        case T_INTEGER: {
            to.integer = from.integer ;
            break ;
            }
        case T_REAL: {
            to.integer = (INTEGER)from.real ;
            break ;
            }
        case T_STRING: {
            if (!convertStringToInt (*from.str, to.integer)) {
                return false ;
            }
            break ;
            }
        case T_CHAR: {
            to.integer = from.integer ;
            break ;
            }
        case T_BYTE: {
            to.integer = from.integer ;
            break ;
            }
        case T_VECTOR: 
        case T_BYTEVECTOR: 
        case T_MAP: {
            return false ;
            }

        case T_ENUMCONST: {
            to.integer = from.ec->value ;
            break ;
            }

        case T_MEMORY:
           to.integer = reinterpret_cast<INTEGER>(from.mem->mem) ;
           break ;

        case T_POINTER:
           to.integer = reinterpret_cast<INTEGER>(from.pointer->mem->mem + from.pointer->offset) ;
           break ;

        case T_FUNCTION: 
        case T_THREAD: 
        case T_CLASS: 
        case T_CLOSURE: 
        case T_INTERFACE: 
        case T_MONITOR: 
        case T_PACKAGE: 
        case T_ENUM: 
            return false ;
        case T_OBJECT: {
            if (from.object == NULL) {
               to.integer = 0 ;
            } else {
                Variable *var = from.object->block->findVariable (string("toInteger"), scope, VAR_PUBLIC, ir->source, NULL) ;
                if (var == NULL) {
                    return false ;
                } else {
                    Value &v = var->getValue(from.object) ;
                    if (v.type != T_FUNCTION) {
                        return false ;
                    } else {
                        Operand res ;
                        callFunction (&res, v.block, from.object) ;
                        to = res.val ;
                    }
                }
            }
            break ;
            }
        default:;
        }
        break ;
        }
    case T_REAL: {
        switch (from.type) {
        case T_INTEGER: {
            to.real = from.integer ;
            break ;
            }
        case T_REAL: {
            to.real = from.real ;
            break ;
            }
        case T_STRING: {
            if (sscanf (from.str->c_str(), "%lg", &to.real) != 1) {
                return false ;
            }
            break ;
            }
        case T_CHAR: {
            to.real = from.integer ;
            break ;
            }
        case T_BYTE: {
            to.real = from.integer ;
            break ;
            }

        case T_ENUMCONST: 
            to.real = from.ec->value ;
            break ;

        case T_VECTOR: 
        case T_BYTEVECTOR: 
        case T_MAP: {
            return false ;
            }
        case T_FUNCTION: 
        case T_THREAD: 
        case T_CLASS: 
        case T_CLOSURE: 
        case T_INTERFACE: 
        case T_MONITOR: 
        case T_PACKAGE: 
        case T_ENUM: 
            return false ;
        case T_OBJECT: {
            if (from.object == NULL) {
                return false;

            } else {
                Variable *var = from.object->block->findVariable (string("toReal"), scope, VAR_PUBLIC, ir->source, NULL) ;
                if (var == NULL) {
                    return false ;
                } else {
                    Value &v = var->getValue(from.object) ;
                    if (v.type != T_FUNCTION) {
                        return false ;
                    } else {
                        Operand res ;
                        callFunction (&res, v.block, from.object) ;
                        to = res.val ;
                    }
                }
            }
            break ;
            }
        default:;
        }
        break ;
        }
    case T_STRING: {
        switch (from.type) {
        case T_INTEGER: {
            char buf[32] ;
            sprintf (buf, "%lld", from.integer) ;
            decRef (to.str, string) ;
            to.str = new string (buf) ;
            incRef (to.str, string) ;
            //to.str->replace (0, to.str->length(), string (buf)) ;
            break ;
            }
        case T_REAL: {
            char buf[32] ;
            sprintf (buf, "%g", from.real) ;
            decRef (to.str, string) ;
            to.str = new string (buf) ;
            incRef (to.str, string) ;
            //to.str->replace (0, to.str->length(), string (buf)) ;
            break ;
            }
        case T_STRING: {
            decRef (to.str, string) ;
            to.str = from.str ;
            incRef (to.str, string) ;
            break ;
            }
        case T_CHAR: {
            char buf[32] ;
            sprintf (buf, "%c", (char)from.integer) ;
            decRef (to.str, string) ;
            to.str = new string (buf) ;
            incRef (to.str, string) ;
            //to.str->replace (0, to.str->length(), string (buf)) ;
            break ;
            }
        case T_BYTE: {
            char buf[32] ;
            buf[0] = (char)from.integer ;
            decRef (to.str, string) ;
            to.str = new string (buf, 1) ;
            incRef (to.str, string) ;
            //to.str->replace (0, to.str->length(), string (buf)) ;
            break ;
            }
        case T_BYTEVECTOR: 
            decRef (to.str, string) ;
            to.str = new string ((char*)&(*(from.bytevec->begin())), from.bytevec->size()) ;
            incRef (to.str, string) ;
            break ;
        case T_VECTOR: 
        case T_MAP: {
            return false ;
            }
        case T_FUNCTION: 
        case T_THREAD: 
        case T_CLASS: 
        case T_INTERFACE: 
        case T_MONITOR: 
        case T_PACKAGE: 
        case T_ENUM: {
            decRef (to.str, string) ;
            to.str = new string (from.block->name) ;
            incRef (to.str, string) ;
            //to.str->replace (0, to.str->length(), from.block->name) ;
            break ;
            }
        case T_CLOSURE: {
            decRef (to.str, string) ;
            to.str = new string (from.closure->block->name) ;
            incRef (to.str, string) ;
            break ;
            }
        case T_ENUMCONST: {
            decRef (to.str, string) ;
            to.str = new string (from.ec->name) ;
            incRef (to.str, string) ;
            break ;
            }
        case T_OBJECT: {
            if (from.object == NULL) {
                decRef (to.str, string) ;
                to.str = new string ("null") ;
                incRef (to.str, string) ;
                //to.str->replace (0, to.str->length(), string ("null")) ;
            } else {
                Variable *var = from.object->block->findVariable (string("toString"), scope, VAR_PUBLIC, ir->source, NULL) ;
                if (var == NULL) {
                    return false ;
                } else {
                    Value &v = var->getValue(from.object) ;
                    if (v.type != T_FUNCTION) {
                        return false ;
                    } else {
                        Operand res ;
                        callFunction (&res, v.block, from.object) ;
                        to = res.val ;
                    }
                }
            }
            break ;
            }
        default:;
        }
        break ;
        }
    case T_CHAR: {
        switch (from.type) {
        case T_INTEGER: {
            to.integer = from.integer ;
            break ;
            }
        case T_REAL: {
            to.integer = (INTEGER)from.real ;
            break ;
            }
        case T_STRING: {
            return false ;
            }
        case T_CHAR: {
            to.integer = from.integer ;
            break ;
            }
        case T_ENUMCONST: 
            to.integer = from.ec->value ;
            break ;
        case T_BYTE:
            to.integer = from.integer ;
            break ;
        case T_VECTOR: 
        case T_BYTEVECTOR: 
        case T_MAP: 
        case T_FUNCTION: 
        case T_THREAD: 
        case T_CLASS: 
        case T_CLOSURE: 
        case T_INTERFACE: 
        case T_MONITOR: 
        case T_PACKAGE: 
        case T_ENUM: 
            return false ;
        case T_OBJECT: {
            if (from.object == NULL) {
                return false ;
            } else {
                Variable *var = from.object->block->findVariable (string("toChar"), scope, VAR_PUBLIC, ir->source, NULL) ;
                if (var == NULL) {
                    return false ;
                } else {
                    Value &v = var->getValue(from.object) ;
                    if (v.type != T_FUNCTION) {
                        return false ;
                    } else {
                        Operand res ;
                        callFunction (&res, v.block, from.object) ;
                        to = res.val ;
                    }
                }
            }
            break ;
            }
        default:;
        }
        break ;
        }
    case T_BYTE: {
        switch (from.type) {
        case T_INTEGER: {
            to.integer = from.integer ;
            break ;
            }
        case T_REAL: {
            to.integer = (INTEGER)from.real ;
            break ;
            }
        case T_STRING: {
            return false ;
            }
        case T_CHAR: {
            to.integer = from.integer ;
            break ;
            }
        case T_BYTE: {
            to.integer = from.integer ;
            break ;
            }
        case T_ENUMCONST: 
            to.integer = from.ec->value ;
            break ;

        case T_VECTOR: 
        case T_BYTEVECTOR:
        case T_MAP: 
        case T_FUNCTION: 
        case T_THREAD: 
        case T_CLASS: 
        case T_CLOSURE: 
        case T_INTERFACE: 
        case T_MONITOR: 
        case T_PACKAGE: 
        case T_ENUM: 
            return false ;
        case T_OBJECT: {
            if (from.object == NULL) {
                return false ;
            } else {
                Variable *var = from.object->block->findVariable (string("toByte"), scope, VAR_PUBLIC, ir->source, NULL) ;
                if (var == NULL) {
                    return false ;
                } else {
                    Value &v = var->getValue(from.object) ;
                    if (v.type != T_FUNCTION) {
                        return false ;
                    } else {
                        Operand res ;
                        callFunction (&res, v.block, from.object) ;
                        to = res.val ;
                    }
                }
            }
            break ;
            }
        default:;
        }
        break ;
        }
    case T_VECTOR: {
        if (from.type != T_OBJECT) {
            return false ;
        } else {
            if (from.object == NULL) {
                return false ;
            } else {
                Variable *var = from.object->block->findVariable (string("toVector"), scope, VAR_PUBLIC, ir->source, NULL) ;
                if (var == NULL) {
                    return false ;
                } else {
                    Value &v = var->getValue(from.object) ;
                    if (v.type != T_FUNCTION) {
                        return false ;
                    } else {
                        Operand res ;
                        callFunction (&res, v.block, from.object) ;
                        to = res.val ;
                    }
                }
            }
        }
        break ;
        }
    case T_BYTEVECTOR: {
        if (from.type != T_OBJECT) {
            switch (from.type) {
            case T_INTEGER:
            case T_REAL:
                for (int i = 56 ; i >= 0 ; i -= 8) {
                    to.bytevec->push_back (from.integer >> i) ;
                }
                break ;
            case T_CHAR:
            case T_BYTE:
                to.bytevec->push_back (from.integer) ;
                break ;
            case T_STRING:
                for (int i = 0 ; i < from.str->size() ; i++) {
                    to.bytevec->push_back ((*from.str)[i]) ;
                }
            default:
                return false ;
            }
        } else {
            if (from.object == NULL) {
                return false ;
            } else {
                Variable *var = from.object->block->findVariable (string("toByteVector"), scope, VAR_PUBLIC, ir->source, NULL) ;
                if (var == NULL) {
                    return false ;
                } else {
                    Value &v = var->getValue(from.object) ;
                    if (v.type != T_FUNCTION) {
                        return false ;
                    } else {
                        Operand res ;
                        callFunction (&res, v.block, from.object) ;
                        to = res.val ;
                    }
                }
            }
        }
        break ;
        }
    case T_MAP: {
        if (from.type != T_OBJECT) {
            return false ;
        } else {
            if (from.object == NULL) {
                return false ;
            } else {
                Variable *var = from.object->block->findVariable (string("toMap"), scope, VAR_PUBLIC, ir->source, NULL) ;
                if (var == NULL) {
                    return false ;
                } else {
                    Value &v = var->getValue(from.object) ;
                    if (v.type != T_FUNCTION) {
                        return false ;
                    } else {
                        Operand res ;
                        callFunction (&res, v.block, from.object) ;
                        to = res.val ;
                    }
                }
            }
        }
        break ;
        }

    case T_CLASS: 
    case T_INTERFACE: 
    case T_MONITOR: 
    case T_PACKAGE: 
        if (from.type == T_OBJECT) {
            if (from.object != NULL) {
                if (!blocksCompatible (to.block, from.object->block)) {
                    return false ;
                }
            }
            to = from ;
        } else if (from.type == to.type) {
            return true ;
        } else {
            stack[sp++] = from ;
            Operand result ;
            newObject (&result, 1, to.type, to.cls, false) ;
            to = result.val ;
        }
        break ;

    case T_FUNCTION: 
    case T_THREAD: 
        if (from.type == to.type) {
            to.block = from.block ;
            return true ;
        } else {
            stack[sp++] = from ;
            Operand res ;
            callFunction (&res, 1, to.func) ;
            to = res.val ;
        }
        break ;

    case T_CLOSURE:
        switch (to.closure->type) {
        case T_CLASS:
        case T_INTERFACE:
        case T_MONITOR:
        case T_PACKAGE:
            if (from.type == T_OBJECT) {
                if (from.object != NULL) {
                    if (!blocksCompatible (to.closure->block, from.object->block)) {
                        return false ;
                    }
                }
                to = from ;
            } else if (from.type == to.closure->type) {
                return true ;
            } else {
                stack[sp++] = from ;
                Operand result ;
                newObject (&result, 1, to.closure->type, (Class*)to.closure->block, false) ;
                to = result.val ;
            }
            break ;
        case T_FUNCTION:
        case T_THREAD:
            if (from.type == to.type) {
                to.closure->block = from.block ;
                return true ;
            } else {
                stack[sp++] = from ;
                Operand res ;
                callFunction (&res, 1, (Function*)to.closure->block) ;
                to = res.val ;
            }
            break ;
        }
        break ;

    case T_ENUM: 
        if (from.type == T_INTEGER || from.type == T_BYTE || from.type == T_CHAR) {           // allow cast from int to enum
            Enum *e = to.en ;
            for (int i = 0 ; i < e->consts.size() ; i++) {
                if (e->consts[i]->value == from.integer) {
                    to.type = T_ENUMCONST ;
                    to.ec = e->consts[i] ;
                    return true ;
                }
            }
            return false ;
        } else if (from.type == T_ENUMCONST) {
            if (from.ec->en != to.en) {
                return false ;
            }
            to = from ;
        } else if (from.type == T_ENUM) {
            if (to.en != from.en) {
                return false ;
            }
        } else if (from.type == T_STRING) {
            // search enum for string
            Enum *e = to.en ;
            for (int i = 0 ; i < e->consts.size() ; i++) {
                if (e->consts[i]->name == *from.str) {
                    to.type = T_ENUMCONST ;
                    to.ec = e->consts[i] ;
                    return true ;
                }
            }
            return false ;
            
        }
        break ;
    case T_ENUMCONST: 
        return false ;

    case T_OBJECT: 
        if (from.type == T_OBJECT) {
            to = from ;
            return true ;
        }
        return false ;

    case T_STREAM:
        if (from.type == T_STREAM) {
            to = from ;
            return true ;
        }
        if (from.type == T_OBJECT && checkForOperator (from, ARROW) != NULL) {
            to = from ;
            return true ;
        }
        return false ;

    default:
        return false ;
    }
    return true ;
}

//
// runtime searches for members.  The members exist in a block, but their value
// exists in a stack frame.  The data needed for the search is:
//
// 1. the block to search
// 2. the name of the member
// 3. the stack frame in which the member's data exists
//
// The current stack frame is in the variable 'frame'.  This is the call
// stack frame and it set when we make a call.  We need another stack
// frame for member searching.  
//

void VirtualMachine::findValue (Operand *dest, Operand *l, Operand *r) {
    Value &left = get (l) ;
    Variable *mem ;
    Value &right = get (r) ;

    switch (left.type) {
    case T_CLOSURE:
        if (left.closure->vm_state != NULL) {
            goto coroutine ;
        }
        // fall through
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_THREAD: {
        StackFrame *parentFrame = frame ;
        Block *block = left.type == T_CLOSURE ? left.closure->block : left.block ;
        bool directParent = findParentBlock(block, parentFrame) ;

        mem = findMember (right, block, frame, directParent) ;
        if (mem == NULL) {
            runtimeError ("\"%s\" is not a member of block %s", getMemname(right), block->name.c_str()) ;
        } else {
            if (mem->flags & VAR_STATIC) {
#if 0
                set (dest, mem->getValue (parentFrame->stack)) ;                     // statics are in parent's stack frame
#else
                set (dest, mem->getValue (getMainStack())) ;                     // statics are in main
#endif
            } else {
                if (directParent) {
                    if (mem->flags & VAR_OLDVALUE) {
                        set (dest, mem->oldValue) ;
                    } else {
                        set (dest, mem->getValue (parentFrame)) ;
                    }
                } else {
                    runtimeError ("Cannot access %s.%s, it is not static", block->name.c_str(), mem->name.c_str()) ;
                }
            }
        }

        break ;
        }

    case T_OBJECT: {
        Object *obj = left.object ;
        if (obj == NULL) {
            runtimeError ("Reference through a null pointer") ;
        }
        Block *block = obj->block ;
        mem = findMember (right, block, frame, isParentBlock(block)) ;
        if (mem == NULL) {
            if (!callOperatorDot (dest, left, right)) {
                runtimeError ("\"%s\" is not a member of block %s", getMemname(right), block->name.c_str()) ;
            }
        } else {
            if (mem->flags & VAR_STATIC) {
                set (dest, getMainStack()->varstack[mem->offset]) ;
            } else {
                // because the object is required after the set is done, we need to
 		// keep it around until after the release
                incRef (obj, object) ;
                obj->acquire(block->flags & BLOCK_SYNCHRONIZED) ;
                set (dest, obj->varstack[mem->offset]) ;
                obj->release(block->flags & BLOCK_SYNCHRONIZED) ;
                decObjectRef (obj) ;		// delete the object if necessary
            }
        }
        break ;
        }

    case T_MAP: {
        // allow . to be used to look up a map with a string value
        VariableName *var = (VariableName*)right.ptr ;
        if (var->name == "length") {
            // special .length is same as sizeof
            dosizeof(dest, l);
            break;
        }
        map::iterator s = left.m->find (var->name) ;
        if (s != left.m->end()) {
            set (dest, (*s).second) ;
            break;
        }

        // check for builtin map function
        Function *func = findBuiltinMethod (var->name) ;
        if (func == NULL) {
            set (dest, none);
        } else {
            set (dest, func) ;
        }
        break ;
        }


   coroutine:
   default: {
        VariableName *var = (VariableName*)right.ptr ;
        if (var->name == "length") {
            // special .length is same as sizeof
            dosizeof(dest, l);
            break;
        }
       Function *func = findBuiltinMethod (var->name) ;
       if (func == NULL) {
           runtimeError ("Illegal use of operator . (cannot be used on type %s)", typestring (left).c_str()) ;
       }
       set (dest, func) ;
       break ;
       }
   }
}

// call the 'operator .' for a block
bool VirtualMachine::callOperatorDot (Operand *dest, Value &left, Value &right) {
    InterpretedBlock *opdot = checkForOperator (left, DOT);
    if (opdot == NULL) {
        return false;
    }
    VariableName *var = static_cast<VariableName*>(right.ptr) ;

    Value r (new string (var->name));
    callFunction (dest, opdot, left, r);
    return true;
}


//
// store a value in a block member
//
void VirtualMachine::storeValue (Operand *dest, Operand *l, Operand *r, Operand *v) {
    Value &left = get (l) ;
    Value &value = get (v) ;
    Variable *mem ;
    Value &right = get (r) ;

    switch (left.type) {
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_CLOSURE:
    case T_THREAD: {
        StackFrame *parentFrame = frame ;
        Block *block = left.type == T_CLOSURE ? left.closure->block : left.block ;
        bool directParent = findParentBlock(block, parentFrame) ;

        mem = findMember (right, block, frame, directParent) ;
        if (mem == NULL) {
            runtimeError ("\"%s\" is not a member of block %s", getMemname(right), block->name.c_str()) ;
        } else {
            if (mem->flags & VAR_STATIC) {
#if 0
                setVariable (mem, parentFrame->stack, value, true, true) ;
#else
                setVariable (mem, getMainStack(), value, true, true) ;
#endif
            } else {
                if (directParent) {
                    setVariable (mem, parentFrame, value, true, true) ;
                } else {
                    runtimeError ("Cannot access %s.%s, it is not static", block->name.c_str(), mem->name.c_str()) ;
                }
            }
        }

        break ;
        }

    case T_OBJECT: {
        Object *obj = left.object ;
        if (obj == NULL) {
            runtimeError ("Reference through a null pointer") ;
        }
        Block *block = obj->block ;
        StackFrame *parentFrame = frame ;
        mem = findMember (right, block, frame, isParentBlock (block)) ;
        if (mem == NULL) {
            runtimeError ("\"%s\" is not a member of block %s", getMemname(right), block->name.c_str()) ;
        } else {
            if (mem->flags & VAR_STATIC) {
                setVariable (mem, getMainStack(), value, true, true) ;
            } else {
                obj->acquire(block->flags & BLOCK_SYNCHRONIZED) ;
                setVariable (mem, obj, value, true, true) ;
                obj->release(block->flags & BLOCK_SYNCHRONIZED) ;
            }
        }
        break ;
        }

    case T_MAP: {
        // allow . to be used to look up a map with a string value
        VariableName *var = (VariableName*)right.ptr ;
        (*left.m)[Value(var->name)] = value;
        break;
        }
    
   default:
       runtimeError ("Illegal use of operator . (cannot be used on type %s)", typestring (left).c_str()) ;
   }
   set (dest, value) ;
}

//
// find the address of a block member
//
void VirtualMachine::findAddress (Operand *dest, Operand *l, Operand *r) {
    Value &left = get (l) ;
    Variable *mem ;
    Value &right = get (r) ;

    switch (left.type) {
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_CLOSURE:
    case T_THREAD: {
        StackFrame *parentFrame = frame ;
        Block *block = left.type == T_CLOSURE ? left.closure->block : left.block ;
        bool directParent = findParentBlock(block, parentFrame) ;

        mem = findMember (right, block, frame, directParent) ;
        if (mem == NULL) {
            runtimeError ("\"%s\" is not a member of block %s", getMemname(right), block->name.c_str()) ;
        } else {
            if (mem->flags & VAR_STATIC) {
#if 0
                set (dest, mem->getAddress (parentFrame->stack)) ;                     // statics are in parent's stack frame
#else
                set (dest, mem->getAddress (getMainStack())) ;                     // statics are in main
#endif
            } else {
                if (directParent) {
                    if (mem->flags & VAR_OLDVALUE) {
                        set (dest, &mem->oldValue) ;
                    } else {
                        set (dest, mem->getAddress (parentFrame)) ;
                    }
                } else {
                    runtimeError ("Cannot access %s.%s, it is not static", block->name.c_str(), mem->name.c_str()) ;
                }
            }
        }

        break ;
        }

    case T_OBJECT: {
        Object *obj = left.object ;
        if (obj == NULL) {
            runtimeError ("Reference through a null pointer") ;
        }
        Block *block = obj->block ;
        mem = findMember (right, block, frame, isParentBlock (block)) ;
        if (mem == NULL) {
            runtimeError ("\"%s\" is not a member of block %s", getMemname(right), block->name.c_str()) ;
        } else {
            if (mem->flags & VAR_STATIC) {
                set (dest, &getMainStack()->varstack[mem->offset]) ;
            } else {
                set (dest, &obj->varstack[mem->offset]) ;
            }
        }
        break ;
        }

    case T_MAP: {
        // allow . to be used to look up a map with a string value
        VariableName *var = (VariableName*)right.ptr ;
        map::iterator s = left.m->find (var->name) ;
        if (s != left.m->end()) {
            set (dest, &(*s).second) ;
            break;
        }
        break ;
        }

   default:
       runtimeError ("Illegal use of operator . (cannot be used on type %s)", typestring (left).c_str()) ;
   }
}


const char *VirtualMachine::getMemname (Value &val) {
    if (val.type == T_STRING) {
        return val.str->c_str() ;
    } else {
        VariableName *var = (VariableName*)val.ptr ;
        return var->name.c_str() ;
    }
}


//
// runtime search to find a block that may be a parent of this one.  We need to search
// each entry in the scope stack for a block.  This means that each of the superblocks of
// each block need to be searched (inheritance).  Also need to look at the siblings of the
// block (using statements)
//
// returns true if the block can be found and also sets the reference para s to the
// stack frame in which the block exists
//

bool VirtualMachine::findParentBlock (Block *block, StackFrame *&s) {
    if (block == aikido->mainPackage) {
        s = getMainStack() ;
	return true ;
    }
    StaticLink *slink = staticLink ;
    Block *b = scopeStack[scopeSP-1]->majorScope ;
    while (b != NULL) {
	if (b == block) {
            s = slink->frame ;
            return true ;
	}
        if (b->superblock != NULL) {
            Block *super = b->superblock->block ;
            while (super != NULL) {
		if (super == block) {
                    s = slink->frame ;
                    return true ;
		}
            super = super->superblock == NULL ? NULL : super->superblock->block ;
            }
	}
        std::list<Package*>::iterator sibling = b->siblings.begin() ;
	while (sibling != b->siblings.end()) {
            if ((*sibling) == b) {
		s = slink->frame ;
		return true ;
            }
	    sibling++ ;
	}
	b = b->parentBlock ;
	slink = slink->next ;
    }
    return false ;

}

// is the block passed as parent block of the current one.  This happens
// when we use "this.foo" inside a member function in a class.  The
// access protection for the privates of the class need to be overridden

bool VirtualMachine::isParentBlock (Block *block) {
    Block *b = scopeStack[scopeSP-1]->majorScope ;
    while (b != NULL) {
	if (b == block) {
            return true ;
	}
	b = b->parentBlock ;
    }
    return false ;

}

bool VirtualMachine::blocksCompatible (Block *b1, Block *b2) {
    while (b1 != NULL) {
        if (b2->isSuperBlock (b1)) {
            return true ;
        }
        if (b1->superblock == NULL) {
            break ;
        }
        b1 = b1->superblock->block ;
    }
    return false ;
}


void VirtualMachine::instantiateMacro (Macro *macro, Operand *lab) {
    int endlabel = lab->val.integer ;
    if (macro != NULL) {
        macro->instantiate (scopeStack[scopeSP-1]) ;
    }
    execute (pc) ;
    if (macro != NULL) {
       macro->uninstantiate() ;
    }
    pc = endlabel ;
}


// foreach var val

void VirtualMachine::foreach (Value *var, Value val, int end) {
    InterpretedBlock *opfunc = NULL ;
    if (val.type == T_OBJECT && val.object != NULL) {
        opfunc = checkForOperator (val, FOREACH) ;
        while (val.type == T_OBJECT) {
            if (val.object == NULL) {
                runtimeError ("Foreach cannot be applied to null objects") ;
            }
            if (opfunc == NULL) {
                runtimeError ("Foreach cannot be applied to this object, no 'operator foreach' is defined") ;
            }
            if (opfunc->parameters.size() == 2) {		// operator foreach with an iterator?
                break ;
            }
            Operand newval ;
            callFunction (&newval, opfunc, val) ;
            val = newval.val ;
            if (val.type == T_OBJECT) {
                opfunc = checkForOperator (val, FOREACH) ;
            }
        }
    } else if (val.type == T_CLOSURE) {
        // check for valid closure.  This is one that has a vm_state associated with it
        // as it represents the result of 'yield'
        if (val.closure->type != T_FUNCTION || val.closure->vm_state == NULL) {
            runtimeError ("Foreach cannot be applied to this closure") ;
        }
    }

    Foreach *f = new Foreach (this, var, val, end, pc, opfunc) ;
    forStack.push (f) ;

    // for a coroutine, the first evalution has already been done so we don't call next()
    // here.  It will be done by the opNEXT instruction for the next iteration.
    if (val.type != T_CLOSURE) {
        next() ;
    } else {
        f->setControlVar(val.closure->value) ;       // set the control variable to the first value
    }
}

void VirtualMachine::foreach (Value *var, Value val, Value endval, int end) {
    if (isIntegral (val) && isIntegral (endval)) {
        Foreach *f = new Foreach (this, var, val, end, pc, NULL, endval) ;
        forStack.push (f) ;
        next() ;
        return ;
    }
    runtimeError ("Illegal foreach range types %s .. %s", typestring (val).c_str(), typestring (endval).c_str()) ;
}


void VirtualMachine::next() {
    Foreach *f = forStack.top() ;
    f->next() ;
    int p = f->getPC() ;
    if (p < 0) {
        pc = f->getEnd() ;
        delete f ;
        forStack.pop() ;
    } else {
        pc = p ;
    }
}


// foreach loop control
Foreach::Foreach (VirtualMachine *v, Value *var, Value &val, int end, int p, InterpretedBlock *op) :
    vm (v), controlVar (var), value (val), endaddr (end), pc (p), opfunc (op) {
    workingType = value.type ;
    currindex = 0 ;
    direction = 1 ;
    switch (workingType) {
    case T_INTEGER:
    case T_CHAR:
    case T_BYTE:
        endindex = value.integer ;
        if (endindex < 0) {
            direction = -1 ;
        }
        break ;
    case T_STRING:
        endindex = value.str->size() ;
        break ;
    case T_VECTOR:
        endindex = value.vec->size() ;
        break ;
    case T_BYTEVECTOR:
        endindex = value.bytevec->size() ;
        break ;
    case T_MAP: {
        currmap = value.m->begin() ;
        endmap = value.m->end() ;
        Object *pairobj = new (pairClass->stacksize) Object (pairClass, NULL, NULL, NULL) ;
        pairobj->ref++ ;
        pairobj->varstack[0] = Value (pairobj) ;
        *controlVar = pairobj ;
        pairobj->ref-- ;
        break ;
        }
    case T_ENUM:
        endindex = value.en->consts.size() ;
        break ;
    case T_STREAM:
        break ;         // no setup required for streams
    case T_CLOSURE:
        break ;
    case T_OBJECT: 
        if (value.object != NULL) {             // no setup required for streams
            iterator = Value() ;
            break ;
        }
        // fall through
    default:
        vm->runtimeError ("Foreach cannot be applied to this type") ;
    }
}

Foreach::Foreach (VirtualMachine *v, Value *var, Value &val, int end, int p, InterpretedBlock *op, Value &endval) :
    vm (v), controlVar (var), value (val), endaddr (end), pc (p), opfunc (op) {
    workingType = value.type ;
    direction = 1 ;
    if (workingType == T_ENUMCONST) {
        currec = value.ec ;
        endec = endval.ec ;
        if (currec->en != endec->en) {
            vm->runtimeError ("Enumeration constants are not members of the same enumeration") ;
        }
        // lets work out if we are going up or down.  Can't rely on the value
        // of the enumeration const as they can be user-set.
        bool dirset = false ;
        EnumConst *ec = currec ;
        while (ec != NULL) {
            if (ec == endec) {
                dirset = true ;
                break ;
            }
            ec = ec->next ;
        }
        if (!dirset) {
            ec = currec ;
            while (ec != NULL) {
                if (ec == endec) {
                    direction = -1 ;
                    dirset = true ;
                    break ;
                }
                ec = ec->prev ;
            }
        }
        if (!dirset) {		// possible?
           vm->runtimeError ("Cannot determine direction for foreach on these enum consts") ;
        }
        endec = direction == 1 ? endec->next : endec->prev ;	// loop is exclusive of end
    } else {
        currindex = getInt (value) ;
        endindex = getInt (endval) ;
        if (currindex > endindex) {
            direction = -1 ;
        }
        endindex += direction ;		// include last index
    }
}

Foreach::~Foreach() {
}

void Foreach::next() {
    switch (workingType) {
    case T_INTEGER:
        if (currindex == endindex) {
            goto endforeach ;
        }
        *controlVar = currindex ;		// set control var
        currindex += direction ;
        break ;
    case T_CHAR:
        if (currindex == endindex) {
            goto endforeach ;
        }
        *controlVar = (char)currindex ;		// set control var
        currindex += direction ;
        break ;
    case T_BYTE:
        if (currindex == endindex) {
            goto endforeach ;
        }
        *controlVar = (unsigned char)currindex ;		// set control var
        currindex += direction ;
        break ;
    case T_STRING:
        if (currindex == endindex) {
            goto endforeach ;
        }
        *controlVar = (*value.str)[currindex] ;
        currindex++ ;
        break ;
    case T_VECTOR:
        if (currindex == endindex) {
            goto endforeach ;
        }
        *controlVar = (*value.vec)[currindex] ;
        currindex++ ;
        break ;
    case T_BYTEVECTOR:
        if (currindex == endindex) {
            goto endforeach ;
        }
        *controlVar = (*value.bytevec)[currindex] ;
        currindex++ ;
        break ;
    case T_MAP: {
        if (currmap == endmap) {
            goto endforeach ;
        }
        Object *pairobj = controlVar->object ;
        pairobj->varstack[1] = (*currmap).first ;
        pairobj->varstack[2] = (*currmap).second ;
        currmap++ ;
        break ;
        }
    case T_ENUM:
        if (currindex == endindex) {
            goto endforeach ;
        }
        *controlVar = value[currindex] ;
        currindex++ ;
        break ;
    case T_OBJECT: {
        if (value.object != NULL && opfunc != NULL) {
            Operand newval ;
            Value addr = &iterator ;
            vm->callFunction (&newval, opfunc, value, addr) ;
            if (iterator.type == T_NONE) {
                goto endforeach ;
            }
            *controlVar = newval.val ;
        }
        break ;
        }
    case T_ENUMCONST:
        if (currec == endec) {
            goto endforeach ;
        }
        *controlVar = currec ;
        currec = (direction == 1) ? currec->next : currec->prev ;
        break ;

    case T_STREAM:
        if (value.stream->eof()) {
            goto endforeach ;
        }
        if (value.stream->mode == Stream::LINEMODE) {
            string *line = new string() ;
            Value linev = line ;
            value.stream->readValue (linev) ;
            if (value.stream->eof()) {
                goto endforeach ;
            }
            *controlVar = linev ;
        } else {
            Value ch ((unsigned char)0) ;
            value.stream->readValue (ch) ;
            if (value.stream->eof()) {
                goto endforeach ;
            }
            *controlVar = ch ;
        }
        break ;
    case T_CLOSURE: {
        // this is coroutine iteration.  We need to restore the state of the vm to execute
        // the code after the yield instruction in the coroutine.  This will result in
        // another closure object with saved vm state, or if the function returns a non-
        // closure we have reached the end of the loop
        value = vm->invoke_coroutine (value.closure) ;
        if (value.type != T_CLOSURE) {
            goto endforeach ;
        }
        *controlVar = value.closure->value ;
        break ;
        }
    }
    return ;

endforeach: 
    pc = -1 ;			// stop the loop
}



// subscripting

inline bool VirtualMachine::subscriptok (const Value &v, int i) {
    switch (v.type) {
    case T_INTEGER:
        if (i < 0 || i > 63) {
            return false ;
        }
        break ;
    case T_CHAR:
    case T_BYTE:
        if (i < 0 || i > 7) {
            return false ;
        }
        break ;
    case T_STRING:
        if (i < 0 || i >= v.str->size()) {
            return false ;
        }
        break ;
    case T_VECTOR:
        if (i < 0 || i >= v.vec->size()) {
            return false ;
        }
        break ;
    case T_BYTEVECTOR:
        if (i < 0 || i >= v.bytevec->size()) {
            return false ;
        }
        break ;
    }
    return true ;

}


// single index subscripting
void VirtualMachine::addrsub (Operand *dest, Value &srcaddr, Value &index) {
    Value &src = getarg (srcaddr) ;
    if (src.type == T_MAP) {
        map::iterator s = src.m->find (index) ;
        if (s != src.m->end()) {
            set (dest, &(*s).second) ;
        } else {
            set (dest, &none) ;
        }
        return ;
    }

    if (isIntegral (index)) {
        int ilo = getInt (index) ;

        if (subscriptok (src, ilo)) {
            switch (src.type) {
            case T_VECTOR: 
                set (dest, (Value*)(&(*(src.vec->begin() + ilo)))) ;
                break ;

            default:
                // since parameters are passed by address we must allow a subscript to be used
                // as a function parameter.  The only value use of a reference to a subscript
                // is to a vector element so for all others we get the value of the subscript
                // and leave it up to other code to check that it is valid
                getsub (dest, srcaddr, index) ;		// all others, just get subscript value
                break ;
            }
        } else {
            runtimeError ("Subscript out of range: [%d]", ilo) ;
        }
    } else {
        runtimeError ("Illegal subscript of [] operator: [%s]", typestring (index).c_str()) ;
    }
}


void VirtualMachine::getsub (Operand *dest, Value &srcaddr, Value &index) {
    Value &src = getarg (srcaddr) ;
    InterpretedBlock *opfunc ;

    if (src.type == T_OBJECT && src.object != NULL && (opfunc = checkForOperator (src, LSQUARE)) != NULL) {
        callFunction (dest, opfunc, src, index) ;
        return ;
    }

    if (src.type == T_MAP) {
        set (dest, src[index]) ;
        return ;
    }

    if (isIntegral (index)) {
        int ilo = getInt (index) ;
        if (subscriptok (src, ilo)) {

            switch (src.type) {
            case T_INTEGER: case T_CHAR: case T_BYTE: {
                INTEGER v = (src.integer >> ilo) & 1 ;
                set (dest, v) ;
                break ;
                }
            case T_STRING: 
                set (dest, (char)(*src.str)[ilo]) ;
                break ;

            case T_VECTOR: 
                set (dest, src[ilo]) ;
                break ;

            case T_BYTEVECTOR: 
                set (dest, src[ilo]) ;
                break ;

            default:
                runtimeError ("Use of [] operator for %s is illegal", typestring (src).c_str()) ;
                break ;
            }
        } else {
            runtimeError ("Subscript out of range: [%d]", ilo) ;
        }
    } else {
        if (src.type == T_STRING) {
            if (index.type != T_STRING) {
                runtimeError ("String subscripts of type %s are illegal", typestring (index).c_str()) ;
            }
            RegExMap::iterator regex = regexMap.find (*index.str) ;
            OS::Regex *comp = NULL ;
            int err ;
            if (regex == regexMap.end()) {
                comp = new OS::Regex ;
                const char *str = index.str->c_str() ;
                int flags = REG_EXTENDED ;
#if 0
                if (str[0] == '~') {                    // ignore case?
                    str++ ;
                    flags |= REG_ICASE ;
                }
#endif
                if ((err = OS::regexCompile (comp, str, flags)) != 0) {
                    char buffer[1024] ;
                    OS::regexError (err, comp, buffer, 1024) ;
                    runtimeError ("Regular expression error: %s", buffer) ;
                }
                regexMap[*index.str] = comp ;
            } else {
               comp = (*regex).second ;
            }
            OS::RegexMatch match[10] ;
            Value::vector *vec = new Value::vector ;
            err = OS::regexExec (comp, src.str->c_str(), 10, match, 0) ;

            switch (err) {
            case 0:
                for (int i = 0 ; i < 10 ; i++) {
                    if (match[i].rm_so == -1) {
                        break ;
                    }
                    Object *regobj = new (regexClass->stacksize) Object (regexClass, NULL, NULL, NULL) ;
                    regobj->ref++ ;
                    regobj->varstack[0] = regobj ;
                    regobj->varstack[1] = match[i].rm_so ;
                    regobj->varstack[2] = match[i].rm_eo - 1 ;
                    vec->push_back (regobj) ;
                    regobj->ref-- ;
                }
                // fall thru
            case REG_NOMATCH:
                set (dest, vec) ;
                break ;
            }
        } else {
            set (dest, src[index]) ;
        }
    }
}

void VirtualMachine::setsub (Operand *dest, Value &val, Value &destaddr, Value &index) {
    Value &src = getarg (destaddr) ;
    if (src.type == T_MAP) {
        (*src.m)[index] = val ;
        set (dest, val) ;
        return ;
    }

    if (isIntegral (index)) {
        int ilo = getInt (index) ;

        if (subscriptok (src, ilo)) {

            switch (src.type) {
            case T_INTEGER: case T_CHAR: case T_BYTE: {
                INTEGER mask = (INTEGER)1 << ilo ;
                INTEGER oldv = src.integer & ~mask ;
                INTEGER newv = oldv | ((val.integer << ilo) & mask) ;
                *destaddr.addr = newv ;
                set (dest, newv) ;
                break ;
                }
            case T_STRING: 
                if (val.type != T_CHAR) {
                    runtimeError ("Char type required to assign to string subscript") ;
                } else {
                    src.str->setchar (ilo, static_cast<char>(val.integer)) ;
                    set (dest, src) ;
                }
                break ;

            case T_VECTOR: 
                (*src.vec)[ilo] = val ;
                set (dest, src) ;
                break ;

            case T_BYTEVECTOR: 
                if (!isIntegral (val)) {
                    runtimeError ("Integral type required to assign to byte vector subscript") ;
                }
                (*src.bytevec)[ilo] = getInt (val) ;
                set (dest, src) ;
                break ;

            default:
                runtimeError ("Use of [] operator for %s is illegal", typestring (src).c_str()) ;
                break ;
            }
        } else {
            runtimeError ("Subscript out of range: [%d]", ilo) ;
        }
    } else {
        runtimeError ("Illegal subscript type %s", typestring (index).c_str()) ;
    }
}

void VirtualMachine::delsub (Value &srcaddr, Value &index) {
    Value &src = getarg (srcaddr) ;
    if (src.type == T_MAP) {
        map::iterator s = src.m->find (index) ;
        if (s != src.m->end()) {
            src.m->erase (s) ;
        }
        return ;
    }
    if (isIntegral (index)) {
        int ilo = getInt (index) ;
        if (subscriptok (src, ilo)) {
            switch (src.type) {
            case T_INTEGER: case T_CHAR: case T_BYTE: {
                runtimeError ("Can't delete bits in an integer") ;
                break ;
                }
            case T_STRING: {
                src.str->erase (ilo, ilo) ;
                break ;
                }
            case T_VECTOR: {
                Value::vector::iterator s = src.vec->begin() ;
                Value::vector::iterator e ;
                s += ilo ;
                e = s + 1 ;			// XXX: check this
                src.vec->erase (s, e) ;            // should call destuctors for values
                break ;
                }
            case T_BYTEVECTOR: {
                Value::bytevector::iterator s = src.bytevec->begin() ;
                Value::bytevector::iterator e ;
                s += ilo ;
                e = s + 1 ;			// XXX: check this
                src.bytevec->erase (s, e) ;
                break ;
                }
            default:
                runtimeError ("Use of delete [] operator for %s is illegal", typestring (src).c_str()) ;
                break ;
            }
       } else {
            runtimeError ("Subscript out of range: [%d]", ilo) ;
       }
    } else {
        runtimeError ("Illegal delete subscript type %s", typestring (index).c_str()) ;

    }
}



// 2 index subscripting


// Note: there is no 2-index version of addrsub as it would have to create a temporary
// vector and return the address of it.  This is not possible.  Anyway, it is not
// needed anyway as getsub will do the same thing.

void VirtualMachine::getsub (Operand *dest, Value &srcaddr, Value &lo, Value &hi) {
    Value &src = getarg (srcaddr) ;
    InterpretedBlock *opfunc ;

    if (src.type == T_OBJECT && src.object != NULL && (opfunc = checkForOperator (src, LSQUARE)) != NULL) {
        callFunction (dest, opfunc, src, lo, hi) ;
        return ;
    }

    if (src.type == T_MAP) {
        runtimeError ("Maps do not support range subscripts") ;
    }

    if (isIntegral (lo) && isIntegral (hi)) {
        int ilo = getInt (lo) ;
        int ihi = getInt (hi) ;
        int olo = ilo ;			// original values (for error message)
        int ohi = ihi ;
        if (ilo > ihi) {
            INTEGER tmp = ilo ;
            ilo = ihi ;
            ihi = tmp ;
        }

        if (subscriptok (src, ilo) && subscriptok (src, ihi)) {
            INTEGER range = ihi - ilo + 1 ;                     // number of bits

            switch (src.type) {
            case T_INTEGER: case T_CHAR: case T_BYTE: {
                INTEGER mask = ((INTEGER)1 << range) - 1 ;
                INTEGER v = (src.integer >> ilo) & mask ;
                set (dest, v) ;
                break ;
                }
            case T_STRING: {
                string *s = new string (src.str->substr (ilo, range)) ;
                set (dest, s) ;
                break ;
                }

            case T_VECTOR: {
                Value::vector *v = new Value::vector ;
                for (int i = ilo ; i <= ihi ; i++) {
                    v->push_back (src[i]) ;
                }
                set (dest, v) ;
                break ;
                }

            case T_BYTEVECTOR: {
                Value::bytevector *v = new Value::bytevector ;
                for (int i = ilo ; i <= ihi ; i++) {
                    v->push_back (src[i]) ;
                }
                set (dest, v) ;
                break ;
                }

            default:
                runtimeError ("Use of [] operator for %s is illegal", typestring (src).c_str()) ;
                break ;
            }
        } else {
            runtimeError ("Subscript out of range: [%d:%d]", olo, ohi) ;
        }
    } else {
        runtimeError ("Illegal range subscript with types [%s:%s]", typestring (lo).c_str(), typestring (hi).c_str()) ;
    }
}

void VirtualMachine::setsub (Operand *dest, Value &val, Value &destaddr, Value &lo, Value &hi) {
    Value &src = getarg(destaddr) ;
    if (src.type == T_MAP) {
        runtimeError ("Maps do not support range subscripts") ;
    }

    if (isIntegral (lo) && isIntegral (hi)) {
        int ilo = getInt (lo) ;
        int ihi = getInt (hi) ;
        int olo = ilo ;			// original values (for error message)
        int ohi = ihi ;
        if (ilo > ihi) {
            INTEGER tmp = ilo ;
            ilo = ihi ;
            ihi = tmp ;
        }

        if (subscriptok (src, ilo) && subscriptok (src, ihi)) {
            INTEGER range = ihi - ilo + 1 ;                     // number of bits

            switch (src.type) {
            case T_INTEGER: case T_CHAR: case T_BYTE: {
                INTEGER mask = (((INTEGER)1 << range) - 1) << ilo ;
                INTEGER oldv = src.integer & ~mask ;
                INTEGER newv = oldv | ((val.integer << ilo) & mask) ;
                *destaddr.addr = newv ;
                set (dest, newv) ;
                break ;
                }
            case T_STRING: 
                if (ilo == ihi) {
                    if (val.type != T_CHAR) {
                        runtimeError ("Char type required to assign to string subscript") ;
                    } else {
                        src.str->setchar (ilo, static_cast<char>(val.integer)) ;
                        set (dest, src) ;
                    }
                } else {
                    if (val.type != T_STRING) {
                        runtimeError ("String type required for string subscript assignment") ;
                    } else {
                        string *s = new string (src.str->substr (ilo, range)) ;
                        *destaddr.addr = s ;
                        set (dest, s) ;
                    }
                }
                break ;

            case T_VECTOR: 
                if (val.type == T_VECTOR) {
                    if (val.vec->size() != range) {
                        runtimeError ("Vector size mismatch for vector subscript assignment: %d/%d", val.vec->size(), range) ;
                    } else {
                        for (int i = ilo ; i <= ihi ; i++) {
                            (*src.vec)[i] = (*val.vec)[i] ;
                        }
                    }
                } else {
                    for (int i = ilo ; i <= ihi ; i++) {
                        (*src.vec)[i] = val ;
                    }
                }
                set (dest, src) ;
                break ;

            case T_BYTEVECTOR: 
                if (val.type == T_BYTEVECTOR) {
                    if (val.bytevec->size() != range) {
                        runtimeError ("Vector size mismatch for vector subscript assignment: %d/%d", val.bytevec->size(), range) ;
                    } else {
                        for (int i = ilo ; i <= ihi ; i++) {
                            (*src.bytevec)[i] = (*val.bytevec)[i] ;
                        }
                    }
                } else {
                    for (int i = ilo ; i <= ihi ; i++) {
                        (*src.bytevec)[i] = val ;
                    }
                }
                set (dest, src) ;
                break ;

            default:
                runtimeError ("Use of [] operator for %s is illegal", typestring (src).c_str()) ;
                break ;
            }
        } else {
            runtimeError ("Subscript out of range: [%d:%d]", olo, ohi) ;
        }
    } else {
        runtimeError ("Illegal range subscript with types [%s:%s]", typestring (lo).c_str(), typestring (hi).c_str()) ;
    }
}


//
// make a new vector of given number of dimensions.  All elements are type none
//

void VirtualMachine::newVector (Operand *dest, int index, int ndims) {
    Value &dimsize = stack[index] ;
    if (!isIntegral (dimsize)) {
        runtimeError ("new [] requires an integral size") ;
    }
    int n = getInt (dimsize) ;
    if (n < 0) {
        runtimeError ("Invalid size value: new [%d]", n) ;
    }
    Value::vector *v = new Value::vector (n) ;
    for (int i = 0 ; i < n ; i++) {
        (*v)[i] = Value() ;
    }
    set (dest, v) ;
    if (ndims > 1) {
       for (int i = 0 ; i < n ; i++) {
           Operand tmp ;
           newVector (&tmp, index - 1, ndims - 1) ;
           (*v)[i] = tmp.val ;
       }
    }
}

//
// make a new vector with ndims dimensions.  Each element is constructed by the code
// addressed by ctstart..ctend
//

void VirtualMachine::newVector (Operand *dest, int index, int ndims, int ctstart, int ctend, Value &firstelement) {
    Value &dimsize = stack[index] ;
    if (!isIntegral (dimsize)) {
        runtimeError ("new [] requires an integral size") ;
    }
    int n = getInt (dimsize) ;
    if (n < 0) {
        runtimeError ("Invalid size value: new [%d]", n) ;
    }
    Value::vector *v = new Value::vector (n) ;
    for (int i = 0 ; i < n ; i++) {
        (*v)[i] = Value() ;
    }
    set (dest, v) ;
    if (ndims > 1) {
       for (int i = 0 ; i < n ; i++) {
           Operand tmp ;
           newVector (&tmp, index - 1, ndims - 1, ctstart, ctend, firstelement) ;
           (*v)[i] = tmp.val ;
       }
    } else if (ndims == 1) {
        if (ctstart != 0) {			// any constructor?
            if (n > 0) {
                (*v)[0] = firstelement ;
                for (int i = 1 ; i < n ; i++) {
                    execute (ctstart) ;
                    (*v)[i] = retval ;
                }
                retval.destruct() ;
            }
        }
    }
}

void VirtualMachine::newByteVector (Operand *dest, int index, int ndims, int ctstart, int ctend, unsigned char firstelement) {
    Value &dimsize = stack[index] ;
    if (!isIntegral (dimsize)) {
        runtimeError ("new [] requires an integral size") ;
    }
    int n = getInt (dimsize) ;
    if (ndims > 1) {
        Value::vector *v = new Value::vector (n) ;
        for (int i = 0 ; i < n ; i++) {
            (*v)[i] = Value() ;
        }
        set (dest, v) ;
        for (int i = 0 ; i < n ; i++) {
            Operand tmp ;
            newByteVector (&tmp, index - 1, ndims - 1, ctstart, ctend, firstelement) ;
            (*v)[i] = tmp.val ;
        }
    } else if (ndims == 1) {
        Value::bytevector *v = new Value::bytevector (n) ;
        set (dest, v) ;
        memset (&(*(v->begin())), 0, v->size()) ;
        if (ctstart != 0) {			// any constructor?
            if (n > 0) {
                (*v)[0] = firstelement ;
                for (int i = 1 ; i < n ; i++) {
                    execute (ctstart) ;
                    (*v)[i] = retval.integer ;
                }
                retval.destruct() ;
            }
        }
    }
}


void VirtualMachine::dodelete (Value &addr) {
    if (aikido->properties & LOCKDOWN) {        // don't delete things in lockdown mode as it can cause crashes
        return ;
    }
    Value *val = addr.addr ;
    val->destruct() ;
}

void VirtualMachine::delsub (Value &srcaddr, Value &lo, Value &hi) {
    Value &src = getarg (srcaddr) ;
    if (src.type == T_MAP) {
        runtimeError ("Maps do not support range subscripts") ;
    }
    if (isIntegral (lo) && isIntegral (hi)) {
        int ilo = getInt (lo) ;
        int ihi = getInt (hi) ;
        int width = ihi - ilo + 1;
        if (subscriptok (src, ilo) && subscriptok (src, ihi)) {
            switch (src.type) {
            case T_INTEGER: case T_CHAR: case T_BYTE: {
                runtimeError ("Can't delete bits in an integer") ;
                break ;
                }
            case T_STRING: {
                src.str->erase (ilo, ihi) ;
                break ;
                }
            case T_VECTOR: {
                Value::vector::iterator s = src.vec->begin() ;
                Value::vector::iterator e ;
                s += ilo ;
                e = s + width ;
                src.vec->erase (s, e) ;            // should call destuctors for values
                break ;
                }
            case T_BYTEVECTOR: {
                Value::bytevector::iterator s = src.bytevec->begin() ;
                Value::bytevector::iterator e ;
                s += ilo ;
                e = s + width ;
                src.bytevec->erase (s, e) ;            // should call destuctors for values
                break ;
                }
            default:
                runtimeError ("Use of delete [] operator for %s is illegal", typestring (src).c_str()) ;
                break ;
            }
       } else {
            runtimeError ("Subscript out of range: [%d:%d]", ilo, ihi) ;
       }
    } else {
        runtimeError ("Illegal delete [] range subscript with types [%s:%s]", typestring (lo).c_str(), typestring (hi).c_str()) ;

    }
}


//
// make an enumeration
//

void VirtualMachine::doenum (Value &en) {
    execute (en.en, frame, staticLink, 0) ;
}

//
// delegate execution to a worker
// NOTE: any reference to currentContext needs to be locked
//
void VirtualMachine::delegate (Value &s) {
    if (aikido->worker->isLoopback()) {
        if (parserDelegate == NULL) {
            runtimeError ("Unrecognized foreign command sequence");
        }
    }
    parserLock.acquire(true) ;
    Context ctx (ir->source, *s.stream, aikido->currentContext) ;
    aikido->currentContext = &ctx ;
    std::istream &currentStream = aikido->currentContext->getStream();
    int pass = aikido->currentPass ;
    parserLock.release(true) ;
    
    ctx.rewind() ;
    incRef (frame, stackframe) ;
    //std::cout << "delegation starting at line " << ctx.getLineNumber() << '\n' ;
    try {
        aikido->worker->parse(currentStream, frame, staticLink, scopeStack[scopeSP - 1], scopeStack[scopeSP - 1]->majorScope->level, ir->source, output.stream->getOutStream(), input.stream->getInStream(), &ctx, pass) ;
    } catch (...) {
        parserLock.acquire(true) ;
        aikido->currentContext = ctx.getPrevious() ;
        parserLock.release(true) ;
        throw ;
    }
    parserLock.acquire(true) ;
    aikido->currentContext = ctx.getPrevious() ;
    parserLock.release(true) ;
    decRef (frame, stackframe) ;
}

//
// execute an inline block
//
void VirtualMachine::doinline(Operand *dest, Operand *endop) {
    Value oldoutput = output ;
    std::stringstream *buffer = new std::stringstream ;     // will be garbage collected
    output = new StdStream (buffer) ;          // redirect output to buffer

    try {
        execute (pc) ;

        // the buffer stream will contain the output from the inline block.  Split
        // it into a vector of lines
        Value::vector *vec = new Value::vector() ;
        std::string::size_type i, oldi = 0 ;
        std::string str = buffer->str() ;
        do {
            i = str.find ('\n', oldi) ;
            if (i != std::string::npos) {
                string *s = new string (str.substr (oldi, i - oldi)) ;
                *s += '\n' ;                        // add line feed
                vec->push_back (s) ;
                oldi = str.find_first_not_of ('\n', i) ;
                if (oldi == std::string::npos) {
                    break ;
                }
            } else {
                string *s = new string (str.substr (oldi)) ;
                vec->push_back (s) ;
            }
        } while (i != std::string::npos) ;
        set (dest, vec) ;
        retval.destruct() ;
        pc = endop->val.integer ;			// jump to after handler
        output = oldoutput ;
    } catch (...) {
        output = oldoutput ;
        throw ;
    }
}


//
// raw native function calling
//

typedef INTEGER (*FUNC)(...) ;

INTEGER callRawNative (FUNC f, int nparas, void **paras) {
    bool is32 = sizeof(long) == 4;
    INTEGER mask = is32 ? 0xffffffffLL : 0xffffffffffffffffLL;
    switch (nparas) {
    case 0:
        return f() & mask ;
    case 1:
        return f(paras[0])  & mask;
    case 2:
        return f(paras[0], paras[1])  & mask;
    case 3:
        return f(paras[0], paras[1], paras[2])  & mask;
    case 4:
        return f(paras[0], paras[1], paras[2], paras[3])  & mask;
    case 5:
        return f(paras[0], paras[1], paras[2], paras[3], paras[4])  & mask;
    case 6:
        return f(paras[0], paras[1], paras[2], paras[3], paras[4], paras[5])  & mask;
    case 7:
        return f(paras[0], paras[1], paras[2], paras[3], paras[4], paras[5], paras[6])  & mask;
    case 8:
        return f(paras[0], paras[1], paras[2], paras[3], paras[4], paras[5], paras[6], paras[7])  & mask;
    case 9:
        return f(paras[0], paras[1], paras[2], paras[3], paras[4], paras[5], paras[6], paras[7], paras[8])  & mask;
    case 10:
        return f(paras[0], paras[1], paras[2], paras[3], paras[4], paras[5], paras[6], paras[7], paras[8], paras[9])  & mask;
    case 11:
        return f(paras[0], paras[1], paras[2], paras[3], paras[4], paras[5], paras[6], paras[7], paras[8], paras[9], paras[10])  & mask;
    case 12:
    case -1:
        return f(paras[0], paras[1], paras[2], paras[3], paras[4], paras[5], paras[6], paras[7], paras[8], paras[9], paras[10], paras[11])  & mask;
    default:
        return 0 ;
    }
}

void RawNativeFunction::call (Value *dest, VirtualMachine *vm, StackFrame *stack, StaticLink *staticLink, Scope *currentScope, int currentScopeLevel, const ValueVec &parameters) {
    void *paras[10] ;			// Needs to match number of case clauses in callRawNative
    int np = nparas ;			// number of parameters passed to raw native
    int nactuals = nparas ;		// number of actual parameters
    int p = 0 ;				// current actual number

    if (vm->aikido->properties & LOCKDOWN) {
        vm->runtimeError ("Security Violation") ;
    }
    if (np > 12) {
        vm->runtimeError ("Too many parameters for raw native function") ;
    }
    if (!(flags & BLOCK_STATIC) && parentClass != NULL) {
        np-- ;
        nactuals-- ;
    }
    if (nparas < 0) {		// variable arg function
        nactuals = 12 ;
        np = 12 ;
    }
    for (int i = 0 ; i < nactuals  && i < parameters.size(); i++, p++) {
        const Value &v = parameters[i] ;
        switch (v.type) {
        case T_INTEGER:
        case T_CHAR:
        case T_BYTE:
            paras[p] = reinterpret_cast<void*>(v.integer) ;
            break ;
        case T_ENUMCONST:
            paras[p] = reinterpret_cast<void*>(v.ec->value) ;
            break ;
        case T_STRING:
            paras[p] = (void*)v.str->c_str() ;
            break ;
        case T_NONE:
            break ;
        case T_BYTEVECTOR:
            paras[p] = (void*)&*v.bytevec->begin() ;
            break ;
        case T_ADDRESS:
            paras[p] = (void*)v.addr ;
            break ;
        case T_MEMORY:
            paras[p] = (void*)v.mem->mem ;
            break ;
        case T_POINTER:
            paras[p] = (void*)(v.pointer->mem->mem + v.pointer->offset) ;
            break ;
        case T_OBJECT:
            if (v.object == NULL) {
                paras[p]= NULL ;
                break ;
            } else {
                Scope *scope ;
                Variable *var = v.object->block->findVariable (string(".toNative"), scope, VAR_PUBLIC, vm->ir->source, NULL) ;
                if (var != NULL) {
                    Tag *tag = (Tag*)var ;
                    Operand tmp ;
                    vm->callFunction (&tmp, tag->block, v.object) ;
                    switch (tmp.val.type) {
                    case T_INTEGER:
                    case T_CHAR:
                    case T_BYTE:
                        paras[p] = reinterpret_cast<void*>(tmp.val.integer) ;
                        continue ;
                    case T_ENUMCONST:
                        paras[p] = reinterpret_cast<void*>(tmp.val.ec->value) ;
                        continue ;
                    case T_STRING:
                        paras[p] = (void*)tmp.val.str->c_str() ;
                        continue ;
                    case T_NONE:
                        continue ;
                    case T_ADDRESS:
                        paras[p] = (void*)tmp.val.addr ;
                        continue ;
                    case T_MEMORY:
                        paras[p] = (void*)tmp.val.mem->mem ;
                        continue ;
                    case T_POINTER:
                        paras[p] = (void*)(tmp.val.pointer->mem->mem + v.pointer->offset) ;
                        continue ;
                    }
                }
            }
            // must fall through here
        default:
            if (nparas > 0) {
                vm->runtimeError ("Illegal parameter type for raw native function parameter #%d", p) ;
            }
        }
    }
    *dest = callRawNative ((FUNC)ptr, np, paras) ;
}

#ifdef JAVA
enum Score {
    NOMATCH,
    POSSIBLEMATCH,
    GOODMATCH,
    EXACTMATCH
} ;

struct ArgMatch {
    Type type ;
    struct JavaType {
        const char t ;
        Score score ;
    } javaType[10] ;
} argMatch[] = {
    { T_INTEGER, {
        { 'I', EXACTMATCH},
        { 'B', POSSIBLEMATCH},
        { 'C', POSSIBLEMATCH},
        { 'J', POSSIBLEMATCH},
        { 'F', POSSIBLEMATCH},
        { 'D', POSSIBLEMATCH},
        { 'S', EXACTMATCH},
        { 'Z', EXACTMATCH}}
    },
    { T_CHAR, {
        { 'I', POSSIBLEMATCH},
        { 'B', POSSIBLEMATCH},
        { 'C', EXACTMATCH},
        { 'J', POSSIBLEMATCH},
        { 'F', POSSIBLEMATCH},
        { 'D', POSSIBLEMATCH},
        { 'S', POSSIBLEMATCH}}
    },
    { T_BYTE, {
        { 'I', POSSIBLEMATCH},
        { 'B', EXACTMATCH},
        { 'C', POSSIBLEMATCH},
        { 'J', POSSIBLEMATCH},
        { 'F', POSSIBLEMATCH},
        { 'D', POSSIBLEMATCH},
        { 'S', POSSIBLEMATCH}}
    },
    { T_REAL, {
        { 'I', POSSIBLEMATCH},
        { 'B', POSSIBLEMATCH},
        { 'C', POSSIBLEMATCH},
        { 'J', POSSIBLEMATCH},
        { 'F', EXACTMATCH},
        { 'D', EXACTMATCH},
        { 'S', POSSIBLEMATCH}}
    },
    { T_OBJECT, {
        { 'L', EXACTMATCH}}
    },
    { T_VECTOR, {
        { '[', EXACTMATCH}}
    },
    { T_NONE}
} ;

static const char *nextarg (const char *args) {
    if (*args == 'L') {			// object
         args++ ;
         while (*args != ';') args++ ;
         args++ ;
    } else if (*args == '[') {		// array
         args++ ;
         while (*args == '[') args++ ;
         args++ ;
         if (*args == 'L') {		// array of objects
              args++ ;
              while (*args != ';') args++ ;
              args++ ;
         } else {
              args++ ;
         }
    } else {
         args++ ;
    }
    return args ;
}


void VirtualMachine::calljava (Operand *dest, Operand *args, int noverloads) {
    Value::vector &argv = *get (args).vec ;
    const int MAX_OVERLOAD = 100 ;
    const char *formalarg[MAX_OVERLOAD] ;
    InterpretedBlock *blocks[MAX_OVERLOAD] ;
    Score scores[MAX_OVERLOAD] ;

    // pop the overloads off the stack and initialize the arrays
    for (int i = 0 ; i < noverloads ; i++) {
        --sp ;
        blocks[i] = (InterpretedBlock*)stack[sp].block ;
        const char *ch = blocks[i]->name.c_str() ;
        while (*ch != 0 && *ch != '(') ch++ ;		// skip to args
        if (*ch == 0) {
            runtimeError ("Bad Java method signature %s", blocks[i]->name.c_str()) ;
        }
        ch++ ;			// skip to first arg
        formalarg[i] = ch ;
        //std::cout << "Method: " << ch << '\n' ;
        scores[i] = NOMATCH ;
    }

    // pop the this ptr
    Value thisptr = stack[--sp] ;

    // calculate the scores for the overloads
    for (int a = 0 ; a < argv.size() ; a++) {		// foreach arg 
        for (int i = 0 ; i < noverloads ; i++) {	// foreach overload
            if (*formalarg[i] == ')') {			// end of formal args?
                scores[i] = NOMATCH ;			// no match 
                continue ;
            }
            for (int t = 0 ; argMatch[t].type != T_NONE ; t++) {
                scores[i] = NOMATCH ;			// assume no match
                if (argMatch[t].type == argv[a].type) {
                    for (int j = 0 ; argMatch[t].javaType[j].t != (const char)0 ; j++) {
                        if (argMatch[t].javaType[j].t == *formalarg[i]) {
                            scores[i] = argMatch[t].javaType[j].score ;
                            break ;
                        }
                    }
                    break ;
                } 
            }
            // go to next arg
            formalarg[i] = nextarg (formalarg[i]) ;
        }
    }

    // eliminate overloads with more formals than actuals
    for (int i = 0 ; i < noverloads ; i++) {	// foreach overload
        if (*formalarg[i] != ')') {
            scores[i] = NOMATCH ;
        }
    }

    int best ;
    int nfound = 0 ;
    for (int i = 0 ; i < noverloads ; i++) {	// foreach overload
        if (scores[i] == EXACTMATCH) {
             nfound++ ;
             best = i ;
        }
    }
    if (nfound == 0) {		// no exact found, look for a possible match
         for (int i = 0 ; i < noverloads ; i++) {	// foreach overload
            if (scores[i] == POSSIBLEMATCH) {
                 nfound++ ;
                 best = i ;
            }
         }
        if (nfound == 0) {
            runtimeError ("Can't find match for Java call") ;
        }
    } 

    if (nfound > 1) {
       runtimeError ("Ambiguous call to Java function set") ;
    }

    // if we get here, best is the index of the best overload
    
    // push the args
    for (int i = argv.size() - 1 ; i >= 0 ; i--) {		// foreach arg 
        stack[sp++] = argv[i] ;
    }
    stack[sp++] = thisptr ;

    // now invoke the appropriate function
    callFunction (dest, argv.size() + 1, static_cast<Function*>(blocks[best])) ;
}

//
// invoke a native function
//

void VirtualMachine::calljni (Operand *dest, int nargs, void *code) {
    java::Method *method = (java::Method*)code ;
    method->findNativeCode() ;
    if (method->nativecode == NULL) {
        runtimeError ("Unable to find Java native method %s", method->name->bytes) ;
    }

    // we have the code to call.  Make an array of words for the equivalent
    // of the java stack

    int *javastack = new int [method->argSize / 4] ;
    const char *descriptor = method->descriptor->bytes ;

    int argstart = 0 ;
    if (!(method->accessFlags & java::ACC_STATIC)) {
        argstart = 1 ;
        Value &val = stack[--sp] ;
        javastack[0] = (int)val.object ;
    }
    for (int i = argstart ; i < nargs ; i++) {
        Value &val = stack[--sp] ;
        switch (*descriptor) {
        case 'B':
        case 'C':
        case 'I':
        case 'S':
        case 'Z':
             javastack[i] = val.integer & 0xffffffff ;
             break ;
        case 'J':
             javastack[i] = val.integer >> 32 ;
             javastack[i+1] = val.integer & 0xffffffff ;
             i++ ;
             break ;
        case 'F': {
             float f = (float)val.real ;		// narrow to float
             javastack[i] = *(float*)&f ;
             break ;
             }
        case 'D':
             javastack[i] = val.integer >> 32 ;			// treat as integer
             javastack[i+1] = val.integer & 0xffffffff ;
             i++ ;
             break ;
        case 'L':
             javastack[i] = (int)val.object ;
             break ;
        case '[':
             javastack[i] = (int)val.vec ;
             break ;
        }
        descriptor = nextarg (descriptor) ;
    }

    java::Class *cls = method->accessFlags & java::ACC_STATIC ? method->cls : NULL ;
    int r = sysInvokeNative (java::jni_NativeInterface, method->nativecode, javastack, 
                             method->descriptor->bytes, nargs, cls) ;

    descriptor++ ;		// skip ) in descriptor
    switch (*descriptor) {
    case 'B':
    case 'C':
    case 'I':
    case 'S':
    case 'Z':
         set (dest, Value ((INTEGER)javastack[0])) ;
         break ;
    case 'L':
         set (dest, Value ((Object*)javastack[0])) ;
         break ;
    case '[':
         set (dest, Value ((Value::vector*)javastack[0])) ;
         break ;
    case 'J': {
         INTEGER v = *(INTEGER*)&javastack[0] ;
         set (dest, Value (v)) ;
         break ;
         }
    case 'F': {
         float f = *(float*)&javastack[0] ;		// take first element
         set (dest, Value (f)) ;
         break ;
         }
    case 'D': {
         double f = *(double*)&javastack[0] ;		// take first 2 elements
         set (dest, Value (f)) ;
         break ;
         }
    }
    delete [] javastack ;
    
}

#endif

void VirtualMachine::instanceof (Operand *dest, Operand *obj, Operand *cls) {
    const Value &val = get (obj) ;
    const Value &c = get (cls) ;
    if (val.type != T_OBJECT) {
        set (dest, Value ((INTEGER)0)) ;
        return ;
    }
    if (val.object == NULL) {
        set (dest, Value ((INTEGER)0)) ;
        return ;
    }

    switch (c.type) {
    case T_CLASS:
    case T_CLOSURE:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD:
    case T_ENUM:
        break ;
    default:
        set (dest, Value ((INTEGER)0)) ;
        return ;
    } 
    Block *blk = val.object->block ;
    set (dest, Value ((INTEGER)blk->isSubclassOrImplements(c.block))) ;
}

#ifdef JAVA
// XXX: this should behave differently

void VirtualMachine::checkcast (Operand *dest, Operand *cls, Operand *obj) {
    const Value &val = get (obj) ;
    const Value &c = get (cls) ;
    if (val.type != T_OBJECT) {
        set (dest, Value ((INTEGER)0)) ;
        return ;
    }
    if (val.object == NULL) {
        set (dest, Value ((INTEGER)0)) ;
        return ;
    }

    switch (c.type) {
    case T_CLASS:
    case T_CLOSURE:
    case T_INTERFACE:
    case T_MONITOR:
    case T_FUNCTION:
    case T_PACKAGE:
    case T_THREAD:
    case T_ENUM:
        break ;
    default:
        set (dest, Value ((INTEGER)0)) ;
        return ;
    } 
    Block *blk = val.object->block ;
    set (dest, Value ((INTEGER)blk->isSubclassOrImplements(c.block))) ;
}

void VirtualMachine::realcompare (Operand *dest, Operand *v1, Operand *v2, int nanflag) {
}

// for the switch instructions, pc is the address of the branch instruction after
// the switch instruction

void VirtualMachine::tableswitch (Operand *value, int lo, int hi) {
    int v = get (value) ;
    if (v >= lo || v <= hi) {
        int index = v - lo ;
        pc += index + 1 ;
    }
    // fall through to default branch instruction
}

void VirtualMachine::lookupswitch (Operand *value, int npairs) {
    int v = get (value) ;
    int i = pc + 1 ;			// index of first NOP
    while (npairs-- > 0) {
        Instruction *inst = static_cast<Instruction*>(currentCode->code.begin() + i) ;
        int val = get (inst->src[0]) ;
        if (v == val) {
            pc = i + 1 ;
            return ;
        }
        i += 2 ;
    }
    // fall through to default branch
}
#endif

void VirtualMachine::monitorEnter (Operand *obj) {
    Value &val = get (obj) ;
    if (val.type != T_OBJECT) {
        runtimeError ("Cannot get a monitor for a value of this type") ;
    }
    if (val.object == NULL) {
        runtimeError ("Cannot get a monitor for a NULL object") ;
    }
    Monitor *mon = aikido->findMonitor (val.object) ;
    mon->acquire (true) ;
}

void VirtualMachine::monitorExit (Operand *obj) {
    Value &val = get (obj) ;
    if (val.type != T_OBJECT) {
        runtimeError ("Cannot get a monitor for a value of this type") ;
    }
    if (val.object == NULL) {
        runtimeError ("Cannot get a monitor for a NULL object") ;
    }
    parserLock.acquire(true) ;
    Monitor *mon = aikido->findMonitor (val.object) ;
    mon->release (true) ;
    parserLock.release(true) ;
}


//
// in operator
//
void VirtualMachine::in (Operand *dest, Operand *src1, Operand *src2) {
    Value &v1 = get (src1) ;
    Value &v2 = get (src2) ;

    switch (v2.type) {
    case T_VECTOR: {
        if (v1.type == T_VECTOR) {
            Value::vector tmp = *v1.vec & *v2.vec ;
            set (dest, Value ((INTEGER)(tmp.size() != 0))) ;
        } else {
            for (int i = 0 ; i < v2.vec->size() ; i++) {
                if (cmpeq (v1, (*v2.vec)[i])) {
                    set (dest, 1) ;
                    return ;
                }
            }
            set (dest, 0) ;
        }
        break ;
        }
    case T_BYTEVECTOR: {
        break ;
        }
    case T_MAP: {
        map::iterator i = v2.m->find (v1) ;
        set (dest, !(i == v2.m->end())) ;
        break ;
        }
    case T_STRING: {
        std::string::size_type i ;
        if (v1.type == T_STRING) {
            i = v2.str->find (v1.str->c_str()) ;
            set (dest, i != std::string::npos) ;
        } else if (v1.type == T_CHAR) {
            i = v2.str->find ((char)v1.integer) ;
            set (dest, i != std::string::npos) ;
        } else {
            runtimeError ("Illegal use of 'in' operator for string, type %s cannot be in a string", typestring (v1).c_str()) ;
        }
        break ;
        }
    case T_OBJECT: {
        if (v2.object == NULL) {
            set (dest, 0) ;
            return ;
        }

        InterpretedBlock *opfunc ;
        if ((opfunc = checkForOperator (v2, TIN)) != NULL) {
            callFunction (dest, opfunc, v2, v1) ;
        } else {
            if (v1.type != T_STRING) {
                runtimeError ("Illegal use of 'in' operator for block, can only use strings to name block members, not %s", typestring (v1).c_str()) ;
            }
            Scope *scope ;
            Tag *tag = (Tag*)v2.object->block->findVariable (string(".") + *v1.str, scope, VAR_ACCESSFULL, NULL, NULL) ;
            set (dest, tag != NULL) ;
        }
        break ;
        }
    case T_CLASS:
    case T_FUNCTION:
    case T_MONITOR:
    case T_THREAD:
    case T_CLOSURE:
    case T_PACKAGE: {
        if (v1.type != T_STRING) {
            runtimeError ("Illegal use of 'in' operator for block, can only use strings to name block members, not %s", typestring (v1).c_str()) ;
        }
        Scope *scope ;
        Block *block = v2.type == T_CLOSURE ? v2.closure->block : v2.block ;
        Variable *v = block->findVariable (*v1.str, scope, VAR_ACCESSFULL, NULL, NULL) ;

        set (dest, v != NULL) ;
        break ;
        }
    default:
        runtimeError ("Illegal use of 'in' operator on type: %s", typestring (v2).c_str()) ;
    }
}

void VirtualMachine::inrange (Operand *dest, Operand *src1, Operand *src2, Operand *src3) {
    Value &v1 = get (src1) ;
    Value &v2 = get (src2) ;
    Value &v3 = get (src3) ;

    if (isIntegral (v2)) {
        if (!isIntegral (v3)) {
            goto illegalrange ;
        }
        if (!isIntegral (v1)) {
            goto illegalrange ;
        }
        INTEGER i = getInt (v1) ;
        INTEGER i1 = getInt (v2) ;
        INTEGER i2 = getInt (v3) ;
        if (i1 > i2) {
            INTEGER tmp = i1 ;
            i1 = i2 ;
            i2 = tmp ;
        }
        set (dest, i >= i1 && i <= i2) ;
        return ;
    } else if (v1.type == T_OBJECT) {
        if (v1.object == NULL) {
            runtimeError ("Reference through a null pointer") ;
        }

        InterpretedBlock *opfunc ;
        if ((opfunc = checkForOperator (v1, TIN)) != NULL) {
            callFunction (dest, opfunc, v1, v2, v3) ;
            return ;
        } else {
            runtimeError ("No overload of 'operator in' found for this object") ;
        }
    }
    // XXX other types (T_REAL?)

    illegalrange:
       runtimeError ("Illegal types: %s in %s..%s", typestring (v1).c_str(), typestring (v2).c_str(), typestring (v3).c_str()) ;
}


// the names of all the builtin methods for objects
static const char *builtinnames[] = {
    "size",
    "type",
    "append",
    "clear",
    "println",
    "print",
    "close",
    "eof",
    "flush",
    "rewind",
    "seek",
    "clone",
    "fill",
    "resize",
    "sort",
    "bsearch",
    "find",
    "rfind",
    "split",
    "transform",
    "trim",
    "replace",
    "hash",
    "insert",
    "unget",
    "getchar",
    "next",
    "value",
    "indexOf",
    "lastIndexOf",
    "merge",
    "reverse",
    "serialize",
    NULL
};

void VirtualMachine::initBuiltinMethods() {
    Tag *tag ;
    Function *func ;
    Scope *scope ;              // ignored

    for (int i = 0 ; builtinnames[i] != NULL ; i++) {
        string name = builtinnames[i] ;
        tag = (Tag *)system->findVariable (string (".__") + name, scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (tag == NULL) {
            throw Exception (string("Can't find System.__") + name) ;
        }
        func = (Function *)tag->block ;
        func->flags |= BLOCK_BUILTINMETHOD ;
        builtinMethods[name] = func ;
    }
}

Function *VirtualMachine::findBuiltinMethod (const string &name) {
    BuiltinMethodMap::iterator i = builtinMethods.find (name) ;
    if (i != builtinMethods.end()) {
        return i->second ;
    }
    return NULL ;
}

void VirtualMachine::make_closure (Value &dest, const Value &v) {
    StaticLink local_slink (staticLink, frame) ;
    StaticLink *sl = &local_slink ;
    switch (v.type) {
    case T_FUNCTION:
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
        //std::cout << "make_closure: type = " << v.type << "\n" ;
        dest = new Closure (v.type, v.block, sl) ;
        break ;
    default:
        dest = v ;
    }
}

void VirtualMachine::make_closure (Value &dest, Operand *op) {
    StaticLink local_slink (staticLink, frame) ;
    StaticLink *sl = &local_slink ;
    Value *v ;
    switch (op->type) {
    case tREGISTER:
        v = &regfile[op->val.integer] ;
        break ;
    case tLOCALVAR:
        v = static_cast<Variable*>(op->val.ptr)->getAddress (frame) ;
        break ;
    case tVALUE:
    case tUNKNOWN:
        v = &op->val ;
        break ;
    default: {
        int n  = (int)op->val.type - 100 ;
        while (n-- > 0) {
            sl = sl->next ;
        } 
        v = static_cast<Variable*>(op->val.ptr)->getAddress (sl->frame) ;
        break ;
        }
    }

    switch (v->type) {
    case T_FUNCTION:
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
        //std::cout << "make_closure: type = " << v.type << "\n" ;
        dest = new Closure (v->type, v->block, sl) ;
        break ;
    default:
        dest = *v ;
    }
}


// make a closure and set the destination to a Value containing the
// address of the closure.

void VirtualMachine::make_closure_addr (Value &dest, Operand *op) {
    StaticLink local_slink (staticLink, frame) ;
    StaticLink *sl = &local_slink ;
    Value *v ;
    switch (op->type) {
    case tREGISTER:
        v = &regfile[op->val.integer] ;
        break ;
    case tLOCALVAR:
        v = static_cast<Variable*>(op->val.ptr)->getAddress (frame) ;
        break ;
    case tVALUE:
    case tUNKNOWN:
        v = &op->val ;
        break ;
    default: {
        int n  = (int)op->val.type - 100 ;
        while (n-- > 0) {
            sl = sl->next ;
        } 
        v = static_cast<Variable*>(op->val.ptr)->getAddress (sl->frame) ;
        break ;
        }
    }

    switch (v->type) {
    case T_FUNCTION:
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR: {
        //std::cout << "make_closure_addr: type = " << v->type << "\n" ;
        Closure *closure = new Closure (v->type, v->block, sl) ;
        dest = closure ;
        break ;
        }
    default:
        dest = v ;
    }
}

// what needs saved?
//  1. registers in the current function
//  2. scope stack for the current function
//  3. the forstack
//  4. the program counter

struct VMState {
    int numregs ;       // number of registers
    Value *regfile ;    // register values
    int pc ;            // program counter
    int scopesize ;     // size of saved scope stack
    Scope **scopeStack ;    // scope stack
    int for_stack_size ;    // size of forStack
    Foreach **forStack ;    // foreach stack
    StackFrame *frame ;
    Object *thisobj ;
    CodeSequence *code ;
} ;

// args:
//  pc: program counter at yield
//  ssp: scope stack pointer at yield
//  rsp: reg stack pointer at yield
//  fss: for stack size to save
//  f: frame

void *VirtualMachine::save_state(int pc, int ssp, int rsp, int fss, StackFrame *f, Object *t, CodeSequence *code) {
    VMState *state = new VMState ;
    // save the easy parts
    state->pc = pc ;
    state->frame = f ;
    state->numregs = rsp - regs ;
    state->scopesize = ssp - scopeSP ;
    state->for_stack_size = fss ;
    state->thisobj = t ;
    state->code = code ;

    // now we need to save the harder bits
    state->regfile = new Value [state->numregs] ;
    state->scopeStack = new Scope * [state->scopesize] ;
    state->forStack = new Foreach * [fss] ;

    // registers
    int j = 0 ;
    for (int i = regs ; i < rsp ; i++,j++) {
        state->regfile[j] = regfile[i] ;
    }
    
    // scopes
    j = 0 ;
    for (int i = scopeSP ; i < ssp ; i++,j++) {
        state->scopeStack[j] = scopeStack[i] ;
    }

    // foreach stack

    j = 0 ;
    for (int i = 0 ; i < fss ; i++) {
        state->forStack[j] = forStack.top() ;
        forStack.pop() ;
    }

    return reinterpret_cast<void*>(state) ;
}

void VirtualMachine::restore_state (void *s) {
    VMState *state = reinterpret_cast<VMState*>(s) ;

    // we can't move the 'regs' variable as this will be set by 'execute'
    // we just reassign the registers above the current position and this will be
    // taken into account by the executor
    for (int i = 0 ; i < state->numregs ; i++) {
        regfile[regs+i] = state->regfile[i] ;
        state->regfile[i].destruct() ;
    }

    for (int i = 0 ; i < state->scopesize ; i++) {
        scopeStack[scopeSP++] = state->scopeStack[i] ;
    }

    for (int i = 0 ; i < state->for_stack_size ; i++) {
        forStack.push (state->forStack[i]) ;
    }
}

void VirtualMachine::free_state (void *s) {
    VMState *state = reinterpret_cast<VMState*>(s) ;
    delete state ;
}

// invoke the code of a coroutine (continue execution of a function) given all the
// state we need (held in the Closure object)
Value VirtualMachine::invoke_coroutine (Closure *closure) {
    VMState *state = reinterpret_cast<VMState*>(closure->vm_state) ;

    int oldscopesp = scopeSP ;
    int currforsp = forStack.size() ;

    // restore the VM registers and stacks
    restore_state (closure->vm_state) ;

    Function *func = reinterpret_cast<Function*>(closure->block) ;

    if (state->thisobj != NULL) {
        state->thisobj->acquire(func->flags & BLOCK_SYNCHRONIZED) ;
    }


    try {
        execute (state->code, state->frame, closure->slink, state->pc) ;
    } catch (YieldValue &y) {
        scopeSP = oldscopesp;
        if (state->thisobj != NULL) {
            state->thisobj->release(func->flags & BLOCK_SYNCHRONIZED) ;
            // don't decrement the reference count as it is held by the vm state
        }
        // caught for a yield opcode execution
        // Yield object contains sp, scopesp and reg pointer.  Use these to
        // save the current state
        Closure *c = new Closure (T_FUNCTION, func, closure->slink) ;
        c->vm_state = save_state (y.pc, y.scopeSP, y.regs, forStack.size() - currforsp, state->frame, state->thisobj, y.code) ;
        c->value = y.value ;
        y.value.destruct() ;
        return c ;
    } catch (...) {
        if (state->thisobj != NULL) {
            state->thisobj->release(func->flags & BLOCK_SYNCHRONIZED) ;
            decObjectRef (state->thisobj) ;
        }
        if (decRef (state->frame, stackframe) == 0) {
            if (verbosegc) {
               std::cerr << "gc: deleting stack frame " << state->frame << "\n" ;
            }
            delete state->frame ;
        }

        scopeSP = oldscopesp;
        throw ;
    }
    if (state->thisobj != NULL) {
        state->thisobj->release(func->flags & BLOCK_SYNCHRONIZED) ;
        decObjectRef (state->thisobj) ;
    }

    Value v = retval ;
    retval.destruct() ;
    if (decRef (state->frame, stackframe) == 0) {
        if (verbosegc) {
           std::cerr << "gc: deleting stack frame " << state->frame << "\n" ;
        }
        delete state->frame ;
    }
    scopeSP = oldscopesp;
    return v ;
}




StackFrame *VirtualMachine::getMainStack() {
    parserLock.acquire (true) ;
    StackFrame *s = aikido->mainStack ;
    parserLock.release(true) ;
    return s ;
}

Node *VirtualMachine::getBlockAnnotationValue (Annotation *ann, Tag *tag, std::string func) {
    // check if there is an actual value for the function
    Annotation::ActualMap::iterator actual = ann->actuals.find (func);
    if (actual == ann->actuals.end()) {
        // no user supplied actual value get the default
        Interface *iface = ann->iface;
        InterpretedBlock *block = (InterpretedBlock*)tag->block;
        if (block->parameters.size() != 0) {
            // has a default value?
            return block->parameters[0]->def;
        }
        return NULL;
    } else {
        return actual->second;
    }
}

void VirtualMachine::getAnnotations (Operand *dest, Operand *var) {
    map *m = new map();
    Variable *v = static_cast<Variable*>(var->val.ptr);
    AnnotationList &annotations = v->annotations;
    for (AnnotationList::iterator i = annotations.begin() ; i != annotations.end() ; i++) {
        Annotation *a = *i;
        map *actualmap = new map();
        Scope::VarMap::iterator var;
        Interface *iface = a->iface;
        for (var = iface->variables.begin() ; var != iface->variables.end() ; var++) {
            std::string tagname = var->first.str;
            if (tagname[0] == '.') {
                std::string varname = tagname.substr(1);
                // tag
                Node *node = getBlockAnnotationValue (a, (Tag*)var->second, varname);
                if (node == NULL) {
                    actualmap->insert (Value(varname), Value());        // none
                } else {
                    node = new Node (aikido, RETURN, node) ;
                    codegen::CodeSequence *code = aikido->codegen->generate (node) ;
                    execute (code) ;
                    Value result = retval ;
                    retval.destruct() ;
                    actualmap->insert (Value (varname), result);
                }
            }
        }
        
        m->insert (Value (a->iface->name.substr(1)), Value (actualmap));        // remove @ from start of name
    }
    set (dest, m);
}


}

