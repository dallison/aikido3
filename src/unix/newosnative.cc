/*
 * osnative.cc
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
 * Portions created by David Allison of PathScale Inc. are Copyright (C) PathScale Inc. 2004.  All
 * Rights Reserved.
 *
 * Closure support is Copyright (C) 2004 David Allison.  All rights reserved.
 * 
 * Contributor(s): dallison
 *
 * Version:  1.12
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */

// CHANGE LOG
// 1/21/2004    David Allison of PathScale Inc.
//              Use cuserid() instead of getlogin() for platforms that support it
// 10/21/2004   David Allison
//              Added closure support

#pragma ident "@(#)osnative.cc	1.12 03/07/29 Aikido - SMI"


// this file is included in the middle of native.cc.  
//
// It is rather messy because it supports various flavors of UNIX.
// Lots of #ifdefs etc.

// Change Log:

#include <sys/types.h>

#include <ctype.h>
#include "string.h"
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#if defined(_OS_LINUX) && !defined (_OS_CYGWIN)
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#endif

#if defined(_OS_MACOSX)
#include <sys/wait.h>
#include <sys/types.h>
//#include <mach/boolean.h>
typedef int boolean_t ;
#include <sys/sysctl.h>
//extern "C" void getdomainname (const char *, int) ;

#elif defined(_OS_CYGWIN)
#include <sys/wait.h>

#else
#include <wait.h>
#include <sys/stat.h>
#endif

#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <algorithm>
#include <sys/resource.h>		// possibly solaris only

#if defined(_OS_SOLARIS)
#include <sys/systeminfo.h>
#endif

#include <pwd.h>
#include <grp.h>
#if !defined(_OS_MACOSX) && !defined(_OS_CYGWIN)
#include <shadow.h>
#endif

#include "terminal.h"

extern char **environ ;


namespace aikido {
#if defined (_OS_MACOSX)
// for thread-unsafe functions
OSMutex oslock ;
#endif

void initSystemVars(StaticLink *staticLink) {
    char buffer[1024] ;
    int len ;

    setSysVar ("fileSeparator", "/", staticLink) ;
    setSysVar ("extensionSeparator", ".", staticLink) ;

#if defined(_OS_MACOSX)
    char *username = getlogin() ;
#elif defined(_OS_LINUX)
    char *username = NULL ;
    struct passwd *pw = getpwuid(getuid()) ;
    if (pw != NULL) {
        username = pw->pw_name ;
    }
#else
    char *username = cuserid(NULL) ;
#endif
    if (username == NULL) {
        username = (char*)"unknown" ;
    }
    setSysVar ("username", username, staticLink) ;

#if defined(_OS_SOLARIS)
    if (sysinfo (SI_HOSTNAME, buffer, 1024) != -1) {
        setSysVar ("hostname", buffer, staticLink) ;
    }

    if (sysinfo (SI_SRPC_DOMAIN, buffer, 1024) != -1) {
        setSysVar ("domainname", buffer, staticLink) ;
    }
#endif

#if defined(_OS_LINUX) 
    gethostname (buffer, 1024) ;
    setSysVar ("hostname", buffer, staticLink) ;
    getdomainname (buffer, 1024) ;
    setSysVar ("domainname", buffer, staticLink) ;
#endif


#if defined(_OS_MACOSX)
    gethostname (buffer, 1024) ;
#if 0
    char *dot = strchr (buffer, '.') ;
    if (dot == NULL) {
        setSysVar ("hostname", buffer, staticLink) ;
        setSysVar ("domainname", "unknown", staticLink) ;
    } else {
        *dot = 0 ;
        setSysVar ("hostname", buffer, staticLink) ;
        setSysVar ("domainname", dot+1, staticLink) ;
    }
#else
    setSysVar ("hostname", buffer, staticLink) ;
    ::getdomainname (buffer, 1024) ;
    setSysVar ("domainname", buffer, staticLink) ;
#endif
#endif

    setSysVar ("pid", getpid(), staticLink) ;
    setSysVar ("uid", getuid(), staticLink) ;
    setSysVar ("gid", getgid(), staticLink) ;
    setSysVar ("ppid", getppid(), staticLink) ;
    setSysVar ("pgrp", getpgrp(), staticLink) ;
#if defined(_OS_SOLARIS) 
    setSysVar ("ppgrp", getpgid(getppid()), staticLink) ;
#endif

    setSysVar ("clocksPerSec", CLOCKS_PER_SEC, staticLink) ;

// set the operating system name
#if defined(_OS_SOLARIS)
    setSysVar ("operatingsystem", "Solaris", staticLink) ;
#elif defined(_OS_CYGWIN)
    setSysVar ("operatingsystem", "Cygwin", staticLink) ;
#elif defined(_OS_LINUX)
    setSysVar ("operatingsystem", "Linux", staticLink) ;
#elif defined(_OS_MACOSX)
    setSysVar ("operatingsystem", "Mac OS X", staticLink) ;
#endif


#if defined(_OS_SOLARIS)

    char os[256] ;
    char version[256] ;
    char release[256] ;
    os[0] = 0 ; version[0] = 0 ; release[0] = 0 ;
    sysinfo (SI_SYSNAME, os, 256) ;
    sysinfo (SI_RELEASE, release, 256) ;
    sysinfo (SI_VERSION, version, 256) ;
    sprintf (buffer, "%s %s %s", os, release,version) ;
    setSysVar ("osinfo",  buffer, staticLink) ;

    if (sysinfo (SI_MACHINE, buffer, 1024) != -1) {
        setSysVar ("machine",  buffer, staticLink) ;
    }

    if (sysinfo (SI_ARCHITECTURE, buffer, 1024) != -1) {
        setSysVar ("architecture",  buffer, staticLink) ;
    }

    if (sysinfo (SI_PLATFORM, buffer, 1024) != -1) {
        setSysVar ("platform",  buffer, staticLink) ;
    }

    if (sysinfo (SI_HW_PROVIDER, buffer, 1024) != -1) {
        setSysVar ("manufacturer",  buffer, staticLink) ;
    }

    if (sysinfo (SI_HW_SERIAL, buffer, 1024) != -1) {
        setSysVar ("serialnumber",  buffer, staticLink) ;
    }

#elif defined(_OS_LINUX) && !defined (_OS_CYGWIN)
    struct utsname s ;
    uname (&s) ;
    sprintf (buffer, "%s %s %s", s.sysname, s.release, s.version) ;
    setSysVar ("osinfo",  buffer, staticLink) ;
    setSysVar ("machine",  s.machine, staticLink) ;
    setSysVar ("architecture",  s.machine, staticLink) ;
    setSysVar ("platform",  "unknown", staticLink) ;
    setSysVar ("manufacturer",  "unknown", staticLink) ;
    setSysVar ("serialnumber",  "unknown", staticLink) ;

#elif defined(_OS_MACOSX)
    char release[256] ;
    char revision[256] ;
    char type[256] ;
    size_t ln ;

    int mib[2] ;
    mib[0] = CTL_KERN ;

    mib[1] = KERN_OSRELEASE ;
    ln = sizeof (release) ;
    sysctl (mib, 2, release, &ln, NULL, 0) ;
    
    mib[1] = KERN_OSREV ;
    ln = sizeof (revision) ;
    sysctl (mib, 2, revision, &ln, NULL, 0) ;
    
    mib[1] = KERN_OSTYPE ;
    ln = sizeof (type) ;
    sysctl (mib, 2, type, &ln, NULL, 0) ;

    sprintf (buffer, "%s %s %s", release, revision, type) ;
    setSysVar ("osinfo",  buffer, staticLink) ;

    mib[0] = CTL_HW ;
    mib[1] = HW_MACHINE ;
    ln = sizeof (buffer) ;
    sysctl (mib, 2, buffer, &ln, NULL, 0) ;
    setSysVar ("machine",  buffer, staticLink) ;

    mib[1] = HW_MODEL ;
    ln = sizeof (buffer) ;
    sysctl (mib, 2, buffer, &ln, NULL, 0) ;
    setSysVar ("architecture",  buffer, staticLink) ;

    setSysVar ("manufacturer", "Apple Computer Inc", staticLink) ;
    
#endif

    setSysVar ("hostid", gethostid(), staticLink) ;

#if defined(_OS_SOLARIS) 
    long c ;

    c = sysconf (_SC_PAGESIZE) ;
    if (c != -1) {
       setSysVar ("pagesize", (INTEGER)c, staticLink) ;
    }

    c = sysconf (_SC_PHYS_PAGES) ;
    if (c != -1) {
       setSysVar ("numpages", (INTEGER)c, staticLink) ;
    }

    c = sysconf (_SC_NPROCESSORS_ONLN) ;
    if (c != -1) {
       setSysVar ("numprocessors", (INTEGER)c, staticLink) ;
    }
#elif defined(_OS_LINUX) && !defined(_OS_CYGWIN)
    int nprocs = get_nprocs() ;
    int npages = get_phys_pages() ;
    setSysVar ("numprocessors", (INTEGER)nprocs, staticLink) ;	
    setSysVar ("numpages", (INTEGER)npages, staticLink) ;
    struct sysinfo si ;
    (void)sysinfo (&si) ;
    unsigned long memsize = si.totalram ;
    setSysVar ("pagesize", (INTEGER)memsize/npages, staticLink) ;
    
#elif defined(_OS_MACOSX)
    unsigned int val ;

    mib[0] = CTL_HW ;
    mib[1] = HW_NCPU ;
    ln = sizeof (val) ;

    sysctl (mib, 2, &val, &ln, NULL, 0) ;
    setSysVar ("numprocessors", (INTEGER)val, staticLink) ;

    mib[1] = HW_PAGESIZE ;
    ln = sizeof (val) ;
    sysctl (mib, 2, &val, &ln, NULL, 0) ;
    setSysVar ("pagesize", (INTEGER)val, staticLink) ;

    int pagesize = val ;

    mib[1] = HW_PHYSMEM ;
    ln = sizeof (val) ;
    sysctl (mib, 2, &val, &ln, NULL, 0) ;
    setSysVar ("numpages", (INTEGER)val/pagesize, staticLink) ;


#endif
}

extern "C" {			// these are located using dlsym, so we need their unmangled name

//
// copy the evironment for a new process and add the additional variables
//

bool matchenv (const char *s1, const char *s2) {
  while (*s1 == *s2++) {
      if (*s1 == '=') {
          return true ;
      }
      s1++;
  }
  return false ;
}


int findenv (const char **vars, const char *var) {
    int ct = 0 ;
    while (vars[ct] != 0) {
        if (matchenv (vars[ct], var)) {
            return ct ;
        }
        ct++ ;
    }
    return -(++ct) ;		// negative of size of table
}

// put a variable into the variables list.  There will be enough space for it.
void myputenv (const char **vars, const char *var) {
    int which = findenv (vars, var) ;
    if (which < 0) {			// not found, which is -ve of table size
        which = -which ;
        vars[which-1] = var ;
        vars[which] = NULL ;
    } else {
         vars[which] = var ;
    }
}


const char **copyenv (Value::vector *newvars, char *strings) {
    int nvars = 0 ;
    while (environ[nvars] != 0) {
        nvars++ ;
    }
    nvars++ ;		// space of 0 at end
    int slen = 0 ;

    const char **newenv = new const char *[nvars + newvars->size()] ;
    memcpy (newenv, environ, nvars * sizeof (char*)) ;
    for (int i = 0 ; i < newvars->size() ; i++) {
        const char *envvar = (*newvars)[i].str->c_str() ;
        char *str = &strings[slen] ;
        strcpy (str, envvar) ;
        slen += strlen (envvar) ;
        strings[slen++] = 0 ;
        
        myputenv (newenv, envvar) ;
    }
    return newenv ;
}


//
// try to redirect a stream.  If the call to dup2 fails for EBADF or EINTR
// we try again.  It is probably due to too many threads with open files

static void redirectfd (int fd1, int fd2, int errpipe) {
    int retries = 10 ;
    while (dup2 (fd1, fd2) < 0) {
        if ((errno == EBADF || errno == EINTR)) {
            if (--retries < 0) {
                goto error ;
            } else {
                sleep (1) ;		// and try again
            }
        } else {
        error:
            char err[256] = "Runtime error: Unable to redirect stream for system call: " ;
            strcat (err, strerror (errno)) ;
            strcat (err, "\n") ;
            write (errpipe, err, strlen (err)) ;
            _exit (1) ;
        }
    }
}

AIKIDO_NATIVE (execv) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "execv", "Bad command parameter type") ;
        return 0 ;
    }
    if (paras[2].type != T_VECTOR) {
        throw newParameterException (vm, stack, "execv", "Bad args parameter type") ;
        return 0 ;
    }
    Value::vector *argv = paras[2].vec ;
    const char **args = new const char *[argv->size() + 1] ;
    for (int i = 0 ; i < argv->size() ; i++) {
        if ((*argv)[i].type != T_STRING) {
            throw newParameterException (vm, stack, "execv", "Bad args parameter type") ;
        }
        args[i] = (*argv)[i].str->c_str() ;
    }
    args[argv->size()] = NULL ;
    execv (paras[1].str->c_str(), (char*const*)args) ;
    return 0 ;
}

