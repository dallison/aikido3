/*
 * os.cc
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

#pragma ident "@(#)os.cc	1.11 03/07/29 Aikido - SMI"


//
// Implemenation of Unix OS specific code
//
// Author: David Allison
// Version: 1.11 07/29/03
//

#if !defined (_OS_MACOSX)
#include <dlfcn.h>
#endif

#include <time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#if defined(_OS_MACOSX)
#include <sys/wait.h>
#include <mach-o/dyld.h>

#elif defined(_OS_CYGWIN)
#include <sys/wait.h>

#else
#include <wait.h>
#endif
#include <errno.h>
#include "../aikido.h"


namespace aikido {

namespace OS {

// some linuxes don't define this
#if defined(_OS_LINUX)
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)0)
#endif
#endif

void init() {
}

#if defined (_OS_MACOSX) 
char linkerror[256] ;
bool errorset ;

// for macosx the file can be a .so file or a .dylib file
// so we try both
void *tryopen (const char *name) {
    NSObjectFileImage image = 0 ;
    NSObjectFileImageReturnCode rc ;
    NSLinkEditErrors errors ;
    int linkerrno ;
    const char *errorstring ;
    const char *filename ;

    rc = NSCreateObjectFileImageFromFile (name, &image) ;
    void *handle = NULL ;
    errorset = false ;
    if (rc == NSObjectFileImageInappropriateFile) {
        handle = (void *)NSAddImage (name, NSADDIMAGE_OPTION_RETURN_ON_ERROR) ;
    } else if (rc == NSObjectFileImageSuccess) {
        handle = NSLinkModule (image, name, NSLINKMODULE_OPTION_RETURN_ON_ERROR) ;
        NSDestroyObjectFileImage (image) ;
    } else {
        return NULL ;
    }
    if (handle == NULL) {
        NSLinkEditError (&errors, &linkerrno, &filename, &errorstring) ;
        errorset = true ;
        strcpy (linkerror, errorstring) ;
    }
    return handle ;
}

#endif


    void *openLibrary (const char *name) {
#if defined (_OS_MACOSX)
        char fullname[256] ;
        strcpy (fullname, name) ;

 	// remove the extension
        char *dot = strrchr (fullname, '.') ;
        *dot = 0 ;

	// try .so
        strcat (fullname, ".so") ;
        void *handle = tryopen (fullname) ;
        if (handle != NULL) {
            return handle ;
        }
        // if not found, remove the extension and try .dylib
        *dot = 0 ;
        strcat (fullname, ".dylib") ;
        handle = tryopen (fullname) ;
        return handle ;
#else
        return dlopen (name, RTLD_LAZY | RTLD_GLOBAL) ;
#endif
    }


    void *findSymbol (const char *name, void *libhandle) {
#if defined(_OS_MACOSX)
        char namebuf[256] ;
        sprintf (namebuf, "_%s", name) ;
        if (NSIsSymbolNameDefined (namebuf)) {
            NSSymbol sym = NSLookupAndBindSymbol (namebuf) ;
            if (sym != NULL) {
                void *addr = NSAddressOfSymbol(sym) ;
                //printf ("found symbol %s at %x\n", name, addr) ;
                return addr ;
            }
        }
        return NULL ;
#else
        return dlsym (RTLD_DEFAULT, name) ;
#endif
    }

    bool libraryLoaded (const std::string &name) {
        return false ;		// done using uniqueId
    }

    char *libraryError() {
#if defined (_OS_MACOSX)
    return errorset ? linkerror : (char*)"" ;
#else
        return (char*)dlerror() ;
#endif
    }

#if defined (_OS_MACOSX)
    const char *libraryExt2 = ".dylib" ;	
#endif
    const char *libraryExt = ".so" ;

    int fork() {
#if defined(_OS_SOLARIS)
        return fork1() ;
#elif defined(_OS_LINUX)
        return ::fork() ;
#elif defined(_OS_MACOSX)
        return ::fork() ;
#else
   #error "Unknown operating system"
#endif
    }

    bool fileStats (const char *name, void *stats) {
        return stat (name, (struct stat*)stats) == 0 ;
    }

    long long uniqueId (void *stats) {
        return (long long)((struct stat*)stats)->st_ino ;
    }

    bool expandFileName (const char *fn, Expansion &res) {
        glob_t *g = new glob_t ;
        if (glob (fn, 0, NULL, g) != 0) {
            delete g ;
            return false ;
        }
        res.handle = (void*)g ;
        for (int i = 0 ; i < g->gl_pathc ; i++) {
            res.names.push_back (g->gl_pathv[i]) ;
        }
        return true ;
    }

    void freeExpansion (Expansion &x) {
        globfree ((glob_t*)x.handle) ;
        delete (glob_t*)x.handle ;
    }

    int regexCompile (Regex *r, const char *str, int flags) {
        return aikido_regcomp (r, str, flags) ;
    }

    int regexExec (Regex *r, const char *str, int i, RegexMatch *match, int flags) {
       return aikido_regexec (r, str, i, (regmatch_t*)match, flags) ;
    }

    void regexError (int n, Regex *r, char *buffer, int size) {
        aikido_regerror (n, r, buffer, size) ;
    }

    void regexFree (Regex *r) {
        aikido_regfree (r) ;
    }

    long long strtoll (const char *str, char **ptr, int base) {
#if defined (_OS_MACOSX)
        return ::strtoq (str, ptr, base) ;
#else
        return ::strtoll (str, ptr, base) ;
#endif
    }

    void gettimeofday (struct timeval *t) {
        ::gettimeofday (t, NULL) ;
    }

}



}
