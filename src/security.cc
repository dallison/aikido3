/*
 * security.cc
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
 * Version:  1.11
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */



#include "aikido.h"

#if defined(_OS_LINUX)
#define _XOPEN_SOURCE
#endif

#include <unistd.h>
#if !defined(_OS_MACOSX)
#include <crypt.h>
#endif

#if defined(_OS_LINUX)
#include <pwd.h>
#endif

namespace aikido {

extern "C" {


AIKIDO_NATIVE (getpassword) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "getpassword", "need a string for 'prompt' parameter") ;
    }
#if defined(_OS_LINUX) || defined(_OS_MACOSX)
    char *pass = getpass (paras[1].str->c_str()) ;
#else
    char *pass = getpassphrase (paras[1].str->c_str()) ;
#endif
    return string (pass) ;
}

AIKIDO_NATIVE (encrypt) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "encrypt", "need a string for 'key' parameter") ;
    }
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "encrypt", "need a string for 'salt' parameter") ;
    }
#if defined(_OS_LINUX) || defined(_OS_MACOSX)
    char *r = crypt (paras[1].str->c_str(), paras[2].str->c_str()) ;
#else
    char *r = des_crypt (paras[1].str->c_str(), paras[2].str->c_str()) ;
#endif
    return string (r) ;
}

}

}