AIKIDO_NATIVE (execl) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "execl", "Bad command parameter type") ;
        return 0 ;
    }
    const char **args = new const char *[paras.size() - 2 + 1] ;
    for (int i = 2 ; i < paras.size() ; i++) {
        if (paras[i].type != T_STRING) {
            throw newParameterException (vm, stack, "execl", "Bad args parameter type") ;
        }
        args[i-2] = paras[i].str->c_str() ;
    }
    args[paras.size()-2] = NULL ;
    execv (paras[1].str->c_str(), (char*const*)args) ;
    return 0 ;
}

AIKIDO_NATIVE (waitpid) {
    if (paras[1].type != T_INTEGER) {
        throw newParameterException (vm, stack, "waitpid", "Bad pid parameter type") ;
        return 0 ;
    }
    if (paras[2].type != T_ADDRESS) {
        throw newParameterException (vm, stack, "waitpid", "Bad status parameter type") ;
        return 0 ;
    }
    if (paras[3].type != T_INTEGER) {
        throw newParameterException (vm, stack, "waitpid", "Bad flags parameter type") ;
        return 0 ;
    }
    int status ;
    int pid = waitpid (paras[1].integer, &status, paras[3].integer) ;
    *paras[2].addr = status ;
    return pid ;
}


AIKIDO_NATIVE(systemEnv) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "system", "Bad command parameter type") ;
        return 0 ;
    }
    if (paras[2].type != T_VECTOR) {
        throw newParameterException (vm, stack, "system", "Bad environment parameter type") ;
        return 0 ;
    }
    if (paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "system", "Bad dir parameter type") ;
        return 0 ;
    }
   

    int pid ;
    int pipes[2] ;
    if (::pipe (pipes) != 0) {
	throw newException (vm, stack,"unable to create pipe") ;
    }

    Value::vector *vec = new Value::vector ;
    const char *command = paras[1].str->c_str() ;
    Value::vector *env = paras[2].vec ;

    // check the environment variables
    int stringsize = 0 ;

    for (Value::vector::iterator i = env->begin() ; i != env->end() ; i++) {
        Value &val = *i ;
        if (val.type != T_STRING) {
	    throw newException (vm, stack,"Illegal environment variable string") ;
        }
        const char *envvar = val.str->c_str() ;
        stringsize += val.str->size() + 1 ;
    }

    const char **newenv = (const char**)environ ;
    bool freeenv = false ;
    char *envstrings = NULL ;
    if (env->size() != 0) {
        envstrings = new char [stringsize] ;
        newenv = copyenv (env, envstrings) ;
        freeenv = true ;
    }

            
    const char *dir = paras[3].str->c_str() ;

    pid = OS::fork() ;					// only this thread is duplicated in child
    if (pid == 0) {			// child
	redirectfd (pipes[1], 1, 2) ;		// stdout is pipe
	//dup2 (pipes[1], 2) ;		// stderr is pipe
	close (pipes[1]) ;
	close (pipes[0]) ;

        if (dir[0] != 0) {		// change directory if necessary
            chdir (dir) ;
        }
        const char *argvec[4] ;
        argvec[0] = "sh" ;
        argvec[1] = "-c" ;
        argvec[2] = command ;
        argvec[3] = NULL ;
        execve ("/bin/sh", (char*const*)argvec, (char *const*)newenv) ;
#if 0
#if 1
	if (::system (command) != 0) {
            close (pipes[0]) ;
            _exit (errno) ;
        } else {
            close (pipes[0]) ;
            _exit (0) ;
        }
#else
        execcmd (command) ;
#endif
#endif
    } else {
        if (freeenv) {
            delete [] newenv ;
            delete [] envstrings ;
        }
	close (pipes[1]) ;
	char buf[1024] ;
	int n ;
        string *line = new string ;
        char *p ;
	while ((n = read (pipes[0], buf, 1024)) > 0) {
            p = buf ;
            while (n > 0) {
                while (n > 0 && *p != '\n') {
                    *line += *p++ ;
                    n-- ;
                }
                if (n > 0 && *p == '\n') {
                    *line += '\n' ;
                    vec->push_back (Value (line)) ;
                    line = new string ;
                    p++ ;
                    n-- ;
                }
            }
	}
        close (pipes[0]) ;
        if (*line != "") {			// any left over in line buffer
           *line  += '\n' ;
           vec->push_back (Value (line)) ;
        } else {
            delete line ;
        }
	int statloc ;
	waitpid (pid, &statloc, 0) ;
        vm->setLastSystemResult (WEXITSTATUS(statloc)) ;                // set the last system result
        return Value (vec) ;
    }

}

static void streamFromPipe (VirtualMachine *vm, int pipe, const Value &out, bool charmode) {
    char buf[1024] ;
    int n ;
    string *line = new string ;
    char *p ;
    Value right = &out ;
    while ((n = read (pipe, buf, 1024)) > 0) {
        p = buf ;
        while (n > 0) {
            if (charmode) {
                Value ch (*p++) ;
                vm->stream (NULL, ch, right) ;
                n-- ;
            } else{
                while (n-- > 0 && *p != '\n') {
                    *line += *p++ ;
                }
                if (*p == '\n') {
                    *line += '\n' ;
                    Value linev (line) ;
                    vm->stream (NULL, linev, right) ;
                    line = new string ;
                    p++ ;
                }
            }
        }
    }
    close (pipe) ;
    if (!charmode) {
        if (*line != "") {			// any left over in line buffer
           *line  += '\n' ;
           Value linev (line) ;
           vm->stream (NULL, linev, right) ;
        } else {
            delete line ;
        }
    } else {
        delete line ;
    }
}

