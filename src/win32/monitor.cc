/*
 * monitor.cc
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
 * Version:  1.4
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */



// 
// Aikido monitor implementation
// Author: David Allison

// This uses the pthreads implementation of mutexes and condition variables.  The
// pthreads mutex is not reentrant from the same thread.  The condition variable
// takes a mutex and atomically releases it.   The mutex field of the monitor
// is held purely during access to the monitor object.  Additional fields
// count the number of times a monitor is entered and which thread currently
// owns it.  
//
// 2 condition variables are used:
//   queue: when the monitor is not available, we wait on this to be
//          notified when some other thread releases the monitor
//   event: used for wait/notify functions.  This is set of threads
//          that have called wait() on the monitor.
//
// The pthread condition variable precludes the use of a reentrant
// mutex because it will only release a plain pthread_mutex_t and
// has no way to use another abstract class built on top of a
// pthread_mutex_t.  In addition, the mutex would need to be
// completely released, which means that the condition variable
// would have to unwind all the thread accesses to it, and
// restore the counters when the mutex is reacquired.
//
// The monitor uses 2 fields for bookkeeping:
//   owner: the thread that currently owns the monitor.
//   n: the number of times the owner has entered the monitor without
//      leaving it.
//

#include "../aikido.h"
#include <errno.h>
#include "../aikidocodegen.h"
#if defined(_OS_LINUX) || defined(_OS_MACOSX)
#include <sys/time.h>
#endif

namespace aikido {

Monitor::Monitor (Block *b, StaticLink *sl, StackFrame *dl, Instruction *i) : Object (b, sl, dl, i, false) {
    owner = 0 ;
    n = 0 ;
    init (sizeof (Monitor)) ;
}

Monitor::~Monitor() {
}

//
// acquire the monitor.  This blocks indefinitely until the
// monitor is made available.  This might never happen
//

void Monitor::acquire(bool force) {
    if (block->flags & BLOCK_SYNCHRONIZED || force) {
        mutex.lock() ;
        OSThread_t me = OSThread::self() ;
        if (owner == 0) {                       // free?
            owner = me ;                        // yup, got it
        } else if (owner != me) {		// not me?
            do {
                queue.wait (mutex) ;		// wait until I can get it
            } while (owner != 0) ;
            owner = me ;                        // I got it, set owner
        }
        n++ ;				// inc entry count
        //std::cout << "Monitor::acquire\n" ;
        mutex.unlock() ;
    }
}

//
// release a monitor, possibly allowing another thread to gain access
//

void Monitor::release(bool force) {
    if (block->flags & BLOCK_SYNCHRONIZED || force) {
        mutex.lock() ;
        OSThread_t me = OSThread::self() ;
        if (owner != me) {
            mutex.unlock() ;
            throw "Not monitor owner" ;
        }
        n-- ;
        if (n == 0) {			// last time I have entered?
            owner = 0 ;			// release ownership
            queue.signal() ;	// signal any waiters
        }
        //std::cout << "Monitor::release\n" ;
        mutex.unlock() ;
    }
}

void Monitor::wait() {
    mutex.lock() ;			// acquire mutex
    OSThread_t me = OSThread::self() ;		// get current thread
    if (owner != me) {			// check that I am the current owner
        mutex.unlock() ;
        throw "Not monitor owner" ;
    }
    int prev = n ;			// save monitor count
    n = 0 ;
    owner = 0 ;
    queue.signal() ;			// signal waiters

    //std::cout << "Monitor::wait - waiting\n" ;
    event.wait (mutex) ;		// wait and release mutex

    //std::cout << "Monitor::wait - reacquiring\n" ;
    while (owner != 0) {			// reacquire monitor
        queue.wait (mutex) ;
    }

    //std::cout << "Monitor::wait - reacquired\n" ;
	//printf ("resetting owner to %x\n", me);
    owner = me ;
    n = prev ;
    mutex.unlock() ;
}

// return false for timeout
bool Monitor::timewait(int microsecs) {
    mutex.lock() ;			// acquire mutex
    OSThread_t me = OSThread::self() ;		// get current thread
    if (owner != me) {			// check that I am the current owner
        mutex.unlock() ;
        throw "Not monitor owner" ;
    }
    int prev = n ;			// save monitor count
    n = 0 ;
    owner = 0 ;
    queue.signal() ;			// signal waiters

    struct timeval now ;
#if defined(_OS_SOLARIS)
    timestruc_t t ;
#else
    struct timespec t ;
#endif

    int secs = microsecs / 1000000 ;    // number of seconds
    int usecs = microsecs % 1000000 ;   // and remaining microseconds

#if defined(_OS_MACOSX) || defined(_OS_CYGWIN)
    gettimeofday (&now, NULL) ;
#else
    OS::gettimeofday(&now);
#endif

    // make in absolute timestruc_t out of the current time
    // and the delta
    t.tv_sec = now.tv_sec + secs + (now.tv_usec + usecs) / 1000000 ;
    t.tv_nsec = ((now.tv_usec + usecs) % 1000000) * 1000 ;

    bool timeout = false ;
    int v = event.timedwait (mutex, &t) ;             // may fail to recover mutex 
    if (v == -1) {
        // mutex is not locked as we failed to lock it
        throw "Failed to recover mutex" ;
    }
    while (owner != 0) {			// reacquire monitor
        queue.wait (mutex) ;
    }

    owner = me ;
    n = prev ;
    mutex.unlock() ;
    return (bool)v ;
}

void Monitor::notify() {
    mutex.lock() ;			// acquire mutex
    OSThread_t me = OSThread::self() ;		// get current thread
    if (owner != me) {			// check that I am the current owner
        mutex.unlock() ;
        throw "Not monitor owner" ;
    }
    event.signal() ;
	if (owner != me) {
		printf ("notify got bad owner\n");
	}
    //std::cout << "Monitor::notify\n" ;
	//printf ("returning from notify with owner %x\n", owner);
    mutex.unlock() ;
}

void Monitor::notifyAll() {
    mutex.lock() ;			// acquire mutex
    OSThread_t me = OSThread::self() ;		// get current thread
    if (owner != me) {			// check that I am the current owner
        mutex.unlock() ;
        throw "Not monitor owner" ;
    }
    event.broadcast() ;
    //std::cout << "Monitor::notifyAll\n" ;
    mutex.unlock() ;
}


}
