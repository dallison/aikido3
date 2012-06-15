/*
 * os.h
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
 * Version:  1.7
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/01/24
 */



#ifndef __OS_H
#define __OS_H

#include <stdio.h>
#include <fstream>

class fd_fstream : public std::iostream {
private:
	FILE *myfp ;
	std::filebuf fb ;
    
public:
	fd_fstream (int __fd, ios_base::openmode __mode = ios_base::in | ios_base::out) : myfp(_fdopen (__fd, "rw")), fb(myfp), std::iostream (NULL) {
        this->init (&fb) ;
    }

    ~fd_fstream() {
        //fb.close() ;
    }

    std::filebuf *rdbuf() { return &fb ; }

    void close() {
        fclose (myfp) ;
    }

	int fd() {
		return myfp->_file ;
	}

} ;

class fd_ifstream : public std::istream {
private:
	FILE *myfp ;
	std::filebuf fb ;
public:
    // NOTE: this is unbuffered because it appears that a read does not complete on CR like other streams
	fd_ifstream (int __fd, ios_base::openmode __mode = ios_base::in) : myfp(_fdopen (__fd, "r")), fb(myfp), std::istream (NULL) {
        this->init (&fb) ;
    }

    ~fd_ifstream() {
        //fb.close() ;
    }

    std::filebuf *rdbuf() { return &fb ; }

    void close() {
        fclose (myfp) ;
    }
	int fd() {
		return myfp->_file ;
	}

} ;

class fd_ofstream : public std::ostream {
private:
	FILE *myfp ;
	std::filebuf fb ;
public:
	fd_ofstream (int __fd, ios_base::openmode __mode = ios_base::out) : myfp(_fdopen (__fd, "w")), fb(myfp), std::ostream (NULL) {
        this->init (&fb) ;
    }
    ~fd_ofstream() {
        //fb.close() ;
    }

    std::filebuf *rdbuf() { return &fb ; }

    void close() {
        fclose (myfp) ;
    }
	int fd() {
		return myfp->_file ;
	}

} ;

#include <vector>
#include <windows.h>
#include <stdio.h>
#include <string>

/*************************************************
*       Perl-Compatible Regular Expressions      *
*************************************************/

/* Copyright (c) 1997-2001 University of Cambridge */

#ifndef _PCREPOSIX_H
#define _PCREPOSIX_H

/* This is the header for the POSIX wrapper interface to the PCRE Perl-
Compatible Regular Expression library. It defines the things POSIX says should
be there. I hope. */

/* Have to include stdlib.h in order to ensure that size_t is defined. */

#include <stdlib.h>

/* Allow for C++ users */