// like systemEnv except doesn't redirect stdout and returns exit status

AIKIDO_NATIVE(execEnv) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }
    enum {
        COMMAND = 1,
        OUTSTREAM,
        ERRSTREAM,
        ENV,
        DIR
    } ;
    
    if (paras[COMMAND].type != T_STRING) {
        throw newParameterException (vm, stack, "exec", "Bad command parameter type") ;
        return 0 ;
    }
    if (paras[OUTSTREAM].type != T_NONE && paras[OUTSTREAM].type != T_STREAM && paras[OUTSTREAM].type != T_OBJECT && paras[OUTSTREAM].type != T_VECTOR) {
        throw newParameterException (vm, stack, "exec", "Bad output stream parameter type") ;
        return 0 ;
    }
    if (paras[ERRSTREAM].type != T_NONE && paras[ERRSTREAM].type != T_STREAM && paras[ERRSTREAM].type != T_OBJECT && paras[ERRSTREAM].type != T_VECTOR) {
        throw newParameterException (vm, stack, "exec", "Bad error stream parameter type") ;
        return 0 ;
    }
    if (paras[ENV].type != T_VECTOR) {
        throw newParameterException (vm, stack, "exec", "Bad environment parameter type") ;
        return 0 ;
    }
    if (paras[DIR].type != T_STRING) {
        throw newParameterException (vm, stack, "exec", "Bad dir parameter type") ;
        return 0 ;
    }
   
    if (paras[OUTSTREAM].type == T_OBJECT) {
        if (vm->checkForOperator (paras[OUTSTREAM], ARROW) == NULL) {
            throw newParameterException (vm, stack, "exec", "Bad output stream parameter type") ;
            return 0 ;
        }
    }

    if (paras[ERRSTREAM].type == T_OBJECT) {
        if (vm->checkForOperator (paras[ERRSTREAM], ARROW) == NULL) {
            throw newParameterException (vm, stack, "exec", "Bad error stream parameter type") ;
            return 0 ;
        }
    }


    bool outcharmode = false ;
    bool errcharmode = false ;

    if (paras[OUTSTREAM].type == T_STREAM) {
        Stream *s = paras[OUTSTREAM].stream ;
        if (s->mode == Stream::CHARMODE) {
            outcharmode = true ;
        }
    }

    if (paras[ERRSTREAM].type == T_STREAM) {
        Stream *s = paras[ERRSTREAM].stream ;
        if (s->mode == Stream::CHARMODE) {
            errcharmode = true ;
        }
    }

    int pid ;
    int outpipes[2] ;
    int errpipes[2] ;
    if (::pipe (errpipes) != 0) {
	throw newException (vm, stack,"unable to create pipe") ;
    }
    if (::pipe (outpipes) != 0) {
	throw newException (vm, stack,"unable to create pipe") ;
    }

    const char *command = paras[COMMAND].str->c_str() ;
    Value::vector *env = paras[ENV].vec ;
    // check the environment variables
    int stringsize = 0 ;

    for (Value::vector::iterator i = env->begin() ; i != env->end() ; i++) {
        Value &val = *i ;
        if (val.type != T_STRING) {
	    throw newException (vm, stack,"Illegal environment variable string") ;
        }
        const char *envvar = val.str->c_str() ;
        stringsize += val.str->size() + 1 ;
    }

    const char **newenv = (const char**)environ ;
    bool freeenv = false ;
    char *envstrings = NULL ;
    if (env->size() != 0) {
        envstrings = new char [stringsize] ;
        newenv = copyenv (env, envstrings) ;
        freeenv = true ;
    }

    const char *dir = paras[DIR].str->c_str() ;

    pid = OS::fork() ;					// only this thread is duplicated in child
    if (pid == 0) {			// child
	redirectfd (outpipes[1], 1, errpipes[0]) ;		// stdout is pipe
	redirectfd (errpipes[1], 2, errpipes[0]) ;		// stderr is pipe
	close (outpipes[1]) ;
	close (outpipes[0]) ;
	close (errpipes[1]) ;
	close (errpipes[0]) ;

        if (dir[0] != 0) {		// change directory if necessary
            chdir (dir) ;
        }
        const char *argvec[4] ;
        argvec[0] = "sh" ;
        argvec[1] = "-c" ;
        argvec[2] = command ;
        argvec[3] = NULL ;
        execve ("/bin/sh", (char*const*)argvec, (char *const*)newenv) ;
    } else {
        if (freeenv) {
            delete [] newenv ;
            delete [] envstrings ;
        }
	close (outpipes[1]) ;
	close (errpipes[1]) ;

        fd_set fds ;
        fd_set errorfds ;
        int fdsize = std::max (outpipes[0], errpipes[0]) + 1 ;

        string *outline = new string ;		// current output line
        string *errline = new string ;		// current error line

        Value outright = &paras[OUTSTREAM] ;	// output stream address
        Value errright = &paras[ERRSTREAM] ;	// error stream address

        bool outopen = true ;
        bool erropen = true ;

        while (outopen || erropen) {
            const int BUFSIZE = 4096 ;
            char buf[BUFSIZE] ;
            char *p ;
            int n = -1 ;

            FD_ZERO (&fds) ;
            FD_ZERO (&errorfds) ;
            if (outopen) {
                FD_SET (outpipes[0], &fds) ;
                FD_SET (outpipes[0], &errorfds) ;
            }
            if (erropen) {
                FD_SET (errpipes[0], &fds) ;
                FD_SET (errpipes[0], &errorfds) ;
            }

            int n1 = select (fdsize, &fds, NULL, &errorfds, NULL) ;	// wait for some data (no timeout)
            if (n1 < 0) {				// error?
                if (errno == EBADF) {		// one of the files has closed, find out which one
                   char b[10] ;
                   if (read (outpipes[0], b, 1) < 0) {
                       outopen = false ;
                   }
                   if (read (errpipes[0], b, 1) < 0) {
                       erropen = false ;
                   }
                   continue ;
                } else {
                    throw newException (vm, stack, strerror (errno)) ;
                }
            }

            if (FD_ISSET (outpipes[0], &errorfds)) {
                outopen = false ;
            }

            if (FD_ISSET (errpipes[0], &errorfds)) {
                erropen = false ;
            }

            // any data available on the output pipe?
            if (outopen && FD_ISSET (outpipes[0], &fds)) {
                n = read (outpipes[0], buf, BUFSIZE) ;		// read first buffer load
                if (n <= 0) {			// closed?
                    outopen = false ;
                } else {
                    do {
                        p = buf ;			// got some chars, split into lines and write to stream
                        while (n > 0) {
                            if (outcharmode) {
                                Value ch (*p++) ;
                                vm->stream (NULL, ch, outright) ;
                                n-- ;
                            } else{
                                while (n > 0 && *p != '\n') {
                                    *outline += *p++ ;
                                    n-- ;
                                }
                                if (n > 0 && *p == '\n') {
                                    *outline += '\n' ;
                                    Value linev (outline) ;
                                    vm->stream (NULL, linev, outright) ;
                                    outline = new string ;
                                    p++ ;
                                    n-- ;
                                }
                            }
                        }
                    } while (n > 0) ;
                }
            }

            // any data available on error pipe?
            if (erropen && FD_ISSET (errpipes[0], &fds)) {
                n = read (errpipes[0], buf, BUFSIZE) ;
                if (n <= 0) {			// closed?
                    erropen = false ;
                } else {
                    do {
                        p = buf ;			// got some chars, split into lines and write to stream
                        while (n > 0) {
                            if (errcharmode) {
                                Value ch (*p++) ;
                                vm->stream (NULL, ch, errright) ;
                                n-- ;
                            } else{
                                while (n > 0 && *p != '\n') {
                                    *errline += *p++ ;
                                    n-- ;
                                }
                                if (n > 0 && *p == '\n') {
                                    *errline += '\n' ;
                                    Value linev (errline) ;
                                    vm->stream (NULL, linev, errright) ;
                                    errline = new string ;
                                    p++ ;
                                    n-- ;
                                }
                            }
                        }
                    } while (n > 0) ;
                }
            }
        }

        // close this end of the pipes
        close (outpipes[0]) ;
        close (errpipes[0]) ;

        // add any remaining lines to the stream
        if (!outcharmode) {
            if (*outline != "") {			// any left over in line buffer
               *outline  += '\n' ;
               Value linev (outline) ;
               vm->stream (NULL, linev, outright) ;
            } else {
                delete outline ;
            }
        } else {
            delete outline ;
        }
        if (!errcharmode) {
            if (*errline != "") {			// any left over in line buffer
               *errline  += '\n' ;
               Value linev (errline) ;
               vm->stream (NULL, linev, errright) ;
            } else {
                delete errline ;
            }
        } else {
            delete errline ;
        }

        //streamFromPipe (vm, outpipes[1], paras[OUTSTREAM], outcharmode) ;
        //streamFromPipe (vm, errpipes[1], paras[ERRSTREAM], errcharmode) ;
	int statloc ;
	waitpid (pid, &statloc, 0) ;
        
        if (WIFEXITED(statloc)) {
            return (INTEGER)WEXITSTATUS(statloc) ;
        }
        return Value ((INTEGER) statloc) ;
    }


}

