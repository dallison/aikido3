/*
 * os.h
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
 * 
 * Contributor(s): dallison
 *
 * Version:  1.9
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/08/11
 */

// CHANGE LOG
//	1/21/2004	David Allison of PathScale Inc.
//		Tell stdio_filebuf to close file on destruction

#pragma ident "@(#)os.h	1.9 03/08/11 Aikido - SMI"


#ifndef __OS_H
#define __OS_H

#include <vector>
#include <glob.h>
#include "aikidopcreposix.h"
#include <string>
#include <errno.h>

#if !defined(_OS_MACOSX)
// problems with timeval if this is included in Mac OS X
#include <unistd.h>
#endif

//
// system dependent things for UNIX systems
// Author: David Allison
// Version: 1.9 08/11/03
//

namespace aikido {

#if defined(_CC_GCC) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4))
#include <ext/stdio_filebuf.h>

class fd_fstream : public std::iostream {
private:
    __gnu_cxx::stdio_filebuf<char> fb ;
    int myfd ;
public:
    fd_fstream (int __fd, ios_base::openmode __mode = ios_base::in | ios_base::out) : myfd((__fd)), fb(__fd, __mode, 1), std::iostream (NULL) {
        this->init (&fb) ;
    }

    ~fd_fstream() {
        //fb.close() ;
    }

    std::filebuf *rdbuf() { return &fb ; }

    void close() {
        ::close (myfd) ;
    }

} ;

class fd_ifstream : public std::istream {
private:
    __gnu_cxx::stdio_filebuf<char> fb ;
    int myfd ;
public:
    // NOTE: this is unbuffered because it appears that a read does not complete on CR like other streams
    fd_ifstream (int __fd, ios_base::openmode __mode = ios_base::in) : myfd((__fd)), fb(__fd, __mode, 1), std::istream (NULL) {
        this->init (&fb) ;
    }

    ~fd_ifstream() {
        //fb.close() ;
    }

    std::filebuf *rdbuf() { return &fb ; }

    void close() {
        ::close (myfd) ;
    }

} ;

class fd_ofstream : public std::ostream {
private:
    __gnu_cxx::stdio_filebuf<char> fb ;
    int myfd ;
public:
    fd_ofstream (int __fd, ios_base::openmode __mode = ios_base::out) : myfd((__fd)), fb(__fd, __mode, BUFSIZ), std::ostream (NULL) {
        this->init (&fb) ;
    }
    ~fd_ofstream() {
        //fb.close() ;
    }

    std::filebuf *rdbuf() { return &fb ; }

    void close() {
        ::close (myfd) ;
    }

} ;

#elif defined(_CC_GCC) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
#include <ext/stdio_filebuf.h>

class fd_fstream : public std::iostream {
private:
    __gnu_cxx::stdio_filebuf<char> fb ;
    int myfd ;
public:
    fd_fstream (int __fd, ios_base::openmode __mode = ios_base::in | ios_base::out) : myfd((__fd)), fb(__fd, __mode, true, 1), std::iostream (NULL) {
        this->init (&fb) ;
    }

    ~fd_fstream() {
        //fb.close() ;
    }

    std::filebuf *rdbuf() { return &fb ; }

    void close() {
        ::close (myfd) ;
    }

} ;

class fd_ifstream : public std::istream {
private:
    __gnu_cxx::stdio_filebuf<char> fb ;
    int myfd ;
public:
    // NOTE: this is unbuffered because it appears that a read does not complete on CR like other streams
    fd_ifstream (int __fd, ios_base::openmode __mode = ios_base::in) : myfd((__fd)), fb(__fd, __mode, true, 1), std::istream (NULL) {
        this->init (&fb) ;
    }

    ~fd_ifstream() {
        //fb.close() ;
    }

    std::filebuf *rdbuf() { return &fb ; }

    void close() {
        ::close (myfd) ;
    }

} ;

class fd_ofstream : public std::ostream {
private:
    __gnu_cxx::stdio_filebuf<char> fb ;
    int myfd ;
public:
    fd_ofstream (int __fd, ios_base::openmode __mode = ios_base::out) : myfd((__fd)), fb(__fd, __mode, true, BUFSIZ), std::ostream (NULL) {
        this->init (&fb) ;
    }
    ~fd_ofstream() {
        //fb.close() ;
    }

    std::filebuf *rdbuf() { return &fb ; }

    void close() {
        ::close (myfd) ;
    }

} ;

#else

#define fd_fstream std::fstream
#define fd_ifstream std::ifstream
#define fd_ofstream std::ofstream

#endif

// this version uses pthreads

#include <pthread.h>
#include <semaphore.h>	// posix semaphores

namespace OS {
    void init() ;
    bool libraryLoaded (const std::string &name) ;
    void *openLibrary (const char *name) ;
    void *findSymbol (const char *name, void *libhandle = NULL) ;
    char *libraryError() ;