#ifdef __cplusplus
extern "C" {
#endif

/* Options defined by POSIX. */

#define REG_ICASE     0x01
#define REG_NEWLINE   0x02
#define REG_NOTBOL    0x04
#define REG_NOTEOL    0x08

/* These are not used by PCRE, but by defining them we make it easier
to slot PCRE into existing programs that make POSIX calls. */

#define REG_EXTENDED  0
#define REG_NOSUB     0

/* Error values. Not all these are relevant or used by the wrapper. */

enum {
  REG_ASSERT = 1,  /* internal error ? */
  REG_BADBR,       /* invalid repeat counts in {} */
  REG_BADPAT,      /* pattern error */
  REG_BADRPT,      /* ? * + invalid */
  REG_EBRACE,      /* unbalanced {} */
  REG_EBRACK,      /* unbalanced [] */
  REG_ECOLLATE,    /* collation error - not relevant */
  REG_ECTYPE,      /* bad class */
  REG_EESCAPE,     /* bad escape sequence */
  REG_EMPTY,       /* empty expression */
  REG_EPAREN,      /* unbalanced () */
  REG_ERANGE,      /* bad range inside [] */
  REG_ESIZE,       /* expression too big */
  REG_ESPACE,      /* failed to get memory */
  REG_ESUBREG,     /* bad back reference */
  REG_INVARG,      /* bad argument */
  REG_NOMATCH      /* match failed */
};


/* The structure representing a compiled regular expression. */

typedef struct {
  void *re_pcre;
  size_t re_nsub;
  size_t re_erroffset;
} regex_t;

/* The structure in which a captured offset is returned. */

typedef int regoff_t;

typedef struct {
  regoff_t rm_so;
  regoff_t rm_eo;
} regmatch_t;

/* The functions */

extern int aikido_regcomp(regex_t *, const char *, int);
extern int aikido_regexec(regex_t *, const char *, size_t, regmatch_t *, int);
extern size_t aikido_regerror(int, const regex_t *, char *, size_t);
extern void aikido_regfree(regex_t *);

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* End of pcreposix.h */



// this is not defined in the header file
#define REG_EXTENDED 0


//
// system dependent things for win32
// Author: David Allison
// Version: 1.7 11/12/03
//

namespace aikido {

struct timespec {
    int tv_sec ;
    int tv_nsec ;
} ;

namespace OS {
    void init() ;

    bool libraryLoaded (const std::string &name) ;
    void *openLibrary (const char *name) ;
    void *findSymbol (const char *name, void *libhandle = NULL) ;
    char *libraryError() ;

    extern const char *libraryExt ;

    int fork() ;

    // get file stats, return true if file exists
    bool fileStats (const char *name, void *stats) ;

    // get a unique identifier for a file
    __int64 uniqueId (void *stats) ;

    // filename expansion utilities
    
    struct Expansion {
        HANDLE handle ;
        std::vector<std::string> names ;
    } ;

    bool expandFileName (const char *name, Expansion &result) ;
    void freeExpansion (Expansion &x) ;


    typedef regex_t Regex ;

    struct RegexMatch {
        int rm_so ;
        int rm_eo ;
    } ;
    int regexCompile (Regex *, const char *str, int flags) ;
    int regexExec (Regex *, const char *str, int, RegexMatch *match, int flags) ;
    void regexError (int, Regex *, char *buffer, int size) ; 
    void regexFree (Regex *) ;

    __int64 strtoll (const char *str, char **ptr, int base) ;

    void gettimeofday (struct timeval *t) ;

    extern __int64 time1970 ;		// time at Jan 1 1970

    void make1970() ;
}


//
// semaphore
//

struct OSSemaphore {
    OSSemaphore(int v = 0) {
        sem = CreateSemaphore (NULL, v, 100000, NULL) ;
    }

    ~OSSemaphore() {
        CloseHandle (sem) ;
    }

    void wait() {
        WaitForSingleObject (sem, INFINITE) ;
    }

    void trywait() {

    }

    void post(int n = 1) {
        ReleaseSemaphore (sem, n, NULL) ;
    }

    HANDLE sem ;
} ;

//
// mutex.
//

struct OSMutex {
    OSMutex() {
        mutex = CreateMutex (NULL, FALSE, NULL) ;
    }
    ~OSMutex() {
        CloseHandle (mutex) ;
    }

    void lock() {
        WaitForSingleObject (mutex, INFINITE) ;
    }

    void unlock() {
        ReleaseMutex (mutex) ;
    }

    HANDLE mutex ;

} ;

//
// condition variable.
//

struct OSCondvar {
    OSCondvar() : waiters (0), tickets(0), counter(0) {
        event = CreateEvent (NULL, true, false, NULL) ;
    }
    
    ~OSCondvar() {
        CloseHandle (event) ;
    }

    int wait(OSMutex &mutex) ;

    int timedwait(OSMutex &mutex, const struct timespec *abstime) ;

    int signal() ;

    int broadcast() ;

    HANDLE event ;
    int waiters ;
    int tickets ;
    int counter ;
} ;

//
// thread
//

typedef DWORD OSThread_t ;

struct OSThread {
    OSThread (void *(*start)(void*), void *arg) {
        handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start, arg, 0, &id) ; 
    }

   ~OSThread() {
       CloseHandle (handle) ;
    }

    static OSThread_t self() {
        return GetCurrentThreadId() ;
    }

    void *join() {		// join with thread t
        WaitForSingleObject (handle, INFINITE) ;
        return NULL ;
    }


    void setPriority (int pri) {
        return ;
    }

    int getPriority() {
        return 0 ;
    }

    void stop() {
        return ;
    }

    HANDLE handle ;
    DWORD id ;
} ;


// fix these with read priority values

inline int toOSPriority (int p) {
    return p ;
}


inline int fromOSPriority (int p) {
    return p ;
}


}

#endif