// like execEnv except return stream to be used as standard input to command

AIKIDO_NATIVE(pipeEnv) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }
    enum {
        COMMAND = 1,
        REDIRECTERR,
        ENV,
        DIR
    } ;
    
    if (paras[COMMAND].type != T_STRING) {
        throw newParameterException (vm, stack, "pipe", "Bad command parameter type") ;
        return 0 ;
    }
    if (!isIntegral (paras[REDIRECTERR])) {
        throw newParameterException (vm, stack, "pipe", "Bad redirectStderr parameter type") ;
        return 0 ;
    }
    if (paras[ENV].type != T_VECTOR) {
        throw newParameterException (vm, stack, "pipe", "Bad environment parameter type") ;
        return 0 ;
    }
    if (paras[DIR].type != T_STRING) {
        throw newParameterException (vm, stack, "pipe", "Bad dir parameter type") ;
        return 0 ;
    }
   
    bool redirectstderr = getInt (paras[REDIRECTERR]) != 0 ;

    int pid ;
    int outpipes[2] ;
    int inpipes[2] ;
    if (::pipe (inpipes) != 0) {
	throw newException (vm, stack,"unable to create pipe") ;
    }
    if (::pipe (outpipes) != 0) {
	throw newException (vm, stack,"unable to create pipe") ;
    }

    const char *command = paras[COMMAND].str->c_str() ;
    Value::vector *env = paras[ENV].vec ;
    // check the environment variables
    int stringsize = 0 ;

    for (Value::vector::iterator i = env->begin() ; i != env->end() ; i++) {
        Value &val = *i ;
        if (val.type != T_STRING) {
	    throw newException (vm, stack,"Illegal environment variable string") ;
        }
        const char *envvar = val.str->c_str() ;
        stringsize += val.str->size() + 1 ;
    }

    const char **newenv = (const char**)environ ;
    bool freeenv = false ;
    char *envstrings = NULL ;
    if (env->size() != 0) {
        envstrings = new char [stringsize] ;
        newenv = copyenv (env, envstrings) ;
        freeenv = true ;
    }

    const char *dir = paras[DIR].str->c_str() ;

    PipeStream *pipestream = new PipeStream (outpipes[0], inpipes[1]) ;

    pid = OS::fork() ;					// only this thread is duplicated in child
    if (pid == 0) {			// child
	redirectfd (inpipes[0], 0, outpipes[1]) ;		// stdin is pipe
	redirectfd (outpipes[1], 1, outpipes[1]) ;		// stdout is pipe
        if (redirectstderr) {
	    redirectfd (outpipes[1], 2, outpipes[1]) ;		// stderr is pipe
        }
	close (inpipes[1]) ;
	close (inpipes[0]) ;
	close (outpipes[1]) ;
	close (outpipes[0]) ;

        if (dir[0] != 0) {		// change directory if necessary
            chdir (dir) ;
        }
        const char *argvec[4] ;
        argvec[0] = "sh" ;
        argvec[1] = "-c" ;
        argvec[2] = command ;
        argvec[3] = NULL ;
        execve ("/bin/sh", (char*const*)argvec, (char *const*)newenv) ;
    } else {
        if (freeenv) {
            delete [] newenv ;
            delete [] envstrings ;
        }
        pipestream->setThreadID ((OSThread_t)pid) ;		// not really a thread, but hey
	close (inpipes[0]) ;
	close (outpipes[1]) ;
        return pipestream ;
    }
}

AIKIDO_NATIVE(pipeclose) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "pipeclose", "Bad stream type") ;
        return 0 ;
    }
    Stream *s = paras[1].stream ;
    int pid = (int)s->getThreadID() ;
    s->close() ;
    int statloc ;
    waitpid (pid, &statloc, 0) ;
    if (WIFEXITED(statloc)) {
        if (WEXITSTATUS(statloc) != 0) {
            return (INTEGER)errno ;
        } else {
            return 0 ;
        }
    }
    return Value ((INTEGER) statloc) ;
}

AIKIDO_NATIVE(pipewait) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "pipewait", "Bad stream type") ;
        return 0 ;
    }
    Stream *s = paras[1].stream ;
    int pid = (int)s->getThreadID() ;
    int statloc ;
    waitpid (pid, &statloc, 0) ;
    if (WIFEXITED(statloc)) {
        if (WEXITSTATUS(statloc) != 0) {
            return (INTEGER)errno ;
        } else {
            return 0 ;
        }
    }
    return Value ((INTEGER) statloc) ;
}

AIKIDO_NATIVE(stat) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "stat", "Invalid filename") ;
    }
    struct stat st ;

    std::string filename = lockFilename (b, paras[1]) ;

    if (stat (filename.c_str(), &st) != 0) {
        return Value ((Object*)NULL) ;
    }

    // static link for stat object is same as for stat function (System block)

    Object *statobj = new (statClass->stacksize) Object (statClass, staticLink, stack, stack->inst) ;
    statobj->ref++ ;

    statobj->varstack[0] = Value (statobj) ;
    // construct the object

    vm->execute (statClass, statobj, staticLink, 0) ;

    statobj->varstack[1] = Value((UINTEGER)st.st_mode) ;
    statobj->varstack[2] = Value((UINTEGER)st.st_ino) ;
    statobj->varstack[3] = Value((UINTEGER)st.st_dev) ;
    statobj->varstack[4] = Value((UINTEGER)st.st_rdev) ;
    statobj->varstack[5] = Value((UINTEGER)st.st_nlink) ;
    statobj->varstack[6] = Value((UINTEGER)st.st_uid) ;
    statobj->varstack[7] = Value((UINTEGER)st.st_gid) ;
    statobj->varstack[8] = Value((UINTEGER)st.st_size) ;
    statobj->varstack[9] = Value((INTEGER)st.st_atime * 1000000) ;
    statobj->varstack[10] = Value((INTEGER)st.st_mtime * 1000000) ;
    statobj->varstack[11] = Value((INTEGER)st.st_ctime * 1000000) ;
    statobj->varstack[12] = Value((UINTEGER)st.st_blksize) ;
    statobj->varstack[13] = Value((UINTEGER)st.st_blocks) ;
    statobj->ref-- ;

    return statobj ;
}

AIKIDO_NATIVE(lstat) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "stat", "Invalid filename") ;
    }
    struct stat st ;

    std::string filename = lockFilename (b, paras[1]) ;

    if (lstat (filename.c_str(), &st) != 0) {
        return Value ((Object*)NULL) ;
    }

    // static link for stat object is same as for stat function (System block)

    Object *statobj = new (statClass->stacksize) Object (statClass, staticLink, stack, stack->inst) ;
    statobj->ref++ ;

    statobj->varstack[0] = Value (statobj) ;
    // construct the object

    vm->execute (statClass, statobj, staticLink, 0) ;

    statobj->varstack[1] = Value((UINTEGER)st.st_mode) ;
    statobj->varstack[2] = Value((UINTEGER)st.st_ino) ;
    statobj->varstack[3] = Value((UINTEGER)st.st_dev) ;
    statobj->varstack[4] = Value((UINTEGER)st.st_rdev) ;
    statobj->varstack[5] = Value((UINTEGER)st.st_nlink) ;
    statobj->varstack[6] = Value((UINTEGER)st.st_uid) ;
    statobj->varstack[7] = Value((UINTEGER)st.st_gid) ;
    statobj->varstack[8] = Value((UINTEGER)st.st_size) ;
    statobj->varstack[9] = Value((INTEGER)st.st_atime * 1000000) ;
    statobj->varstack[10] = Value((INTEGER)st.st_mtime * 1000000) ;
    statobj->varstack[11] = Value((INTEGER)st.st_ctime * 1000000) ;
    statobj->varstack[12] = Value((UINTEGER)st.st_blksize) ;
    statobj->varstack[13] = Value((UINTEGER)st.st_blocks) ;
    statobj->ref-- ;

    return statobj ;
}

AIKIDO_NATIVE(getUser) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }

    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "getUser", "Invalid username") ;
    }
    struct passwd pwd ;
#if !defined (_OS_MACOSX) && !defined(_OS_CYGWIN)
    struct spwd spwd ;
#endif
    char buffer1[4096] ;
    char buffer2[4096] ;
    

#if defined(_OS_LINUX) && !defined(_OS_CYGWIN)
    struct passwd *pwdr ;
    struct spwd *spwdr ;
    if (getpwnam_r (paras[1].str->c_str(), &pwd, buffer1, 4096, &pwdr) < 0) {
        return Value ((Object*)NULL) ;
    }
    if (getspnam_r (paras[1].str->c_str(), &spwd, buffer2, 4096, &spwdr) < 0) {
        return Value ((Object*)NULL) ;
    }
#elif defined (_OS_MACOSX)
    struct passwd *p = getpwnam (paras[1].str->c_str()) ;
    if (p == NULL) {
        return Value ((Object*)NULL) ;
    }
    pwd = *p ;