    extern const char *libraryExt ;
#if defined (_OS_MACOSX)
    extern const char *libraryExt2 ;
#endif

    int fork() ;

    // get file stats, return true if file exists
    bool fileStats (const char *name, void *stats) ;

    // get a unique identifier for a file
    long long uniqueId (void *stats) ;

    // filename expansion utilities
    
    struct Expansion {
        void *handle ;
        std::vector<const char*> names ;
    } ;

    bool expandFileName (const char *name, Expansion &result) ;
    void freeExpansion (Expansion &x) ;

    typedef regex_t Regex ;

    typedef regmatch_t RegexMatch ;

    int regexCompile (Regex *, const char *str, int flags) ;
    int regexExec (Regex *, const char *str, int, RegexMatch *match, int flags) ;
    void regexError (int, Regex *, char *buffer, int size) ;
    void regexFree (Regex *) ;

    long long strtoll (const char *str, char **ptr, int base) ;

    void gettimeofday (struct timeval *t) ;


}

//
// semaphore
//

struct OSSemaphore {
    OSSemaphore(int v = 0) {
        sem_init (&sem, 0, v) ;
    }

    ~OSSemaphore() {
        sem_destroy (&sem) ;
    }

    void wait() {
        sem_wait (&sem) ;
    }

    void trywait() {
        sem_trywait (&sem) ;
    }

    void post(int n = 1) {
        while (n-- > 0) {
            sem_post (&sem) ;
        }
    }

    sem_t sem ;
} ;

//
// mutex.  This is a non-reentrant POSIX style mutex
//

struct OSMutex {
    OSMutex() {
        pthread_mutex_init (&mutex, NULL) ;
    }
    ~OSMutex() {
        pthread_mutex_destroy (&mutex) ;
    }

    // reentrant mutex lock
    void lock() {
        pthread_mutex_lock (&mutex) ;
    }

    void unlock() {
        pthread_mutex_unlock (&mutex) ;
    }

    pthread_mutex_t mutex ;
} ;

//
// condition variable.
//

struct OSCondvar {
    OSCondvar() {
        pthread_cond_init (&cond, NULL) ;
    }
    ~OSCondvar() {
        pthread_cond_destroy (&cond) ;
    }

    int wait(OSMutex &mutex) {
        return pthread_cond_wait (&cond, &mutex.mutex) ;
    }

    // returns 1 for OK, 0 for timeout and -1 for error
    int timedwait(OSMutex &mutex, const struct timespec *abstime) {
        int v = pthread_cond_timedwait (&cond, &mutex.mutex, abstime) ;
        return v != ETIMEDOUT ;
    }

    int signal() {
        return pthread_cond_signal (&cond) ;
    }

    int broadcast() {
        return pthread_cond_broadcast (&cond) ;
    }

    pthread_cond_t cond ;
} ;

//
// thread
//

typedef pthread_t OSThread_t ;

struct OSThread {
    OSThread (void *(*start)(void*), void *arg) {
        //pthread_attr_t attr ;
        //pthread_attr_init(&attr) ;
        //pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM) ;
        int e = pthread_create (&id, NULL, start, arg) ;
        if (e != 0) {
            throw "Unable to create system thread" ;
        }
    }

   ~OSThread() {
    }

    static OSThread_t self() {
       return pthread_self() ;
    }

    void *join() {		// join with thread t
        void *status = 0 ;
        if (id != 0) {
            pthread_join (id, &status) ;
        }
        return status ;
    }

    void setPriority (int pri) {
        struct sched_param p ;
        p.sched_priority = pri ;
        pthread_setschedparam (id, SCHED_OTHER, &p) ;
    }

    int getPriority() {
        struct sched_param p ;
        int policy ;
        pthread_getschedparam (id, &policy, &p) ;
        return p.sched_priority ;
    }

    void stop() {
    }

    pthread_t id ;
} ;


// according to the priocntl -l command:
//
//CONFIGURED CLASSES
//==================
//
//SYS (System Class)
//
//TS (Time Sharing)
//	Configured TS User Priority Range: -60 through 60
//
//IA (Interactive)
//	Configured IA User Priority Range: -60 through 60
//
//
// the range of priorities are -60 to +60 with a higher value
// meaning higher priority
//
// The Aikido priorities are 0 .. 100

const int MIN_OS_PRI = -60 ;
const int MAX_OS_PRI = 60 ;
const int OS_PRI_RANGE = 120 ;
const float OS_PRI_RATIO = (float)OS_PRI_RANGE / 100.0 ;

inline int toOSPriority (int p) {
    return MIN_OS_PRI + (p * OS_PRI_RANGE) ; 
}


inline int fromOSPriority (int p) {
    return (int)((p - MIN_OS_PRI) / OS_PRI_RATIO) ;
}


}

#endif
