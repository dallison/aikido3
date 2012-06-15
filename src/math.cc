/*
 * math.cc
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
 * 
 * Contributor(s): dallison
 *
 * Version:  1.12
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */



//
// aikido native functions for mathematics
// 
// Author: David Allison
// Version: 1.2 12/21/99
//
#include "aikido.h"
#include <math.h>

using namespace aikido ;

extern "C" {

#define GETARG(f,t,arg,n) \
    t arg ;\
    if (paras[n].type == T_INTEGER) {\
        arg = paras[n].integer ;\
    } else if (paras[n].type == T_REAL) {\
        arg = static_cast<t>(paras[n].real) ;\
    } else {\
        throw newParameterException (vm, stack, f, "bad argument type") ;\
    }


#define MATHFUNC(f) AIKIDO_NATIVE(_##f) {\
    GETARG (#f, double, arg1, 1) \
    return ::f (arg1) ;\
}

// single argument math functions

MATHFUNC (sin) 
MATHFUNC (cos) 
MATHFUNC (tan) 
MATHFUNC (acos) 
MATHFUNC (asin) 
MATHFUNC (atan) 
MATHFUNC (sinh) 
MATHFUNC (cosh) 
MATHFUNC (tanh) 
MATHFUNC (exp) 
MATHFUNC (log) 
MATHFUNC (log10) 
MATHFUNC (sqrt) 
MATHFUNC (ceil) 
MATHFUNC (fabs) 
MATHFUNC (floor) 

AIKIDO_NATIVE (_atan2) {
    GETARG ("atan2", double, arg1, 1)
    GETARG ("atan2", double, arg2, 2)
    return ::atan2 (arg1, arg2) ;
}

AIKIDO_NATIVE (_pow) {
    GETARG ("pow", double, arg1, 1)
    GETARG ("pow", double, arg2, 2)
    return ::pow (arg1, arg2) ;
}

AIKIDO_NATIVE (_ldexp) {
    GETARG ("ldexp", double, arg1, 1)
    GETARG ("ldexp", int, arg2, 2)
    return ::ldexp (arg1, arg2) ;
}

AIKIDO_NATIVE (_fmod) {
    GETARG ("fmod", double, arg1, 1)
    GETARG ("fmod", double, arg2, 2)
    if (arg2 == 0) {
        throw newParameterException (vm, stack, "fmod", "second argument would result in division by zero") ;
    }
    return ::fmod (arg1, arg2) ;
}

AIKIDO_NATIVE (_trunc) {
    GETARG ("trunc", double, arg, 1) 
    return (INTEGER)arg ;
}

AIKIDO_NATIVE (_round) {
    GETARG ("round", double, arg, 1) 
    return (INTEGER)(arg + (double)0.5) ;
}

}