#elif defined(_OS_SOLARIS) && (defined(_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
    struct passwd *pwdr ;
    if (getpwnam_r (paras[1].str->c_str(), &pwd, buffer1, 4096, &pwdr) < 0) {
        return Value ((Object*)NULL) ;
    }
    if (getspnam_r (paras[1].str->c_str(), &spwd, buffer1, 4096) < 0) {
        return Value ((Object*)NULL) ;
    }
#elif defined(_OS_CYGWIN)
#else
    if (getpwnam_r (paras[1].str->c_str(), &pwd, buffer1, 4096) == NULL) {
        return Value ((Object*)NULL) ;
    }
    if (getspnam_r (paras[1].str->c_str(), &spwd, buffer1, 4096) == NULL) {
        return Value ((Object*)NULL) ;
    }
#endif

    // static link for stat object is same as for stat function (System block)

    Object *userobj = new (userClass->stacksize) Object (userClass, staticLink, stack, stack->inst) ;
    userobj->ref++ ;

    userobj->varstack[0] = Value (userobj) ;
    // construct the object

    vm->execute (userClass, userobj, staticLink, 0) ;

    userobj->varstack[1] = pwd.pw_name ;
    userobj->varstack[2] = pwd.pw_uid ;
    userobj->varstack[3] = pwd.pw_gid ;
    userobj->varstack[4] = pwd.pw_gecos ;
    userobj->varstack[5] = pwd.pw_dir ;
    userobj->varstack[6] = pwd.pw_shell ;
#if defined(_OS_MACOSX)
    userobj->varstack[7] = pwd.pw_passwd ;		// password
#elif defined(_OS_LINUX) && !defined(_OS_CYGWIN)
    if (spwdr != NULL) {
        userobj->varstack[7] = spwdr->sp_pwdp ;		// password
    } else {
        userobj->varstack[7] = "" ;
    }
#elif defined(_OS_CYGWIN)
#else
    userobj->varstack[7] = spwd.sp_pwdp ;		// password
#endif

    userobj->ref-- ;


    return userobj ;
}

AIKIDO_NATIVE(getUserName) {
    if (paras[1].type != T_INTEGER) {
        throw newParameterException (vm, stack, "getUserName", "Invalid user id") ;
    }
    struct passwd pwd ;
    char buffer1[4096] ;
    char buffer2[4096] ;
    

#if defined(_OS_LINUX) && !defined(_OS_CYGWIN)
    struct passwd *pwdr ;
    if (getpwuid_r (paras[1].integer, &pwd, buffer1, 4096, &pwdr) < 0) {
        throw newException (vm, stack, "Failed to get user name") ;
    }
#elif defined (_OS_MACOSX)
    struct passwd *p = getpwuid (paras[1].integer) ;
    if (p == NULL) {
        throw newException (vm, stack, "Failed to get user name") ;
    }
    pwd = *p ;
#elif defined(_OS_SOLARIS) && (defined(_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
    struct passwd *pwdr ;
    if (getpwuid_r (paras[1].integer, &pwd, buffer1, 4096, &pwdr) < 0) {
        throw newException (vm, stack, "Failed to get user name") ;
    }
#elif defined(_OS_CYGWIN)
#else
    if (getpwuid_r (paras[1].integer, &pwd, buffer1, 4096) == NULL) {
        throw newException (vm, stack, "Failed to get user name") ;
    }
#endif

    return new string (pwd.pw_name) ;
}

AIKIDO_NATIVE(getGroupName) {
    if (paras[1].type != T_INTEGER) {
        throw newParameterException (vm, stack, "getGroupName", "Invalid group id") ;
    }
    struct group grp ;
    char buffer1[4096] ;
    char buffer2[4096] ;
    

#if defined(_OS_LINUX) && !defined(_OS_CYGWIN)
    struct group *grpr ;
    if (getgrgid_r (paras[1].integer, &grp, buffer1, 4096, &grpr) < 0) {
        throw newException (vm, stack, "Failed to get group name") ;
    }
#elif defined (_OS_MACOSX)
    struct group *p = getgrgid (paras[1].integer) ;
    if (p == NULL) {
        throw newException (vm, stack, "Failed to get group name") ;
    }
    grp = *p ;
#elif defined(_OS_SOLARIS) && (defined(_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
    struct grp *grpr ;
    if (getgrgid_r (paras[1].integer, &grp, buffer1, 4096, &grpr) < 0) {
        throw newException (vm, stack, "Failed to get group name") ;
    }
#elif defined(_OS_CYGWIN)
#else
    if (getpwgid_r (paras[1].integer, &grp, buffer1, 4096) == NULL) {
        throw newException (vm, stack, "Failed to get group name") ;
    }
#endif

    return new string (grp.gr_name) ;
}

AIKIDO_NATIVE(readdir) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "readdir", "Invalid directory name") ;
        return 0 ;
    }
    DIR *dir ;
    struct dirent *entry ;
    std::string dirname = lockFilename (b, paras[1]) ;

    dir = opendir (dirname.c_str()) ;
    if (dir == NULL) {
        throw newFileException (vm, stack,*paras[1].str, "unable to open directory") ;
    }
    Value::vector *vec = new Value::vector ;
    while ((entry = readdir (dir)) != NULL) {
        if (strcmp (entry->d_name, ".") != 0 && strcmp (entry->d_name, "..") != 0) {
            vec->push_back (Value (new string (entry->d_name))) ;
	}
    }
    closedir (dir) ;
    return vec ;
}

AIKIDO_NATIVE(chdir) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "chdir", "Invalid directory name") ;
        return 0 ;
    }
    std::string dirname = lockFilename (b, paras[1]) ;

    if (chdir (dirname.c_str()) != 0) {
        throw newFileException (vm, stack,*paras[1].str, "unable to change directory") ;
    }
    current_directory = dirname ;
    return 0 ;
}

AIKIDO_NATIVE(getwd) {
    char dir[1024] ;
    std::string dirname ;

    if (b->properties & LOCKDOWN) {
        dirname = current_directory ;
        //printf ("dirname: %s, lockdown: %s\n", dirname.c_str(), b->lockdownRootdir.c_str()) ;
        if (dirname.size() > b->lockdownRootdir.size()) {
            //printf ("substr: %s\n", dirname.substr (0, b->lockdownRootdir.size()).c_str()) ;
            if (dirname.substr (0, b->lockdownRootdir.size()) == b->lockdownRootdir) {
                dirname = dirname.substr (b->lockdownRootdir.size() - 1) ;
            } else {
                return new string ("/") ;
            }
        } else {
            return new string("/") ;
        }
    } else {
        dirname = getcwd(dir, 1024) ;
    }
    return Value (new string (dirname)) ;
}

// get the time of day in microseconds UTC

AIKIDO_NATIVE(time) {
    struct ::timeval t ;
    gettimeofday (&t, NULL) ;
    INTEGER tm = (INTEGER)t.tv_sec * 1000000 + t.tv_usec ;
    return tm ;
}

static Value toDateObject (VirtualMachine *vm, StaticLink *staticLink, StackFrame *stack, struct tm *tm) {
    // static link for date object is same as for date function (System block)

    Object *dateobj = new (dateClass->stacksize) Object (dateClass, staticLink, stack, stack->inst) ;
    dateobj->ref++ ;

    dateobj->varstack[0] = Value (dateobj) ;

    // construct the object

    vm->execute (dateClass, dateobj, staticLink, 0) ;

    dateobj->varstack[1] = Value("") ;          // parameter
    dateobj->varstack[2] = Value (tm->tm_sec) ;
    dateobj->varstack[3] = Value (tm->tm_min) ;
    dateobj->varstack[4] = Value (tm->tm_hour) ;
    dateobj->varstack[5] = Value (tm->tm_mday) ;
    dateobj->varstack[6] = Value (tm->tm_mon) ;
    dateobj->varstack[7] = Value (tm->tm_year) ;
    dateobj->varstack[8] = Value (tm->tm_wday) ;
    dateobj->varstack[9] = Value (tm->tm_yday) ;
    dateobj->varstack[10] = Value (tm->tm_isdst) ;
#if defined (_OS_MACOSX)
    dateobj->varstack[11] = tm->tm_gmtoff;
#elif defined (_OS_LINUX) && !defined(_OS_CYGWIN)
    dateobj->varstack[11] = timezone ;
#elif defined(_OS_CYGWIN)
#else
    dateobj->varstack[11] = tm->tm_isdst ? altzone : timezone ;
#endif
    dateobj->varstack[12] = tzname[tm->tm_isdst > 0 ? 1 : 0] ;
    dateobj->ref-- ;
    return dateobj ;
}


AIKIDO_NATIVE(date) {
    time_t clock = time(NULL) ;
    struct tm timebuf ;
    struct tm *tm = localtime_r (&clock, &timebuf) ;
    return toDateObject (vm, staticLink, stack, tm) ;
}


AIKIDO_NATIVE(gmdate) {
    time_t clock = time(NULL) ;
    struct tm timebuf ;
    struct tm *tm = gmtime_r (&clock, &timebuf) ;
    return toDateObject (vm, staticLink, stack, tm) ;
}


AIKIDO_NATIVE(makedate) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "makedate", "Invalid time parameter") ;
    }
    time_t clock = (time_t)(getInt(paras[1]) / 1000000) ;		// make into seconds
    struct tm timebuf ;
    struct tm *tm = localtime_r (&clock, &timebuf) ;

    return toDateObject (vm, staticLink, stack, tm) ;
}


AIKIDO_NATIVE(makegmdate) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "makegmdate", "Invalid time parameter") ;
    }
    time_t clock = (time_t)(getInt(paras[1]) / 1000000) ;		// make into seconds
    struct tm timebuf ;
    struct tm *tm = gmtime_r (&clock, &timebuf) ;

    return toDateObject (vm, staticLink, stack, tm) ;
}



#if defined(_OS_SOLARIS) 
static int string2resource (VirtualMachine *vm, StackFrame *stack, string &resname) {
    int resource ;
    if (resname == "cputime") {
        resource = RLIMIT_CPU ;
    } else if (resname == "filesize") {
        resource = RLIMIT_FSIZE ;
    } else if (resname == "datasize") {
        resource = RLIMIT_DATA ;
    } else if (resname == "stacksize") {
        resource = RLIMIT_STACK ;
    } else if (resname == "coredumpsize") {
        resource = RLIMIT_CORE ;
    } else if (resname == "descriptors") {
        resource = RLIMIT_NOFILE ;
    } else if (resname == "memorysize") {
        resource = RLIMIT_VMEM ;
    } else {
        throw newParameterException (vm, stack, "get/setlimit", "Invalid resource name") ;
    }
    return resource ;
}


AIKIDO_NATIVE (setlimit) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "setlimit", "Invalid resource name type") ;
        return 0 ;
    }
    if (paras[2].type != T_INTEGER) {
        throw newParameterException (vm, stack, "setlimit", "Invalid resource value type") ;
        return 0 ;
    }
    string &resname = *paras[1].str ;
    int value = paras[2].integer ;

    struct rlimit lim ;
    int resource = string2resource (vm, stack, resname) ;

    int r = getrlimit (resource, &lim) ;
    if (r < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    switch (value) {
    case -1:
        lim.rlim_cur = RLIM_INFINITY ;
        break ;
    case -2:
        lim.rlim_cur = RLIM_SAVED_MAX ;
        break ;
    case -3:
        lim.rlim_cur = RLIM_SAVED_CUR ;
        break ;
    default:
        lim.rlim_cur = value ;
    }
    r = setrlimit (resource, &lim) ;
    if (r < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    return 0 ;
}

AIKIDO_NATIVE (getlimit) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "getlimit", "Invalid resource name type") ;
        return 0 ;
    }
    string &resname = *paras[1].str ;

    struct rlimit lim ;
    int resource = string2resource (vm, stack, resname) ;

    int r = getrlimit (resource, &lim) ;
    if (r < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    int value = lim.rlim_cur ;
    switch (value) {
    case RLIM_INFINITY:
        return (INTEGER)-1 ;
    case RLIM_SAVED_MAX:
        return (INTEGER)-2 ;
    case RLIM_SAVED_CUR:
        return (INTEGER)-3 ;
    default:
        return value ;
    }
}
#elif defined(_OS_LINUX) && !defined(_OS_CYGWIN)
static enum __rlimit_resource string2resource (VirtualMachine *vm, StackFrame *stack, string &resname) {
    enum __rlimit_resource resource ;
    if (resname == "cputime") {
        resource = RLIMIT_CPU ;
    } else if (resname == "filesize") {
        resource = RLIMIT_FSIZE ;
    } else if (resname == "datasize") {
        resource = RLIMIT_DATA ;
    } else if (resname == "stacksize") {
        resource = RLIMIT_STACK ;
    } else if (resname == "coredumpsize") {
        resource = RLIMIT_CORE ;
    } else if (resname == "descriptors") {
        resource = RLIMIT_NOFILE ;
    } else if (resname == "memorysize") {
        resource = RLIMIT_RSS ;
    } else {
        throw newParameterException (vm, stack, "get/setlimit", "Invalid resource name") ;
    }
    return resource ;
}


AIKIDO_NATIVE (setlimit) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "setlimit", "Invalid resource name type") ;
        return 0 ;
    }
    if (paras[2].type != T_INTEGER) {
        throw newParameterException (vm, stack, "setlimit", "Invalid resource value type") ;
        return 0 ;
    }
    string &resname = *paras[1].str ;
    int value = paras[2].integer ;

    struct rlimit lim ;
    enum __rlimit_resource resource = string2resource (vm, stack, resname) ;

    int r = getrlimit (resource, &lim) ;
    if (r < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    switch (value) {
    case -1:
        lim.rlim_cur = RLIM_INFINITY ;
        break ;
    case -2:
        lim.rlim_cur = RLIM_SAVED_MAX ;
        break ;
    case -3:
        lim.rlim_cur = RLIM_SAVED_CUR ;
        break ;
    default:
        lim.rlim_cur = value ;
    }
    r = setrlimit (resource, &lim) ;
    if (r < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    return 0 ;
}

AIKIDO_NATIVE (getlimit) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "getlimit", "Invalid resource name type") ;
        return 0 ;
    }
    string &resname = *paras[1].str ;

    struct rlimit lim ;
    enum __rlimit_resource resource = string2resource (vm, stack, resname) ;

    int r = getrlimit (resource, &lim) ;
    if (r < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    int value = lim.rlim_cur ;
    if (value == RLIM_INFINITY) {		//linux doesn't have SAVED_MAX and SAVED_CUR
        return (INTEGER)-1 ;
    }
    return value ;
}
#elif defined(_OS_MACOSX)
static int string2resource (VirtualMachine *vm, StackFrame *stack, string &resname) {
    int resource ;
    if (resname == "cputime") {
        resource = RLIMIT_CPU ;
    } else if (resname == "filesize") {
        resource = RLIMIT_FSIZE ;
    } else if (resname == "datasize") {
        resource = RLIMIT_DATA ;
    } else if (resname == "stacksize") {
        resource = RLIMIT_STACK ;
    } else if (resname == "coredumpsize") {
        resource = RLIMIT_CORE ;
    } else if (resname == "descriptors") {
        resource = RLIMIT_NOFILE ;
    } else if (resname == "memorysize") {
        resource = RLIMIT_RSS ;
    } else {
        throw newParameterException (vm, stack, "get/setlimit", "Invalid resource name") ;
    }
    return resource ;
}


AIKIDO_NATIVE (setlimit) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "setlimit", "Invalid resource name type") ;
        return 0 ;
    }
    if (paras[2].type != T_INTEGER) {
        throw newParameterException (vm, stack, "setlimit", "Invalid resource value type") ;
        return 0 ;
    }
    string &resname = *paras[1].str ;
    int value = paras[2].integer ;

    struct rlimit lim ;
    int resource = string2resource (vm, stack, resname) ;

    int r = getrlimit (resource, &lim) ;
    if (r < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    switch (value) {
    case -1:
        lim.rlim_cur = RLIM_INFINITY ;
        break ;
    default:
        lim.rlim_cur = value ;
    }
    r = setrlimit (resource, &lim) ;
    if (r < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    return 0 ;
}

AIKIDO_NATIVE (getlimit) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "getlimit", "Invalid resource name type") ;
        return 0 ;
    }
    string &resname = *paras[1].str ;

    struct rlimit lim ;
    int resource = string2resource (vm, stack, resname) ;

    int r = getrlimit (resource, &lim) ;
    if (r < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    int value = lim.rlim_cur ;
    switch (value) {
    case RLIM_INFINITY:
        return (INTEGER)-1 ;
    default:
        return value ;
    }
}
#elif defined (_OS_CYGWIN)
AIKIDO_NATIVE (setlimit) {
    return 0 ;
}

AIKIDO_NATIVE (getlimit) {
    return 0 ;
}


#endif


AIKIDO_NATIVE (getenv) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "getenv", "invalid environment variable") ;
    }
    const char*r = ::getenv (paras[1].str->c_str()) ;
    if (r == NULL) {
        return (Object*)NULL ;
    }
    return r ;
}

AIKIDO_NATIVE (setenv) {
    if (paras[1].type != T_STRING || paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "setenv", "invalid environment variable or value") ;
    }
    char *buf ;				// cannot be automatic for putenv
    const char *name = paras[1].str->c_str() ;
    const char *value = paras[2].str->c_str() ;

    buf = new char[strlen (name) + strlen (value) + 2] ;
    sprintf (buf, "%s=%s", name, value) ;
    ::putenv (buf) ;			// makes the arg part of the environment
    return 0 ;
}


//
// perform a select operation on a socket
//
// args:
//   1: stream
//   2: timeout in microseconds
//
// returns:
//   true if there is data ready on the stream
//

AIKIDO_NATIVE (select) {
    int fd = paras[1].stream->getInputFD() ;
    if (paras[1].stream->availableChars() > 0) {
        return 1 ;
    }
    struct timeval timeout ;
    timeout.tv_sec = paras[2] / 1000000 ;
    timeout.tv_usec = paras[2] % 1000000 ;
    fd_set fds ;
    FD_ZERO (&fds) ;
    FD_SET (fd, &fds) ;
    int n = select (256, &fds, NULL, NULL, &timeout) ;
    if (n < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    if (n == 0) {
        return 0 ;
    }
    if (FD_ISSET (fd, &fds)) {
        return 1 ;
    }
    return 0 ;
}


AIKIDO_NATIVE (kill) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }
    if (!isIntegral (paras[1]) && paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "kill", "Illegal pid type") ;
    }
    if (!isIntegral (paras[2])) {
        throw newParameterException (vm, stack, "kill", "Illegal signal type") ;
    }
    if (paras[1].type == T_STREAM) {
        int pid = (int)paras[1].stream->getThreadID() ;
        return (INTEGER)(kill (pid, getInt (paras[2])) == 0);
    } else {
        return (INTEGER)(kill (getInt (paras[1]), getInt (paras[2])) == 0) ;
    }
}

#if defined(_OS_MACOSX) 		// macosx uses bsd style signal handling

typedef void (*SIG_TYP)(int) ;

AIKIDO_NATIVE (sigset) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigset", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sigset", "Illegal signal value") ;
    }
    SIG_TYP result ;
    Closure *prev ;

    if (paras[2].type == T_CLOSURE && paras[2].closure->type == T_FUNCTION) {
        Function *f = (Function*)paras[2].closure->block ;
        if (f->isNative()) {
            throw newParameterException (vm, stack, "sigset", "Illegal signal handler - function can't be native") ;
        }
        InterpretedBlock *func = reinterpret_cast<InterpretedBlock*>(f) ;
        parserLock.acquire(true) ;
        prev = signals[sig] ;
        signals[sig] = paras[2].closure ;
        incRef (paras[2].closure, closure) ;
        parserLock.release(true) ;

        result = signal (sig, signalHit) ;			// set the signal handler
    } else if (paras[2].type == T_INTEGER) {
        SIG_TYP handler = (SIG_TYP)paras[2].integer ;
        parserLock.acquire(true) ;
        prev = signals[sig] ;
        signals[sig] = NULL ;
        parserLock.release(true) ;

        result = signal (sig, (void (*)(int))handler) ;
    } else {
       throw newParameterException (vm, stack, "sigset", "Illegal signal handler type") ;
    }

    if (result == signalHit) {		// had we previously handled the signal?
        if (prev == NULL) {
            return (Object*)NULL ;
        } else {
            return prev ;
        }
    } else if (result == SIG_ERR) {
        throw newException (vm, stack, string ("Error setting signal: ") + strerror(errno)) ;
    } else {
        return (INTEGER)result ;
    }

}

AIKIDO_NATIVE (sigrelse) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigrelse", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sigrelse", "Illegal signal value") ;
    }
    signal (sig, SIG_DFL) ;
    return 0 ;
}

AIKIDO_NATIVE (sighold) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sighold", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sighold", "Illegal signal value") ;
    }
    return 0 ;
}

AIKIDO_NATIVE (sigignore) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigignore", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sigignore", "Illegal signal value") ;
    }
    signal (sig, SIG_IGN) ;
    return 0 ;
}

AIKIDO_NATIVE (sigpause) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigpause", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sigpause", "Illegal signal value") ;
    }
    return 0 ;
}

#else
#if defined(_OS_LINUX) && !defined(_OS_CYGWIN)
#define SIG_TYP __sighandler_t
extern "C" {
    SIG_TYP sigset (int, SIG_TYP) ;
    void sigrelse (int) ;
    void sighold (int) ;
    void sigignore (int) ;
    //void sigpause (int) ;
}
#endif


AIKIDO_NATIVE (sigset) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigset", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sigset", "Illegal signal value") ;
    }
#if !defined (_OS_CYGWIN)

    SIG_TYP result ;
    Closure *prev ;

    if (paras[2].type == T_CLOSURE && paras[2].closure->type == T_FUNCTION) {
        Function *f = (Function*)paras[2].closure->block ;
        if (f->isNative()) {
            throw newParameterException (vm, stack, "sigset", "Illegal signal handler - function can't be native") ;
        }
        InterpretedBlock *func = reinterpret_cast<InterpretedBlock*>(f) ;
        parserLock.acquire(true) ;
        prev = signals[sig] ;
        signals[sig] = paras[2].closure ;
        incRef (paras[2].closure, closure) ;
        parserLock.release(true) ;

        result = sigset (sig, signalHit) ;			// set the signal handler
    } else if (paras[2].type == T_INTEGER) {
        SIG_TYP handler = reinterpret_cast<SIG_TYP>(paras[2].integer) ;
        parserLock.acquire(true) ;
        prev = signals[sig] ;
        signals[sig] = NULL ;
        parserLock.release(true) ;

        result = sigset (sig, handler) ;
    } else {
       throw newParameterException (vm, stack, "sigset", "Illegal signal handler type") ;
    }

    if (result == signalHit) {		// had we previously handled the signal?
        if (prev == NULL) {
            return (Object*)NULL ;
        } else {
            return prev ;
        }
#if !defined(_OS_LINUX)
    } else if (result == SIG_HOLD) {
        return 2 ;
#endif
    } else if (result == SIG_ERR) {
        throw newException (vm, stack, string ("Error setting signal: ") + strerror(errno)) ;
    } else {
        return reinterpret_cast<INTEGER>(result) ;
    }
#else
    return 0 ;
#endif
}

AIKIDO_NATIVE (sigrelse) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigrelse", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sigrelse", "Illegal signal value") ;
    }
#if !defined(_OS_CYGWIN)
    sigrelse (sig) ;
#endif
    return 0 ;
}

AIKIDO_NATIVE (sighold) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sighold", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sighold", "Illegal signal value") ;
    }
#if !defined(_OS_CYGWIN)
    sighold (sig) ;
#endif
    return 0 ;
}

AIKIDO_NATIVE (sigignore) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigignore", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sigignore", "Illegal signal value") ;
    }
#if !defined(_OS_CYGWIN)
    sigignore (sig) ;
#endif
    return 0 ;
}

AIKIDO_NATIVE (sigpause) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigpause", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sigpause", "Illegal signal value") ;
    }
#if !defined(_OS_CYGWIN)
    sigpause (sig) ;
#endif
    return 0 ;
}

#endif // defined (_OS_MACOSX)


AIKIDO_NATIVE (rename) {
    if (paras[1].type != T_STRING || paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "rename", "Illegal filename type") ;
    }
    std::string filename1 = lockFilename (b, paras[1]) ;
    std::string filename2 = lockFilename (b, paras[2]) ;

    int err = rename (filename1.c_str(), filename2.c_str()) ;
    if (err == -1) {
         throw newException (vm, stack, strerror (errno)) ;
    }
    return 0 ;
}

AIKIDO_NATIVE (remove) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "remove", "Illegal filename type") ;
    }
    std::string filename = lockFilename (b, paras[1]) ;
    struct stat st ;
    int err = stat (filename.c_str(), &st) ;
    if (err != 0) {
         throw newException (vm, stack, strerror (errno)) ;
    }
    if (S_ISDIR(st.st_mode)) {
        err = rmdir (filename.c_str()) ;
    } else {
        err = unlink (filename.c_str()) ;
    }
    if (err == -1) {
         throw newException (vm, stack, strerror (errno)) ;
    }
    return 0 ;
}

AIKIDO_NATIVE (chmod) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "chmod", "Illegal filename type") ;
    }
    if (paras[2].type != T_INTEGER) {
        throw newParameterException (vm, stack, "chmod", "Illegal mode type") ;
    }
    std::string filename = lockFilename (b, paras[1]) ;
    int mode = getInt (paras[2]) ;

    int err = chmod (filename.c_str(), mode) ;
    if (err == -1) {
         throw newException (vm, stack, strerror (errno)) ;
    }
    return 0 ;
}

AIKIDO_NATIVE (chown) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "chown", "Illegal filename type") ;
    }
    if (paras[2].type != T_INTEGER) {
        throw newParameterException (vm, stack, "chown", "Illegal uid type") ;
    }
    if (paras[3].type != T_INTEGER) {
        throw newParameterException (vm, stack, "chown", "Illegal gid type") ;
    }
    std::string filename = lockFilename (b, paras[1]) ;
    int uid = getInt (paras[2]) ;
    int gid = getInt (paras[3]) ;

    int err = chown (filename.c_str(), uid, gid) ;
    if (err == -1) {
         throw newException (vm, stack, strerror (errno)) ;
    }
    return 0 ;
}

AIKIDO_NATIVE (readlink) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "readlink", "Illegal filename type") ;
    }
    char buf[1024] ;
    std::string filename = lockFilename (b, paras[1]) ;
    int n = readlink (filename.c_str(), buf, sizeof(buf)) ;
    if (n < 0) {
         throw newException (vm, stack, strerror (errno)) ;
    }
    buf[n] = 0 ;
    return new string (buf) ;
}

AIKIDO_NATIVE (symlink) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "symlink", "Illegal filename type") ;
    }
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "symlink", "Illegal filename type") ;
    }
    std::string oldpath = lockFilename (b, paras[1]) ;
    std::string newpath = lockFilename (b, paras[2]) ;
    int e = symlink (oldpath.c_str(), newpath.c_str()) ;
    if (e < 0) {
         throw newException (vm, stack, strerror (errno)) ;
    }
}

AIKIDO_NATIVE (mkdir1) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "mkdir", "Illegal filename type") ;
    }
    if (!isIntegral (paras[2].type)) {
        throw newParameterException (vm, stack, "mkdir", "Illegal mode type") ;
    }
    std::string filename = lockFilename (b, paras[1]) ;

    int err = mkdir (filename.c_str(), getInt (paras[2])) ;
    if (err == -1) {
         throw newException (vm, stack, strerror (errno)) ;
    }
    return 0 ;
}

AIKIDO_NATIVE (mknod) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "mknod", "Illegal filename type") ;
    }
    if (!isIntegral (paras[2].type)) {
        throw newParameterException (vm, stack, "mknod", "Illegal mode type") ;
    }
    std::string filename = lockFilename (b, paras[1]) ;

    int err = mknod (filename.c_str(), getInt (paras[2]), 0) ;
    if (err == -1) {
         throw newException (vm, stack, strerror (errno)) ;
    }
    return 0 ;
}

AIKIDO_NATIVE (tmpnam) {
    char buffer[1024] ;
    strcpy (buffer, "/tmp/aikidotmpXXXXXX") ;
    int fd = mkstemp (buffer) ;
    ::close (fd) ;
    return buffer ;
}

AIKIDO_NATIVE (isatty) {
    if (paras[1].type != T_STREAM) {
        return 0 ;
    }
    int fd = paras[1].stream->getInputFD() ;
    return isatty (fd) ;
}

AIKIDO_NATIVE (ttyname) {
    if (paras[1].type != T_STREAM) {
        return "" ;
    }
    int fd = paras[1].stream->getInputFD() ;
#if defined (_OS_SOLARIS)
    char name[256] ;
    ttyname_r (fd, name, 256) ;
#elif defined (_OS_MACOSX)
    oslock.lock() ;
    char *name = ttyname (fd) ;
    oslock.unlock() ;
#else
    char *name = ttyname (fd) ;
#endif
    return name ;
}

AIKIDO_NATIVE (raw_open) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "raw_open", "Illegal filename type") ;
    }
    if (!isIntegral (paras[2].type)) {
        throw newParameterException (vm, stack, "raw_open", "Illegal flags type") ;
    }
    if (!isIntegral (paras[3].type)) {
        throw newParameterException (vm, stack, "raw_open", "Illegal mode type") ;
    }
    std::string filename = lockFilename (b, paras[1]) ;
    int flags = getInt (paras[2]) ;
    int mode = getInt (paras[3]) ;

    int fd = open (filename.c_str(), flags, mode) ;
    if (fd < 0) {
        throw newException (vm, stack, "Failed to open file") ;
    }
    return fd ;
}

AIKIDO_NATIVE (raw_close) {
    if (!isIntegral (paras[1].type)) {
        throw newParameterException (vm, stack, "raw_close", "Illegal fd type") ;
    }
    ::close (getInt (paras[1])) ;
    return 0 ;
}

AIKIDO_NATIVE (raw_read) {
    if (!isIntegral (paras[1].type)) {
        throw newParameterException (vm, stack, "raw_read", "Illegal fd type") ;
    }
    if (!isIntegral (paras[2].type)) {
        throw newParameterException (vm, stack, "raw_read", "Illegal size type") ;
    }
    int fd = getInt (paras[1]) ;
    int size =  getInt (paras[2]) ;
    Value::bytevector *vec = new Value::bytevector() ;
    char *buf = new char[size] ;
    int n = ::read (fd, buf, size) ;
    if (n == 0) {
        delete [] buf ;
        return vec ;
    }
    if (n < 0) {
        delete [] buf ;
        throw newException (vm, stack, "Read failed") ;
    }
    for (int i = 0 ; i < n ; i++) {
        vec->push_back (buf[i]) ;
    }
    delete [] buf ;
    return vec ;
}


AIKIDO_NATIVE (raw_write) {
    if (!isIntegral (paras[1].type)) {
        throw newParameterException (vm, stack, "raw_write", "Illegal fd type") ;
    }
    if (paras[2].type != T_STRING && paras[2].type != T_BYTEVECTOR) {
        throw newParameterException (vm, stack, "raw_write", "Illegal buf type") ;
    }

    if (!isIntegral (paras[3].type)) {
        throw newParameterException (vm, stack, "raw_write", "Illegal size type") ;
    }
    int fd = getInt (paras[1]) ;
    int size =  getInt (paras[3]) ;
    const char *buf ;
    bool deletebuf = false ;
    if (paras[2].type == T_STRING) {
        buf = paras[2].str->c_str() ;
    } else {
        deletebuf = true ;
        char *b = new char [paras[2].bytevec->vec.size()] ;
        for (int i = 0 ; i < paras[2].bytevec->vec.size() ; i++) {
            *b++ = paras[2].bytevec->vec[i] ;
        }
        buf = const_cast<const char *>(b) ;
    }

    int n = ::write (fd, buf, size) ;
    if (n >= 0) {
        if (deletebuf) {
            delete [] buf ;
        }
        return 0 ;
    }
    if (deletebuf) {
        delete [] buf ;
    }
    throw newException (vm, stack, "Write failed") ;
}

AIKIDO_NATIVE (raw_select) {
    if (paras[1].type != T_VECTOR) {
        throw newParameterException (vm, stack, "raw_select", "Illegal fdset type") ;
    }
    if (paras[2].type != T_INTEGER) {
        throw newParameterException (vm, stack, "raw_select", "Illegal timeout type") ;
    }
    Value::vector *fdvec = paras[1].vec ;
    int timeout = getInt (paras[2]) ;
    fd_set fdset ;
    FD_ZERO (&fdset) ;

    int max = 0 ;
    for (int i = 0 ; i < fdvec->size() ; i++) {
        if ((*fdvec)[i].type != T_INTEGER) {
            throw newException (vm, stack, "Only integer values allowed in raw_select fdset vector") ;
        }
        if ((*fdvec)[i].integer > max) {
            max = (*fdvec)[i].integer ;
        }
        FD_SET ((*fdvec)[i].integer, &fdset) ;
    }

    struct timeval to ;
    to.tv_sec = timeout / 1000000 ;
    to.tv_usec = timeout % 1000000 ;

    // -1 timeout means none
    struct timeval *top = timeout == -1 ? NULL : &to ;

    int e = select (max+1, &fdset, NULL, NULL, top) ;
    if (e < 0) {
        if (errno == EINTR) {
            return new Value::vector() ;        // empty for interrupt
        }
        throw newException (vm, stack, "Select failed") ;
    }
    Value::vector *result = new Value::vector() ;
    for (int i = 0 ; i < fdvec->size() ; i++) {
        if (FD_ISSET ((*fdvec)[i].integer, &fdset)) {
            result->push_back (Value ((*fdvec)[i].integer)) ;
        }
    }
    return result ;
}

AIKIDO_NATIVE (raw_seek) {
    if (!isIntegral (paras[1].type)) {
        throw newParameterException (vm, stack, "raw_seek", "Illegal fd type") ;
    }
    if (!isIntegral (paras[2].type)) {
        throw newParameterException (vm, stack, "raw_seek", "Illegal offset type") ;
    }
    if (!isIntegral (paras[3].type)) {
        throw newParameterException (vm, stack, "raw_seek", "Illegal whence type") ;
    }
    int e = lseek (getInt (paras[1]), getInt(paras[2]), getInt(paras[3])) ;
    if (e == -1) {
        throw newException (vm, stack, "seek failed") ;
    }
    return 0 ;
}

//
// terminal functions
//
AIKIDO_NATIVE ( terminal_create) {
    return (reinterpret_cast<INTEGER>(new Terminal())) & 0xffffffffLL ;
}

AIKIDO_NATIVE ( terminal_open) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->open() ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_close) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->close() ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_put_string) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->put(paras[1].str->str) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_put_char) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->put((char)paras[1].integer) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_get ) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    char ch ;
    bool ok = term->get (ch, (int)paras[2].integer) ;
    *paras[1].addr = (char)ch ;
    return ok ;
}

AIKIDO_NATIVE ( terminal_erase_line) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->erase_line() ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_insert_char) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->insert_char((char)paras[1].integer) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_delete_left) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->delete_left() ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_amove ) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->amove(paras[1].integer, paras[2].integer) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_move ) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->move(paras[1].integer, paras[2].integer) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_clear_screen) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->clear_screen() ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_get_screen_size ) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    int rows, cols ;
    term->get_screen_size (rows, cols) ;
    *paras[1].addr = rows ;
    *paras[2].addr = cols ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_set_screen_size ) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    int rows = paras[2].integer ;
    int cols = paras[3].integer ;
    term->set_screen_size (rows, cols) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_scroll) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->scroll(paras[1].integer, paras[2].integer, paras[3].integer, paras[4].integer) ;
    return 0 ;
}



}		// end of extern "C"

// these functions are in the main package

// sleep for microseconds

AIKIDO_NATIVE(threadSleep) {
    if (!isIntegral (paras[0])) {
        throw newParameterException (vm, stack, "sleep", "Illegal sleep value") ;
    }
#if defined(_OS_MACOSX) 
    usleep (getInt (paras[0])) ;
#else
    struct timespec ts;

    int us = getInt (paras[0]) ;
    ts.tv_sec = us / 1000000 ;
    ts.tv_nsec = (us % 1000000) * 1000 ;
    nanosleep (&ts, 0) ;
#endif
    return 0 ;
}


}

