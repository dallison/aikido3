/*
 * native.cc
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
 * Closure support is Copyright (C) 2004 David Allison.  All rights reserved.
 * 
 * Contributor(s): dallison
 *
 * Version:  1.49
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */

// CHANGE LOG
// 10/21/2004           David Allison           Added closure support




//
// implementation of builtin native functions
//

#if defined(_OS_MACOSX) 
#include <sys/stat.h>
#endif

#include <sys/types.h>
#include "aikido.h"
#include <ctype.h>
#include "string.h"
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>
#include <algorithm>
#include "aikidointerpret.h"
#include "aikidoxml.h"

#if defined(_CC_GCC)
extern "C" struct tm *localtime_r(const time_t *, struct tm *) ;
extern "C" struct tm *gmtime_r(const time_t *clock, struct tm *res);
#if defined(_OS_SOLARIS)
extern "C" int altzone ;
#endif
#endif

namespace aikido {

extern void initSystemVars (StaticLink *staticlink) ;	// in osnative.cc

// if a native function is in the main package then it is just passed the parameters
// made in the call.
//
// if the function is part of a block (package, class etc) then the first parameter
// is a pointer to an object


// create a new exception object

// some system specific variables for use by native functions

StaticLink systemSlink ;
StaticLink *systemStack = &systemSlink ;

extern Package *system ;			// in interpret.cc
Class *exceptionClass = NULL ;
Class *fileExceptionClass = NULL ;
Class *parameterExceptionClass = NULL ;
Class *dateClass = NULL ;
Class *frameClass = NULL ;
Class *statClass = NULL ;
Class *userClass = NULL ;

Object *parserDelegate = NULL;

// parser locking monitor
// a monitor is required here because it needs to be reentrant from the same
// thread.  Say we call eval() which creates calls eval() through some
// convoluted route.

MonitorBlock locker (NULL, "$locker", 0, NULL) ;		// dummy package for locker
Monitor parserLock (&locker, NULL, NULL, NULL) ;

extern void signalHit (int) ;		// in parser.cc

// the getcwd function doesn't work they way we want it to for locked down files.  We want symbolic
// links in the path to appear in the name of the directory.
// This variable holds the current directory

std::string current_directory ;


//
// exception building
//

Exception newException(VirtualMachine *vm, StackFrame *stack, string reason) {
    // static link for exception object is the System block.

    Object *exceptionobj = new (exceptionClass->stacksize) Object (exceptionClass, systemStack, systemStack->frame, stack->inst) ;
    exceptionobj->ref++ ;
    exceptionobj->varstack[0] = Value (exceptionobj) ;
    exceptionobj->varstack[1] = Value (reason) ;

    vm->execute (exceptionClass, exceptionobj, systemStack, 0) ;
    exceptionobj->ref-- ;
    return Exception (exceptionobj, stack, systemStack, exceptionClass, exceptionClass->level) ;
}

Exception newFileException(VirtualMachine *vm, StackFrame *stack, string filename, string reason) {
    // static link for exception object is the System block.

    Object *exceptionobj = new (fileExceptionClass->stacksize) Object (fileExceptionClass, systemStack, systemStack->frame, stack->inst) ;
    exceptionobj->ref++ ;

    int parentsize = exceptionClass->stacksize ;

    exceptionobj->varstack[0] = Value (exceptionobj) ;
    exceptionobj->varstack[parentsize] = Value (filename) ;
    exceptionobj->varstack[parentsize + 1] = Value (reason) ;

    vm->execute (fileExceptionClass, exceptionobj, systemStack, 0) ;

    exceptionobj->ref-- ;
    return Exception (exceptionobj, stack, systemStack, exceptionClass, exceptionClass->level) ;
}

Exception newParameterException(VirtualMachine *vm, StackFrame *stack, string func, string reason) {
    // static link for exception object is the System block.

    Object *exceptionobj = new (parameterExceptionClass->stacksize) Object (parameterExceptionClass, systemStack, systemStack->frame, stack->inst) ;
    exceptionobj->ref++ ;

    int parentsize = exceptionClass->stacksize ;

    exceptionobj->varstack[0] = Value (exceptionobj) ;
    exceptionobj->varstack[parentsize] = Value (func) ;
    exceptionobj->varstack[parentsize + 1] = Value (reason) ;

    vm->execute (parameterExceptionClass, exceptionobj, systemStack, 0) ;

    exceptionobj->ref-- ;
    return Exception (exceptionobj, stack, systemStack, exceptionClass, exceptionClass->level) ;
}

void setSysVar (string name, const Value &v, StaticLink *slink) {
    Scope *scope ;
    Variable *var = system->findVariable (name, scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (var == NULL) {
        throw Exception ("Can't find required system variable") ;
    }
    var->setValue (v, slink->frame) ;
}
   

bool isIntegral (const Value &v) {
    return v.type == T_INTEGER || v.type == T_BYTE || v.type == T_CHAR || v.type == T_ENUMCONST ;
}

static INTEGER getInt (const Value &v) {
    if (v.type == T_ENUMCONST) {
        return v.ec->value ;
    } else {
        return v.integer ;
    }
}

// lock down the file name by prepending the root dir and making sure the
// user isn't playing tricks with relative directory names
std::string lockFilename (Aikido *b, const Value &v) {
    std::string fn = v.str->str ;
    if (b->properties & LOCKDOWN) {
        bool fromcurrent = false ;
        if (fn[0] == '/') {
            fn = b->lockdownRootdir + fn ;
        } else {
            std::string dir = current_directory ;
            fn = dir + '/' + fn ;
            fromcurrent = true ;
        }
        std::vector<std::string> vec ;
        // split the filename into a its parts
        std::string::size_type i, oldi = 0 ;
        do {
            i = fn.find ('/', oldi) ;
            if (i != std::string::npos) {
                std::string s (fn.substr (oldi, i - oldi)) ;
                vec.push_back (s) ;
                oldi = fn.find_first_not_of ('/', i) ;
                if (oldi == std::string::npos) {
                    break ;
                }
            } else {
                std::string s (fn.substr (oldi)) ;
                vec.push_back (s) ;
            }
        } while (i != std::string::npos) ;

        // now canonicalize the path name by removing all /.. and /.
        // a/b/../c => a/c
        //     i  j
        //   ^
        //   j-2

        for (i = 0 ; i < vec.size() ; i++) {
            //printf ("%s\n", vec[i].c_str()) ;
            if (vec[i] == "..") {
                //printf ("removing .. at %d\n",i) ;
                for (unsigned int j = i+1 ; j < vec.size() ; j++) {
                     vec[j-2] = vec[j] ;
                }
                vec.resize (vec.size() - 2) ;
                i -= 2 ;
            } else if (vec[i] == ".") {
                for (unsigned int j = i+1 ; j < vec.size() ; j++) {
                     vec[j-1] = vec[j] ;
                }
                vec.resize (vec.size() - 1) ;
                i -= 1 ;
            }
        }

        // now put it all back into a string, removing all empty components
        std::string r ;
        for (i = 0 ; i < vec.size() ; i++) {
            if (vec[i] != "") {
                r += "/" + vec[i] ;
            }
        }
        
        //printf ("r = %s\n", r.c_str()) ;
        std::string r1 = r + "/" ;
        // make sure that the result file begins with the root dir name
        if (r1.find(b->lockdownRootdir) == 0) {
            return r;
        }

        throw Exception ("Invalid filename") ;
    } else {
        return fn ;
    }
}


//
// date manipulation
//
static const char *datetable[] = {"sunday", "monday", "tuesday", "wednesday",
                          "thursday", "friday", "saturday",
                          "january", "february", "march", "april",
                          "may", "june", "july", "august", "september",
                          "october", "november", "december",
                          "am", "pm", "gmt", "utc", NULL} ;

static int datelookup (const char *s) {
    int len = strlen (s) ;
    for (int i = 0 ; datetable[i] != NULL ; i++) {
        if (strncmp (datetable[i], s, len) == 0) {
            return i ;
        }
    }
    return -1 ;
}

enum DateToken {
    DATE_END,
    DATE_BAD,
    DATE_NUMBER,
    DATE_PLUS,
    DATE_MINUS,
    DATE_COLON,
    DATE_SLASH,
    DATE_SUNDAY, DATE_MONDAY,
    DATE_TUESDAY, DATE_WEDNESDAY,
    DATE_THURSDAY,
    DATE_FRIDAY, DATE_SATURDAY,
    DATE_JANUARY, DATE_FEBRUARY, DATE_MARCH,
    DATE_APRIL, DATE_MAY, DATE_JUNE, DATE_JULY,
    DATE_AUGUST, DATE_SEPTEMBER, DATE_OCTOBER,
    DATE_NOVEMBER, DATE_DECEMBER,
    DATE_AM, DATE_PM, DATE_UTC, DATE_GMT
} ;

// retrieve the next token from the string representing a date
static DateToken nextDateToken (const char *&str, int &number) {
    while (*str != 0 && (isspace (*str) || *str == ',')) str++ ;   // skip spaces
    if (*str == 0) {
        return DATE_END ;
    }
    char ch = *str++ ;
    if (ch == 0) {
        return DATE_END ;
    }
    // number?
    if (isdigit (ch)) {
        int n = ch - '0' ;
        while (*str != 0 && isdigit (*str)) {
            ch = *str++ ;
            n = n * 10 + ch - '0' ;
        }
        number = n ;
        return DATE_NUMBER ;
    }
    switch (ch) {
    case '/':
        return DATE_SLASH ;
    case ':':
        return DATE_COLON ;
    case '+':
        return DATE_PLUS ;
    case '-':
        return DATE_MINUS ;
    }
    // alphanumeric
    if (isalpha (ch)) {
        std::string spelling = "" ;
        spelling += tolower (ch) ;
        while (*str != 0 && isalpha (*str)) {
            spelling += tolower (*str++) ;
        }
        int index = datelookup (spelling.c_str()) ;
        if (index == -1) {
            throw Exception ("Invalid date") ;
        } 
        return (DateToken)((int)DATE_SUNDAY + index) ;
    }
    return DATE_BAD ;
}


//
// parse a date from a string
//

void parsedate (Object *dateobj, const char *str) {
    DateToken currtok, prevtok ;
    prevtok = DATE_END ;

    int sec = -1 ;
    int min= -1 ;
    int hour = -1 ;
    int day = -1 ;
    int mon = -1 ;
    int year = -1 ;
    int tz = -1 ;

    // get current date and time
    time_t clock = time(NULL) ;
    struct tm timebuf ;
#ifdef _OS_WINDOWS
    struct tm *tm = localtime (&clock) ;
#else
    struct tm *tm = localtime_r (&clock, &timebuf) ;
#endif

    dateobj->varstack[1] = Value ("") ;
    dateobj->varstack[2] = Value (tm->tm_sec) ;
    dateobj->varstack[3] = Value (tm->tm_min) ;
    dateobj->varstack[4] = Value (tm->tm_hour) ;
    dateobj->varstack[5] = Value (tm->tm_mday) ;
    dateobj->varstack[6] = Value (tm->tm_mon) ;
    dateobj->varstack[7] = Value (tm->tm_year) ;
    dateobj->varstack[8] = Value (tm->tm_wday) ;
    dateobj->varstack[9] = Value (tm->tm_yday) ;
    dateobj->varstack[10] = Value (tm->tm_isdst) ;
#if defined(_OS_MACOSX)
    dateobj->varstack[11] = tm->tm_gmtoff ;
    dateobj->varstack[12] = tm->tm_zone ;
#elif defined (_OS_LINUX) || defined (_OS_WINDOWS)
    dateobj->varstack[11] = timezone ;
    dateobj->varstack[12] = tzname[tm->tm_isdst > 0 ? 1 : 0] ;
#else
    dateobj->varstack[11] = tm->tm_isdst ? altzone : timezone ;
    dateobj->varstack[12] = tzname[tm->tm_isdst > 0 ? 1 : 0] ;
#endif


    // parse the string and fill in the local vars
    // this is a state machine with each state transition triggered
    // by the type of token.  The current state is held in
    // the set of local variables.


    bool tzset = false ;
    do {
        int n ;
        currtok = nextDateToken (str, n) ;
        if (currtok == DATE_NUMBER) {
            if (year >= 0 && (prevtok == DATE_PLUS || (prevtok == DATE_MINUS && tzset))) {
               if (n < 24) {
                   n *= 60 ;
               } else {
                   n = n % 100 + n / 100 * 60 ;
               }
               if (prevtok == DATE_PLUS) {
                   n = -n ;
               }
               tz = n ;
            } else if (year >= 0 && !tzset && prevtok == DATE_MINUS) { // allow year-mon-day format
                if (mon < 0) {
                    mon = n - 1;
                } else if (day < 0) {
                    day = n ;
                } else {
                    throw Exception ("Invalid date") ;
                }
            } else if (n >= 70) {
                if (year >= 0) {
                    throw Exception ("Invalid date") ;
                }
                year = n < 1900 ? n : n - 1900 ;
            } else if (*str == ':') {
                if (hour < 0) {
                    hour = n ;
                } else if (min < 0) {
                    min = n ;
                } else {
                    throw Exception ("Invalid date") ;
                }
            } else if (*str == '/') {
                if (mon < 0) {
                    mon = n - 1 ;
                } else if (day < 0) {
                    day = n ;
                } else {
                    throw Exception ("Invalid date") ;
                }
            } else if (hour >= 0 && min < 0) {
                min = n ;
            } else if (min >= 0 && sec < 0) {
                sec = n ;
            } else if (day < 0) {
                day = n ;
            } else {
                throw Exception ("Invalid date") ;
            }
            prevtok = DATE_NUMBER ;
        } else {
            switch (currtok) {
            case DATE_SLASH:
            case DATE_COLON:
            case DATE_PLUS:
            case DATE_MINUS:
                prevtok = currtok ;
                break ;
            case DATE_SUNDAY:
            case DATE_MONDAY:
            case DATE_TUESDAY:
            case DATE_WEDNESDAY:
            case DATE_THURSDAY:
            case DATE_FRIDAY:
            case DATE_SATURDAY:
                break ;
            case DATE_JANUARY:
            case DATE_FEBRUARY :
            case DATE_MARCH:
            case DATE_APRIL:
            case DATE_MAY:
            case DATE_JUNE:
            case DATE_JULY:
            case DATE_AUGUST:
            case DATE_SEPTEMBER :
            case DATE_OCTOBER:
            case DATE_NOVEMBER:
            case DATE_DECEMBER:
                if (mon < 0) {		// setting month?
                    mon = (int)currtok - DATE_JANUARY ;
                } else {
                    throw Exception ("Invalid date") ;
                }
                break ;
            case DATE_AM:
                if (hour > 12 || hour < 1) {
                    throw Exception ("Invalid date") ;
                }
                if (hour == 12) {
                    hour = 0 ;
                }
                break ;
            case DATE_PM:
                if (hour > 12 || hour < 1) {
                    throw Exception ("Invalid date") ;
                }
                if (hour < 12) {
                    hour += 12 ;
                }
                break ;
            case DATE_UTC:
            case DATE_GMT:
                if (tzset) {
                    throw Exception ("Invalid date") ;
                }
                tzset = true ;
                break ;
            case DATE_END:
               break ;
            default:
                throw Exception ("Invalid date") ;
            }
        }
    } while (currtok != DATE_END) ;

    // replace or default the local vars
    if (year < 0) {
        year = (int)getInt (dateobj->varstack[7]) ;
    }
    if (mon < 0) {
        mon = (int)getInt (dateobj->varstack[6]) ;
    }
    if (day < 0) {
        day = (int)getInt (dateobj->varstack[5]) ;
    }
    if (sec < 0) {
        sec = (int)getInt (dateobj->varstack[2]) ;
    }
    if (min < 0) {
        min = (int)getInt (dateobj->varstack[3]) ;
    }
    if (hour < 0) {
        hour = (int)getInt (dateobj->varstack[4]) ;
    }

    struct tm time ;
    time.tm_sec = sec ;
    time.tm_min = min ;
    time.tm_hour = hour ;
    time.tm_mday = day ;
    time.tm_mon = mon ;
    time.tm_year = year ;
    time.tm_isdst = -1 ;
    time_t t = mktime (&time) ;         // normalize the time

    dateobj->varstack[2] = Value (time.tm_sec) ;
    dateobj->varstack[3] = Value (time.tm_min) ;
    dateobj->varstack[4] = Value (time.tm_hour) ;
    dateobj->varstack[5] = Value (time.tm_mday) ;
    dateobj->varstack[6] = Value (time.tm_mon) ;
    dateobj->varstack[7] = Value (time.tm_year) ;
    dateobj->varstack[8] = Value (time.tm_wday) ;
    dateobj->varstack[9] = Value (time.tm_yday) ;
    dateobj->varstack[10] = Value (tm->tm_isdst) ;

    // how do I do these?
    dateobj->varstack[11] = tz ;
#if defined (_OS_MACOSX)
    dateobj->varstack[12] = tm->tm_zone ;
#else
    dateobj->varstack[12] = tzname[tm->tm_isdst > 0 ? 1 : 0] ;
#endif
}

extern "C" {			// these are located using dlsym, so we need their unmangled name

AIKIDO_NATIVE(initSystem) {
    Scope *scope ;
    *systemStack = *staticLink ;
    Tag *sys = b->findTag (string (".System")) ;
    if (sys == NULL) {
        throw Exception ("Can't find system package") ;
    }
    system = (Package *)sys->block ;

    Tag *exceptiontag = (Tag *)system->findVariable (string (".Exception"), scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (exceptiontag == NULL) {
        throw Exception ("Can't find System.Exception") ;
    }
    exceptionClass = (Class *)exceptiontag->block ;

    Tag *fileExceptiontag = (Tag *)system->findVariable (string (".FileException"), scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (fileExceptiontag == NULL) {
        throw Exception ("Can't find System.FileException") ;
    }
    fileExceptionClass = (Class *)fileExceptiontag->block ;

    Tag *parameterExceptiontag = (Tag *)system->findVariable (string (".ParameterException"), scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (parameterExceptiontag == NULL) {
        throw Exception ("Can't find System.ParameterException") ;
    }
    parameterExceptionClass = (Class *)parameterExceptiontag->block ;

    Tag *datetag = (Tag *)system->findVariable (string (".Date"), scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (datetag == NULL) {
        throw Exception ("Can't find System.Date") ;
    }
    dateClass = (Class *)datetag->block ;

    Tag *frametag = (Tag *)system->findVariable (string (".StackFrame"), scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (frametag == NULL) {
        throw Exception ("Can't find System.StackFrame") ;
    }
    frameClass = (Class *)frametag->block ;

    Tag *stattag = (Tag *)system->findVariable (string (".Stat"), scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (stattag == NULL) {
        throw Exception ("Can't find System.Stat") ;
    }
    statClass = (Class *)stattag->block ;

    Tag *usertag = (Tag *)system->findVariable (string (".User"), scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (usertag == NULL) {
        throw Exception ("Can't find System.User") ;
    }
    userClass = (Class *)usertag->block ;

    setSysVar ("programname", programname.c_str(), staticLink) ;
    setSysVar ("programdir", b->directory, staticLink) ;

    initSystemVars (staticLink) ;		// in osnative.cc

    // if the username is locked down, set it now
    if (b->properties & LOCKDOWN && b->lockdownUser.size() != 0) {
        setSysVar ("username", b->lockdownUser.c_str(), staticLink) ;
    }
    return 0 ;
}

//
// native functions.  These are in the System package, so the first para is the system object
//

//
// open a file for input
//
AIKIDO_NATIVE(openin) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "openin", "Bad filename type") ;
        return 0 ;
    }
    std::string filename = lockFilename (b, paras[1]) ;
#if defined(_CC_GCC) && __GNUC__ == 2
    std::iostream *s = (std::iostream*)new std::fstream (filename.c_str(), std::ios::in|std::ios::binary) ;
#else
    std::iostream *s = (std::iostream*)new std::fstream (filename.c_str(), std::ios_base::in|std::ios_base::binary) ;
#endif
    if (!(*s)) {
        throw newFileException (vm, stack, *paras[1].str, "cannot open file") ;
    }
    return new StdStream (s) ;
}

//
// open a file for output
//

AIKIDO_NATIVE(openout) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "openout", "Bad filename type") ;
        return 0 ;
    }
    std::string filename = lockFilename (b, paras[1]) ;
#if defined(_CC_GCC) && __GNUC__ == 2
    std::iostream *s = (std::iostream*)new std::fstream (filename.c_str(), std::ios::out|std::ios::binary) ;
#else
    std::iostream *s = (std::iostream*)new std::fstream (filename.c_str(), std::ios_base::out|std::ios_base::binary) ;
#endif
    if (!(*s)) {
        throw newFileException (vm, stack, *paras[1].str, "cannot open file") ;
    }
    return new StdStream (s) ;
}

//
// open a file for update
//
AIKIDO_NATIVE(openup) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "openup", "Bad filename type") ;
        return 0 ;
    }
    std::string filename = lockFilename (b, paras[1]) ;
#if defined(_CC_GCC) && __GNUC__ == 2
    std::iostream *s = (std::iostream*)new std::fstream (filename.c_str(), std::ios::in | std::ios::out|std::ios::binary) ;
#else
    std::iostream *s = (std::iostream*)new std::fstream (filename.c_str(), std::ios_base::out|std::ios_base::app|std::ios_base::binary) ;
#endif
    if (!(*s)) {
        throw newFileException (vm, stack, *paras[1].str, "cannot open file") ;
    }
    return new StdStream (s) ;
}

//
// open a file given a mode
//

AIKIDO_NATIVE(open) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "open", "Bad filename type") ;
        return 0 ;
    }
    if (!isIntegral (paras[2])) {
        throw newParameterException (vm, stack, "open", "Bad open mode") ;
        return 0 ;
    }
    std::string filename = lockFilename (b, paras[1]) ;
    
#if defined(_CC_GCC) && __GNUC__ == 2
    std::ios::openmode mode = 0 ;
    int inmode = getInt (paras[2]) ;
    if (inmode & 1) {
        mode |= std::ios::app ;
    }
    if (inmode & 2) {
        mode |= std::ios::binary ;
    }
    if (inmode & 4) {
        mode |= std::ios::in ;
    }
    if (inmode & 8) {
        mode |= std::ios::out ;
    }
    if (inmode & 16) {
        mode |= std::ios::trunc ;
    }
    if (inmode & 32) {
        mode |= std::ios::ate ;
    }
    if (inmode & 64) {
        mode |= std::ios::nocreate ;
    }
    if (inmode & 128) {
        mode |= std::ios::noreplace ;
    }
#else
    std::ios_base::openmode mode = (std::ios_base::openmode)0 ;
    int inmode = getInt (paras[2]) ;
    if (inmode & 1) {
        mode |= std::ios_base::app ;
    }
    if (inmode & 2) {
        mode |= std::ios_base::binary ;
    }
    if (inmode & 4) {
        mode |= std::ios_base::in ;
    }
    if (inmode & 8) {
        mode |= std::ios_base::out ;
    }
    if (inmode & 16) {
        mode |= std::ios_base::trunc ;
    }
    if (inmode & 32) {
        mode |= std::ios_base::ate ;
    }
#if !defined (_CC_GCC) && !defined(_OS_WINDOWS)
    if (inmode & 64) {
        mode |= std::ios_base::nocreate ;
    }
    if (inmode & 128) {
        mode |= std::ios_base::noreplace ;
    }
#endif // _CC_GCC
#endif // GCC 3.1
    std::iostream *s = (std::iostream*)new std::fstream (filename.c_str(), mode) ;
    if (!(*s)) {
        throw newFileException (vm, stack, *paras[1].str, "cannot open file") ;
    }
    return new StdStream (s) ;
}

//
// close a stream
//

AIKIDO_NATIVE(close) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "close", "Bad stream type") ;
        return 0 ;
    }
    paras[1].stream->close() ;
    return Value (0) ;
}


//
// has a stream reached EOF
//

AIKIDO_NATIVE(eof) {
    if (paras[1].type != T_STREAM && paras[1].type != T_NONE) {
        throw newParameterException (vm, stack, "eof", "Bad stream type") ;
        return 0 ;
    }
    if (paras[1].type == T_NONE) {
        // standard input stream
        return Value (vm->input.stream->eof()) ;
    }
    return Value (paras[1].stream->eof()) ;
}

//
// flush a stream to its device
//

AIKIDO_NATIVE(flush) {
    if (paras[1].type == T_NONE) {
        Value &out = vm->output ;
        if (out.type == T_STREAM) {
            vm->output.stream->flush() ;
        }
        return 0 ;
    }
    if (paras[1].type == T_STREAM) {
        paras[1].stream->flush() ;
    }
    return 0 ;
}

//
// how many buffered chars are there
//
AIKIDO_NATIVE(availableChars) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "availableChars", "Bad stream type") ;
        return 0 ;
    }
    return paras[1].stream->availableChars() ;
}

//
// get a single character from a stream
//

AIKIDO_NATIVE(getchar) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "getchar", "Bad stream type") ;
        return 0 ;
    }
    return paras[1].stream->get() ;
}

AIKIDO_NATIVE(unget) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "unget", "Bad stream type") ;
        return 0 ;
    }
    paras[1].stream->unget() ;
    return 0 ;
}

//
// get a set of characters from a stream
//
AIKIDO_NATIVE(get) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "get", "Bad stream type") ;
        return 0 ;
    }
    if (!isIntegral (paras[2])) {
        throw newParameterException (vm, stack, "get", "Bad type for n") ;
    }
    int n = getInt (paras[2]) ;

    Value::bytevector *v = new Value::bytevector (n) ;
    for (int i = 0 ; i < n ; i++) {
        (*v)[i] = paras[1].stream->get() ;
    }
    return v ;
}

//
// get all available chars from the stream
//
AIKIDO_NATIVE(getbuffer) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "getbuffer", "Bad stream type") ;
        return 0 ;
    }
    Value::bytevector *v = new Value::bytevector() ;
    Stream *stream = paras[1].stream ;
    if (stream->eof()) {
        return v ;
    }
    char c ;
    do {
        c = stream->get() ;
        if (!stream->eof()) {
            v->push_back (c) ;
        }
    } while (stream->availableChars() > 0 && !stream->eof()) ;
    return v ;
}

//
// signal an error
//
AIKIDO_NATIVE(error) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "error", "Bad error parameter type") ;
        return 0 ;
    }
    std::cerr << *paras[1].str << '\n' ;
    b->numErrors++ ;
    return 0 ;
}

//
// format a number into a string
//
static bool formatNumber (char *format, char *buf, const Value &v) {
    switch (v.type) {
    case T_INTEGER:
    case T_CHAR:
    case T_BYTE:
    case T_ENUMCONST:
        sprintf (buf, format, getInt (v)) ;
        return true ;
    case T_REAL:
        sprintf (buf, format, (INTEGER)v.real) ;
        return true ;
    
    case T_STRING:
    case T_VECTOR:
    case T_MAP:
    case T_OBJECT:
    default:
        return false ;
    }
}

static bool formatChar (char *format, char *buf, const Value &v) {
    switch (v.type) {
    case T_INTEGER:
    case T_CHAR:
    case T_BYTE:
        sprintf (buf, format, (char)getInt (v)) ;
        return true ;
    
    case T_REAL:
    case T_ENUMCONST:
    case T_STRING:
    case T_VECTOR:
    case T_MAP:
    case T_OBJECT:
    default:
        return false ;
    }
}

static bool formatReal (char *format, char *buf, const Value &v) {
    switch (v.type) {
    case T_INTEGER:
    case T_CHAR:
    case T_BYTE:
    case T_ENUMCONST:
        sprintf (buf, format, (double)getInt (v)) ;
        return true ;
    case T_REAL:
        sprintf (buf, format, v.real) ;
        return true ;
    
    case T_STRING:
    case T_VECTOR:
    case T_MAP:
    case T_OBJECT:
    default:
        return false ;
    }
}

static bool formatString (char *format, char *buf, const Value &v) {
    char nbuf[100] ;
    switch (v.type) {
    case T_INTEGER:
    case T_CHAR:
    case T_BYTE:
    case T_ENUMCONST:
        sprintf (nbuf, "%lld", getInt (v)) ;
        sprintf (buf, format, nbuf) ;
        return true ;
    case T_STRING:
        sprintf (buf, format, v.str->c_str()) ;
        return true ;
        
    case T_CLASS:
    case T_FUNCTION:
    case T_MONITOR:
    case T_PACKAGE:
    case T_ENUM:
    case T_THREAD:
        sprintf (buf, format, v.block->name.c_str()) ;
        return true ;
    case T_VECTOR:
    case T_MAP:
    case T_OBJECT:
    default:
        return false ;
    }
}




static bool formatBinary (char *format, char *buf, const Value &v) {
    if (!(v.type ==T_INTEGER || v.type == T_CHAR || v.type == T_BYTE)) {
        return false ;
    }
    int width = 0 ;
    bool zerofill = false ;
    bool leftjust = false ;
    char *s = format + 1 ;
    if (*s == '-') {
        leftjust = true ;
        s++ ;
    }
    if (*s == '0') {
        zerofill = true ;
        s++ ;
    }
    while (isdigit (*s)) {
        width = (width * 10) + *s++ - '0' ;
    }
    if (width > 1024) {
        width = 1024 ;
    }
    char nbuf[1024] ;			// binary is written reversed here
    s = nbuf ;
    int n = 0 ;
    int size = 63 ;
    while (size > 0 && (v.integer & ((INTEGER)1 << size)) == (INTEGER)0) {
        size-- ;
    }
    while (n <= size) {
        *s++ = ((v.integer & ((INTEGER)1 << n++)) != (INTEGER)0) + '0' ;
    }
    while (n < width) {
        *s++ = zerofill ? '0' : ' ' ;
        n++ ;
    }
    *s = 0 ;
    if (width == 0) {
        width = strlen (nbuf) ;
    }
    if (!leftjust) {
        s = nbuf + width - 1 ;
        while (s >= nbuf) {
            *buf++ = *s-- ;
        }
        *buf = 0 ;
    } else {
        s = nbuf + size ;
        while (s >= nbuf) {
            *buf++ = *s-- ;
        }
        n = width - size ;
        while (n > 0) {
            *buf++ = ' ' ;
            n-- ;
        }
        *buf = 0 ;
    }
    return true ;
}

static bool isFormatChar (char c) {
    switch (c) {
    case 'd':
    case 'x':
    case 'X':
    case 'c':
    case 's':
    case 'b':
    case 'o':
    case 'e':
    case 'f':
    case 'g':
        return true ;
    default:
        return false ;
    }
}

AIKIDO_NATIVE(vformat) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "vformat", "Bad format parameter type") ;
        return 0 ;
    }
    if (paras[2].type != T_VECTOR) {
        throw newParameterException (vm, stack, "vformat", "Bad args parameter type") ;
        return 0 ;
    }
    Value::vector &args = *paras[2].vec ;

    int para = 0 ;
    const char *fmt = paras[1].str->c_str() ;
    string str ;
    char buf[1024] ;
    char format[100] ;
    while (*fmt != 0) {
        if (*fmt == '%') {
            char *s = format ;
            *s++ = '%' ;
            fmt++ ;
            if (*fmt == '%') {
                str += '%' ;
                fmt++ ;
                continue ; 
            }
            while (!isFormatChar (*fmt)){
                *s++ = *fmt++ ;
            }
            char f = *fmt ;
            *s++ = *fmt++ ;
            *s = 0 ;
            switch (f) {
            case 'd':			// decimal
            case 'x':			// lower hex
            case 'X':			// upper hex
            case 'o':			// octal
                s-- ;
                *s++ = 'l' ;
                *s++ = 'l' ;
                *s++ = f ;
                *s = 0 ;
                if (!formatNumber (format, buf, args[para++])) {
                    throw newException (vm, stack,"Cannot format this type") ;
                }
                str += buf ;
                break ;
            case 'e':
            case 'f':
            case 'g':
                if (!formatReal (format, buf, args[para++])) {
                    throw newException (vm, stack,"Cannot format this type") ;
                }
                str += buf ;
                break ;
            case 's':			// string
                if (!formatString (format, buf, args[para++])) {
                    throw newException (vm, stack,"Cannot convert this type to string") ;
                }
                str += buf ;
                break ;
            case 'b':			// binary
                if (!formatBinary (format, buf, args[para++])) {
                    throw newException (vm, stack,"Cannot convert this type to binary") ;
                }
                str += buf ;
                break ;
            case 'c':			// char
                if (!formatChar (format, buf, args[para++])) {
                    throw newException (vm, stack,"Cannot convert this type to char") ;
                }
                str += buf ;
                break ;
            }
        } else {
            str += *fmt++ ;
        }
    }
    return Value (str) ;
}

AIKIDO_NATIVE (format) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "format", "Bad format parameter type") ;
        return 0 ;
    }

    int para = 2 ;
    const char *fmt = paras[1].str->c_str() ;
    string str ;
    char buf[1024] ;
    char format[100] ;
    while (*fmt != 0) {
        if (*fmt == '%') {
            char *s = format ;
            *s++ = '%' ;
            fmt++ ;
            if (*fmt == '%') {
                str += '%' ;
                fmt++ ;
                continue ; 
            }
            while (!isFormatChar (*fmt)){
                *s++ = *fmt++ ;
            }
            char f = *fmt ;
            *s++ = *fmt++ ;
            *s = 0 ;
            switch (f) {
            case 'd':			// decimal
            case 'x':			// lower hex
            case 'X':			// upper hex
            case 'o':			// octal
                s-- ;
                *s++ = 'l' ;
                *s++ = 'l' ;
                *s++ = f ;
                *s = 0 ;
                if (!formatNumber (format, buf, paras[para++])) {
                    throw newException (vm, stack,"Cannot format this type") ;
                }
                str += buf ;
                break ;
            case 'e':
            case 'f':
            case 'g':
                if (!formatReal (format, buf, paras[para++])) {
                    throw newException (vm, stack,"Cannot format this type") ;
                }
                str += buf ;
                break ;
            case 's':			// string
                if (!formatString (format, buf, paras[para++])) {
                    throw newException (vm, stack,"Cannot convert this type to string") ;
                }
                str += buf ;
                break ;
            case 'b':			// binary
                if (!formatBinary (format, buf, paras[para++])) {
                    throw newException (vm, stack,"Cannot convert this type to binary") ;
                }
                str += buf ;
                break ;
            }
        } else {
            str += *fmt++ ;
        }
    }
    return Value (str) ;

}

//
// not yet implemented
//
AIKIDO_NATIVE(scan) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "scan", "Bad format parameter type") ;
        return 0 ;
    }
    return 0 ;
}

//
// rewind a stream
//
AIKIDO_NATIVE(rewind) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "rewind", "Invalid stream") ;
    }
    paras[1].stream->rewind() ; 
    return 0 ;
}



// follow the current stack up to the top, building a vector of StackFrame objects

AIKIDO_NATIVE(getStackTrace) {
    Value::vector *vec = new Value::vector ;
    StackFrame *f = stack ;
    while (f != NULL && f->inst != NULL && f->block != NULL) {
        Object *frameobj = new (frameClass->stacksize) Object (frameClass, NULL, NULL, NULL) ;
        frameobj->ref++ ;
        frameobj->varstack[0] = Value (frameobj) ;
        frameobj->varstack[1] = Value (f->inst->source->filename) ;
        frameobj->varstack[2] = Value (f->inst->source->lineno) ;
        if (f->dlink != NULL) {
            frameobj->varstack[3] = Value (f->dlink->block->name) ;
        } else {
            frameobj->varstack[3] = "" ;
        }
        vec->push_back (Value (frameobj)) ;
        frameobj->ref-- ;
        f = f->dlink ;			// follow dynamic link to next frame
    }
    
    return Value (vec) ;
}

//
// evaluate the string as an expression
//
AIKIDO_NATIVE(eval) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "eval", "Bad parameter type") ;
        return 0 ;
    }

    parserLock.acquire(true) ;

#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream stream ;
#else
    std::stringstream stream ;
#endif
    stream << *paras[1].str ;
    string filename (stack->inst->source->filename) ;
    Context ctx (stream, filename, stack->inst->source->lineno) ;
    b->currentContext = &ctx ;
    b->readLine() ;
    stream.clear() ;
    b->nextToken() ;

    StackFrame *parentStack = stack->dlink ;		// parent stack frame is caller of eval
    //Block *parentBlock = parentStack->block ;		// want to be in this block
    b->currentStack = parentStack ;

    b->currentScope = currentScope ; // parentBlock ;
    b->currentScopeLevel = currentScopeLevel ; // parentBlock->level ;
    b->currentMajorScope = currentScope->majorScope ;
    b->currentClass = NULL ;
    try {
        Node *node = b->expression() ;
        Node *tree = node ;
        b->currentContext = ctx.getPrevious() ;

        if (node != NULL) {
            node = new Node (b, RETURN, node) ;
            codegen::CodeSequence *code = b->codegen->generate (node) ;
            vm->execute (code,  parentStack, parentStack->slink, 0) ;
            b->currentStack = NULL ;
            b->deleteTree (tree) ;
            parserLock.release(true) ;
            Value result = vm->retval ;
            vm->retval.destruct() ;
            delete code ;
            return result ;
        } else {
            b->currentStack = NULL ;
            b->deleteTree (tree) ;
            parserLock.release(true) ;
            return Value() ;
        }
    } catch (...) {
        parserLock.release(true) ;
        throw ;
    }
}

// load a aikido program from a stream at runtime.  This:
// 1. creates a new package with the given name
// 2. parses the input stream in the context of that package (as if it was inline)
// 3. creates an instance of the package and constructs it


Value loadStream (Aikido *b, VirtualMachine *vm, std::istream &is, string name, string filename, int lineno, StackFrame *stack, StaticLink *staticLink,
                    Scope *currentScope, int currentScopeLevel) {

    // set the parent block for the package.  The current stack is the function loadStream or
    // loadVector.  It is called from System.load.  The caller of that is the block we want
    // as our parent
    //
    // we want the static link of the new object to be the caller block.  The slink
    // field of the stackframe corresponding to the caller contains a pointer to
    // a StaticLink struct.  We need to create a new StaticLink object to point
    // to this one.  The new object is created inside the object itself

    parserLock.acquire(true) ;
    StackFrame *parentStack = stack->dlink->dlink ;
    StaticLink newslink (parentStack->slink, parentStack) ;

    Block *parentBlock = parentStack->block ;
    int level = parentBlock->level + 1 ;

    Block *block = new Class (b, name, level, parentBlock) ;
    block->stacksize = 1 ;		// room for this

    Variable *thisvar = new Variable ("this", 0) ;
    thisvar->setAccess (PROTECTED) ;
    block->variables["this"] = thisvar ;

    block->setParentBlock (parentBlock) ;

    // create a temp stack frame so that variables can be created in it.  The real stack
    // frame will be an object allocated to the appropriate size when the number
    // of variables is known
    StackFrame *currframe = new (1) StackFrame (&newslink, parentStack, NULL, block, 1, true) ;

    

    b->currentStack = currframe ;    /// parentStack ;       // we want variables to be created in object, 
    b->currentMajorScope = block ;
    b->currentScope = block ;
    b->currentScopeLevel = block->level ;
    b->currentClass = (Class*)block ;

    Context ctx (is, filename, lineno, b->currentContext) ;
    b->currentContext = &ctx ;

    Node *tree ;

    try {
        b->importedTree = NULL ;
        tree = b->parseStream (is) ;

        block->body = new Node (b, SEMICOLON, b->importedTree, tree) ;
        block->code = b->codegen->generate (static_cast<InterpretedBlock*>(block)) ;
    
        b->currentContext = ctx.getPrevious() ;
    
        b->currentStack = NULL ;
    } catch (...) {
        parserLock.release(true) ;
        throw ;
    }
    parserLock.release(true) ;
    delete currframe ;

    Object *obj = new (block->stacksize) Object (block, &newslink, stack, &block->code->code[0]) ;
    obj->ref++ ;
    obj->varstack[0] = Value (obj) ;

    vm->execute (static_cast<InterpretedBlock*>(block), obj, &newslink, 0) ;
    obj->ref-- ;
    return obj ;
}


AIKIDO_NATIVE (loadStream) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "loadStream", "Can only load from stream") ;
        return 0 ;
    }
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "loadStream", "Invalid block name") ;
        return 0 ;
    }
    if (paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "loadStream", "Invalid file name") ;
        return 0 ;
    }
    if (paras[4].type != T_INTEGER) {
        throw newParameterException (vm, stack, "loadStream", "Invalid line number") ;
        return 0 ;
    }
    return loadStream (b, vm, paras[1].stream->getInStream(), *paras[2].str, *paras[3].str, paras[4].integer, stack, staticLink, currentScope, currentScopeLevel) ;
}

//
// load a file from a vector.  The first para is the vector to load from, the second
// is the name to give to the package
//

AIKIDO_NATIVE (loadVector) {
    if (paras[1].type != T_VECTOR) {
        throw newParameterException (vm, stack, "loadVector", "Can only load from vector") ;
        return 0 ;
    }
    if (paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "loadVector", "Invalid block name") ;
        return 0 ;
    }
    if (paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "loadVector", "Invalid file name") ;
        return 0 ;
    }
    if (paras[4].type != T_INTEGER) {
        throw newParameterException (vm, stack, "loadVector", "Invalid line number") ;
        return 0 ;
    }
#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream stream ;
#else
    std::stringstream stream ;
#endif
    Value::vector &vec = *paras[1].vec ;
    for (int i = 0 ; i < vec.size() ; i++) {
        if (vec[i].type != T_STRING) {
            throw newParameterException (vm, stack, "loadVector", "Invalid vector member") ;
        }
        string &str = *vec[i].str ;
        if (str[str.size() - 1] != '\n') {		// add newline if not present
            stream << str << '\n' ;
        } else {
            stream << str ;
        }
    }

    return loadStream (b, vm, stream, *paras[2].str, *paras[3].str, paras[4].integer, stack, staticLink, currentScope, currentScopeLevel) ;
}

AIKIDO_NATIVE (parseString) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "parse", "Must parse a string") ;
        return 0 ;
    }
#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream stream ;
#else
    std::stringstream stream ;
#endif
    string &str = *paras[1].str ;
    if (str[str.size() - 1] != '\n') {		// add newline if not present
        stream << str << '\n' ;
    } else {
        stream << str ;
    }

    string empty("");
    return loadStream (b, vm, stream, empty, empty, 0, stack, staticLink, currentScope, currentScopeLevel) ;
}


//
// load a library (.so file on unix)
//
AIKIDO_NATIVE (loadLibrary) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "loadLibrary", "Filename must be a string") ;
        return 0 ;
    }
    OS::openLibrary (paras[1].str->c_str()) ;
    return 0 ;
}

//
// seek on a stream
//
AIKIDO_NATIVE(seek) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "seek", "Invalid stream") ;
    }
    if (!isIntegral (paras[2]) || !isIntegral (paras[3])) {
        throw newParameterException (vm, stack, "seek", "Invalid offset or whence") ;
    }
    return paras[1].stream->seek (getInt (paras[2]), getInt (paras[3])) ;
}

//
// exit the program
//
AIKIDO_NATIVE(exit) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "exit", "Invalid exit code") ;
        return 0 ;
    }
//#if define(_OS_LINUX) 
    //b->threadLock.lock() ;
    //ThreadMap::iterator i ;
    //for (i = b->threads.begin() ; i != b->threads.end() ; i++) {
       //pthread_kill (i->first, SIGCHLD) ; 
    //}
//#else
    ::exit (getInt (paras[1])) ;
//#endif
    return 0 ;
}

AIKIDO_NATIVE(abort) {
    ::abort() ;
    return 0 ;
}


//
// create a stream attached to an open file
//
AIKIDO_NATIVE (openfd) {
    std::iostream *stream = new fd_fstream (getInt (paras[1])) ;
    return new StdStream (stream) ;
}

//
// random number
//
AIKIDO_NATIVE (rand) {
    return (INTEGER)::rand()  & (INTEGER)0xffffffff;
}

//
// set random seed
//
AIKIDO_NATIVE (srand) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "srand", "Invalid seed") ;
        return 0 ;
    }
    ::srand (getInt (paras[1])) ;
    return 0 ;
}

//
// value cloning
//
typedef std::map<Object*, Object*> CloneInfo ;

static Object *lookupClone (CloneInfo &info, Object* obj) {
    CloneInfo::iterator i = info.find (obj) ;
    if (i == info.end()) {
        return NULL ;
    }
    return i->second ;
}

static void addCloneInfo (CloneInfo &info, Object *key, Object *clone) {
    info[key] = clone ;
}

static Value clone (const Value &v, bool recurse, CloneInfo &info) ;

static void cloneBlock (Value *base, Block *blk, Object *obj, bool recurse, CloneInfo &info) {
    if (blk->superblock != NULL) {
        cloneBlock (base, blk->superblock->block, obj, recurse, info) ;
    }

    Scope::VarMap::iterator i ;

    for (i = blk->variables.begin() ; i != blk->variables.end() ; i++) {
        Variable *var = (*i).second ;
        if (var->name[0] == '.') {          // tag?  Tags have no storage, the refer to vars
            continue ;
        }
        if (var->flags & VAR_STATIC) {          // don't clone static members
            continue ;
        }
        Value &val = base[var->offset] ;
        if (recurse) {
            var->setValue (clone (val, recurse, info), obj) ;
        } else {
            var->setValue (val, obj) ;
        }
    }
}

static Value clone (const Value &v, bool recurse, CloneInfo &info) {
    switch (v.type) {
    case T_INTEGER:
    case T_CHAR:
    case T_BYTE:
        return v.integer ;
    case T_REAL:
        return v.real ;
    case T_STRING: {
        string *s = new string (v.str->str) ;
        return s ;
        }
    case T_VECTOR: {
        Value::vector *vec = new Value::vector (v.vec->size()) ;
        for (int i = 0 ; i < v.vec->size() ; i++) {
            if (recurse) {
                (*vec)[i] = clone ((*v.vec)[i], recurse, info) ;
            } else {
                (*vec)[i] = (*v.vec)[i] ;
            }
        }
        return vec ;
        }
    case T_BYTEVECTOR: {
        Value::bytevector *vec = new Value::bytevector (v.bytevec->size()) ;
        for (int i = 0 ; i < v.bytevec->size() ; i++) {
            (*vec)[i] = (*v.bytevec)[i] ;
        }
        return vec ;
        }
    case T_MAP: {
        map::iterator s ;
        map *mp = new map ;
        for (s = v.m->begin() ; s != v.m->end() ; s++) {
            const Value &key = (*s).first ;
            Value &val = (*s).second ;
            if (recurse) {
                mp->insert (clone (key, recurse, info), clone (val, recurse, info)) ;
            } else {
                mp->insert (key, val) ;
            }
        }
        return mp ;
        }
    case T_STREAM:
        return v.stream ;

    case T_OBJECT: {
        if (v.object ==NULL) {
            return (Object*)NULL ;
        }
        Object *obj = lookupClone (info, v.object) ;
        if (obj != NULL) {
            return obj ;
        }
        Block *blk = v.object->block ;
        Value *base = v.object->varstack ;
        if (blk->type == T_MONITOR) {
            obj = (Object*)new (blk->stacksize)  Monitor (blk, &v.object->objslink, v.object->dlink, v.object->inst) ;
        } else {
            obj = new (blk->stacksize)  Object (blk, &v.object->objslink, v.object->dlink, v.object->inst) ;
        }
        obj->ref++ ;
        addCloneInfo (info, v.object, obj) ;		// add to clone info

        // clone the object and the superblocks of the object, placing all the
        // cloned values into the newly allocated object
        cloneBlock (base, blk, obj, recurse, info) ;

        obj->varstack[0] = obj ;		// set the this pointer
        obj->ref-- ;
        return obj ;
        }
        
    case T_FUNCTION:
        return v.func ;
    case T_CLASS:
    case T_MONITOR:
        return v.cls ;
    case T_PACKAGE:
        return v.package ;
    case T_THREAD:
        return v.thread ;
    case T_ENUM:
        return v.en ;
    case T_ENUMCONST:
        return v.ec ;
    default:
        return Value() ;
    }
}

//
// produce a copy of a value
//
AIKIDO_NATIVE (clone) {
    if (!isIntegral (paras[2])) {
        throw newParameterException (vm, stack, "clone", "Invalid recurse parameter") ;
    }
    CloneInfo info ;
    return clone (paras[1], getInt (paras[2]), info) ;
}


// copy the vector and sort it.  Return the sorted vector
AIKIDO_NATIVE (sort) {
    if (paras[1].type != T_VECTOR) {
        throw newParameterException (vm, stack, "sort", "can only be applied to vectors") ;
    }
    Value::vector *vec = paras[1].vec ;
    Value::vector *v = new Value::vector (vec->size()) ;
    v->ref++ ;
    for (int i = 0 ; i < vec->size() ; i++) {
        (*v)[i] = (*vec)[i] ;
    }
    std::sort (v->begin(), v->end()) ;
    v->ref-- ;
    return v ;
}

//
// binary search in a sorted vector
//
AIKIDO_NATIVE (bsearch) {
    if (paras[1].type != T_VECTOR) {
        throw newParameterException (vm, stack, "bsearch", "can only be applied to vectors") ;
    }
    Value::vector *vec = paras[1].vec ;
    Value val = paras[2] ;
    bool result = std::binary_search (vec->begin(), vec->end(), val) ;
    return result ? 1 : 0 ;
}


const int SATTR_BUFFERSIZE = 1 ;
const int SATTR_MODE = 2 ;
const int SATTR_AUTOFLUSH = 3 ;

//
// set stream attributes
//
AIKIDO_NATIVE (setStreamAttribute) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "setStreamAttribute", "Illegal stream") ;
    }
    if (!isIntegral (paras[2])) {
        throw newParameterException (vm, stack, "setStreamAttribute", "Illegal stream attribute") ;
    }

    switch (getInt (paras[2])) {
    case SATTR_BUFFERSIZE:
        if (!isIntegral (paras[3])) {
            throw newException (vm, stack, "Illegal BUFFERSIZE value type") ;
        }
        paras[1].stream->setBufferSize (getInt (paras[3])) ;
        break ;
    case SATTR_MODE: {
        if (!isIntegral (paras[3])) {
            throw newException (vm, stack, "Illegal MODE value type") ;
        }
        int mode = getInt (paras[3]) ;
        if (mode <0 || mode > 1) {
            throw newException (vm, stack, "Illegal MODE value") ;
        }
        paras[1].stream->setMode (mode) ;
        break ;
        }
    case SATTR_AUTOFLUSH:
        if (!isIntegral (paras[3])) {
            throw newException (vm, stack, "Illegal AUTOFLUSH value type") ;
        }
        paras[1].stream->setAutoFlush (getInt (paras[3])) ;
        break ;
    default:
        throw newException (vm, stack, "Illegal stream attribute parameter") ;
    }

}

// fill a vector or string

AIKIDO_NATIVE (fill) {
    const Value &obj = paras[1] ;
    const Value &val = paras[2] ;
    if (!isIntegral (paras[3])) {
        throw newParameterException (vm, stack, "fill", "Illegal start value") ;
    }
    if (!isIntegral (paras[4])) {
        throw newParameterException (vm, stack, "fill", "Illegal end value") ;
    }
    INTEGER start = getInt (paras[3]) ;
    INTEGER end = getInt (paras[4]) ;

    if (start > end) {
        INTEGER tmp = start ;
        start = end ;
        end = tmp ;
    }

    switch (obj.type) {
    case T_VECTOR: {
        if (end > obj.vec->size()) {
            obj.vec->vec.resize (end) ;
        }
        for (int i = start ; i < end ; i++) {
            (*obj.vec)[i] = val ;
        }
        break ;
        }
    case T_BYTEVECTOR: {
        if (end > obj.bytevec->size()) {
            obj.bytevec->vec.resize (end) ;
        }
        for (int i = start ; i < end ; i++) {
            (*obj.bytevec)[i] = val ;
        }
        break ;
        }
    case T_STRING: {
        if (!isIntegral (val)) {
            throw newParameterException (vm, stack, "fill", "Integer, byte or char type required") ;
        }
        if (end > obj.str->size()) {
            obj.str->str.resize (end) ;
        }
        int chval = getInt (val) ;
        for (int i = start ; i < end ; i++) {
            obj.str->setchar (i,(char)chval) ;
        }
        break ;
        }
    default:
        throw newParameterException (vm, stack, "fill", "Illegal object type") ;
    }
    return 0 ;
}

//
// change the size of a vector or string
//
AIKIDO_NATIVE (resize) {
    if (!isIntegral (paras[2])) {
        throw newParameterException (vm, stack, "resize", "Integer size required") ;
    }
    switch (paras[1].type) {
    case T_VECTOR: 
        paras[1].vec->vec.resize (getInt (paras[2])) ;
        break ;
    case T_BYTEVECTOR: 
        paras[1].bytevec->vec.resize (getInt (paras[2])) ;
        break ;
    case T_STRING:
        paras[1].str->str.resize (getInt (paras[2])) ;
        break ;
    default:
        throw newParameterException (vm, stack, "resize", "Illegal object type") ;
    }
    return 0 ;
}

// find things in values

AIKIDO_NATIVE (findn) {
    const Value &obj = paras[1] ;
    const Value &val = paras[2] ;
    if (!isIntegral (paras[3])) {
        throw newParameterException (vm, stack, "find", "Integer required for index") ;
    }
    if (!isIntegral (paras[4])) {
        throw newParameterException (vm, stack, "find", "Integer required for direction") ;
    }
    
    int index = getInt (paras[3]) ;
    int dir = getInt (paras[4]) ;
    if (dir < -1 || dir > 1) {
        throw newParameterException (vm, stack, "find", "Invalid value for direction") ;
    }
    Scope *scope ;
    Tag *tag ;
    Block *block ;

    switch (obj.type) {
    case T_VECTOR: {
        if (index < 0 || index >= obj.vec->size()) {
            throw newParameterException (vm, stack, "find", "Invalid value for index") ;
        }
        if (dir < 0) {
            int i = index ;
            while (i >= 0) {
                if (vm->cmpeq ((*obj.vec)[i], val)) {
                    return i ;
                }
                i-- ;
            }
        } else {
            int i = index ;
            while (i < obj.vec->size()) {
                if (vm->cmpeq ((*obj.vec)[i], val)) {
                    return i ;
                }
                i++ ;
            }
        }
        return -1 ;
        break ;
        }
    case T_BYTEVECTOR: {
        if (!isIntegral (val)) {
            throw newParameterException (vm, stack, "find", "Illegal type for value to find") ;
        }
        if (index < 0 || index >= obj.bytevec->size()) {
            throw newParameterException (vm, stack, "find", "Invalid value for index") ;
        }
        unsigned char fval = (unsigned char)getInt (val) ;
        if (dir < 0) {
            int i = index ;
            while (i >= 0) {
                if ((*obj.bytevec)[i] == fval) {
                    return i ;
                }
                i-- ;
            }
        } else {
            int i = index ;
            while (i < obj.vec->size()) {
                if ((*obj.bytevec)[i] == fval) {
                    return i ;
                }
                i++ ;
            }
        }
        return -1 ;
        break ;
        }

    case T_STRING:
        if (index < 0 || index > obj.str->size()) {
            throw newParameterException (vm, stack, "find", "Invalid value for index") ;
        }
        switch (val.type) {
        case T_CHAR:
        case T_BYTE:
        case T_INTEGER:
            if (dir < 0) {
                return static_cast<int>(obj.str->str.rfind ((char)getInt (val), index)) ;
            } else {
                return static_cast<int>(obj.str->str.find ((char)getInt (val), index)) ;
            }

        case T_STRING:
            if (dir < 0) {
                return static_cast<int>(obj.str->str.rfind (val.str->str, index)) ;
            } else {
                return static_cast<int>(obj.str->str.find (val.str->str, index)) ;
            }
        default:
            return -1 ;
        }
        break ;

    case T_MAP: 
        return obj[val] ;
        break ;

    case T_OBJECT: 
        if (obj.object == NULL) {
            return 0 ;
        }
        tag = (Tag*)obj.object->block->findVariable (".find", scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (tag != NULL) {
            Operand tmp ;
            vm->callFunction (&tmp, tag->block, obj, val, paras[3], paras[4]) ;
            return tmp.val ;
        }
        if (val.type != T_STRING) {
           throw newParameterException (vm, stack, "find", "Illegal block name type") ;
        }
        tag = (Tag*)obj.object->block->findVariable (string (".") + *val.str, scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (tag == NULL) {
            return 0 ;
        }
        goto getblock ;

    case T_CLASS:
    case T_FUNCTION:
    case T_THREAD:
    case T_MONITOR:
    case T_PACKAGE: 
        if (val.type != T_STRING) {
           throw newParameterException (vm, stack, "find", "Illegal block name type") ;
        }
        tag = (Tag*)obj.block->findVariable (string (".") + *val.str, scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (tag == NULL) {
            return 0 ;
        }
	goto getblock ;
    case T_CLOSURE:
        if (val.type != T_STRING) {
           throw newParameterException (vm, stack, "find", "Illegal block name type") ;
        }
        tag = (Tag*)obj.closure->block->findVariable (string (".") + *val.str, scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (tag == NULL) {
            return 0 ;
        }

    getblock:
        block = tag->block ;
        switch (block->type) {
        case T_CLASS:
            return (Class*)block ;

        case T_FUNCTION:
            return (Function*)block ;
        case T_THREAD:
            return (Thread*)block ;
        case T_MONITOR:
            return (MonitorBlock*)block ;
        case T_PACKAGE: 
            return (Package*)block ;
        case T_ENUM:
            return (Enum*)block ;
        default:
            return 0 ;
        }
        break ;

    default:
        throw newParameterException (vm, stack, "find", "Illegal object type") ;
    }
}

static void getBlockContents (Value::vector *vec, Block *block) {
    if (block->superblock != NULL) {
        getBlockContents (vec, block->superblock->block) ;
    }
    Scope::VarMap::iterator i ;
    for (i = block->variables.begin() ; i != block->variables.end() ; i++) {
        Variable *var = (*i).second ;
        vec->push_back (new string (var->name)) ;
    }
}


// based on the Java hashCode functionality
static INTEGER hashCode (VirtualMachine *vm, StackFrame *stack, const Value &obj) {
    INTEGER h = 0 ;
    switch (obj.type) {
    case T_NONE:
        return 0 ;

    case T_INTEGER:
    case T_CHAR:
    case T_BYTE:
    case T_ENUMCONST:
        return getInt (obj) ;

    case T_REAL:
        return (INTEGER)obj.real ;

    case T_STRING: {
        int off = 0;
        const char *val = obj.str->c_str() ;
        int len = obj.str->size();

        if (len < 16) {
            for (int i = len ; i > 0; i--) {
                h = (h * 37) + val[off++];
            }
        } else {
            // only sample some characters
            int skip = len / 8;
            for (int i = len ; i > 0; i -= skip, off += skip) {
                h = (h * 39) + val[off];
            }
        }

        return h;
        break ;
        }
    case T_VECTOR: 
        for (int i = 0 ; i < obj.vec->size() ; i++) {
            h += hashCode (vm, stack, (*obj.vec)[i]) ;
        }
        return h ;
    case T_BYTEVECTOR: {
        int off = 0;
        unsigned char *val = &(*(obj.bytevec->begin())) ;
        int len = obj.bytevec->size();

        if (len < 16) {
            for (int i = len ; i > 0; i--) {
                h = (h * 37) + val[off++];
            }
        } else {
            // only sample some characters
            int skip = len / 8;
            for (int i = len ; i > 0; i -= skip, off += skip) {
                h = (h * 39) + val[off];
            }
        }
        return h;
        }
        
    case T_MAP: {
        map::iterator i = obj.m->begin() ;
        while (i != obj.m->end()) {
            h += hashCode (vm, stack, (*i).first) ;
            h += hashCode (vm, stack, (*i).second) ;
        }
        return h ;
        }
    case T_OBJECT: {
        Scope *scope ;
        if (obj.object == NULL) {
            return 0 ;
        }
        Tag *tag = (Tag*)obj.object->block->findVariable (".hash", scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (tag != NULL) {
            Operand tmp ;
            vm->callFunction (&tmp, tag->block, obj) ;
            if (!isIntegral (tmp.val)) {
                throw newException (vm, stack, "Illegal hash code type returned from object") ;
            }
            return getInt (tmp.val) ;
        } else {
            return reinterpret_cast<INTEGER>(obj.object) ;
        }
        break ;
        }
    default:
        return reinterpret_cast<INTEGER>(obj.ptr) ;
    }
}

AIKIDO_NATIVE (hash) {
    return hashCode (vm, stack, paras[1]) ;
}

//
// split a value up
//
AIKIDO_NATIVE (split) {
    const Value &obj = paras[1] ;
    const Value &sep = paras[2] ;
    Tag *tag ;
    Scope *scope ;
    Block *block ;

    switch (obj.type) {
    case T_STRING: {
        switch (sep.type) {
        case T_INTEGER:
        case T_BYTE:
        case T_CHAR: {
            Value::vector *vec = new Value::vector (10) ;
            vec->clear() ;
            std::string::size_type i, oldi = 0 ;
            do {
                i = obj.str->str.find ((char)getInt (sep), oldi) ;
                if (i != std::string::npos) {
                    string *s = new string (obj.str->str.substr (oldi, i - oldi)) ;
                    vec->push_back (s) ;
                    oldi = obj.str->str.find_first_not_of ((char)getInt (sep), i) ;
                    if (oldi == std::string::npos) {
                        break ;
                    }
                } else {
                    string *s = new string (obj.str->str.substr (oldi)) ;
                    vec->push_back (s) ;
                }
            } while (i != std::string::npos) ;
            return vec ;
            break ;
            }
        case T_STRING: {
            Value::vector *vec = new Value::vector (10) ;
            vec->clear() ;
            std::string::size_type i, oldi = 0 ;
            do {
                i = obj.str->str.find (sep.str->str, oldi) ;
                if (i != std::string::npos) {
                    string *s = new string (obj.str->str.substr (oldi, i - oldi)) ;
                    vec->push_back (s) ;
                    oldi = i + sep.str->size() ;
                } else {
                    string *s = new string (obj.str->str.substr (oldi)) ;
                    vec->push_back (s) ;
                }
            } while (i != std::string::npos) ;
            return vec ;
            break ;
            }
        default:
            throw newParameterException (vm, stack, "split", "Illegal string separator") ;
        }
        break ;
        }

    case T_VECTOR: {
        Value::vector *vec = new Value::vector (10) ;
        vec->clear() ;
        int i, oldi = 0 ;
        int size = obj.vec->size() ;
        Value::vector::iterator elem ;
        do {
            i = oldi ;
            elem = obj.vec->begin() + i ;
            while (i < size) {
                if (vm->cmpeq (*elem, sep)) {
                    break ;
                }
                i++ ;
                elem++ ;
            }				// comes out with elem = element at separator or i == size
            if (i <= size) {
                if (i == (oldi + 1)) {		// only one value
                    vec->push_back ((*obj.vec)[oldi]) ;
                } else {
                    Value::vector *v = new Value::vector (i - oldi) ;
                    v->clear() ;
                    while (oldi != i) {
                        v->push_back ((*obj.vec)[oldi]) ;
                        oldi++ ;
                    }
                    vec->push_back (v) ;
                }
                elem = obj.vec->begin() + i ;
                while (i < size) {
                    if (vm->cmpne (*elem,sep)) {
                        break ;
                    }
                    i++ ;
                    elem++ ;
                }				// comes out with elem = element at separator or i == size
                oldi = i ;
            }
        } while (i < size) ;

        return vec ;
        break ;
        }
    case T_BYTEVECTOR: {
        if (!isIntegral (sep)) {
            throw newParameterException (vm, stack, "split", "Illegal bytevector separator") ;
        }
        unsigned char ssep = (unsigned char)getInt (sep) ;
        Value::vector *vec = new Value::vector (10) ;
        vec->clear() ;
        int i, oldi = 0 ;
        int size = obj.bytevec->size() ;
        Value::bytevector::iterator elem ;
        do {
            i = oldi ;
            elem = obj.bytevec->begin() + i ;
            while (i < size) {
                if (*elem == ssep) {
                    break ;
                }
                i++ ;
                elem++ ;
            }				// comes out with elem = element at separator or i == size
            if (i <= size) {
                if (i == (oldi + 1)) {		// only one value
                    vec->push_back ((*obj.bytevec)[oldi]) ;
                } else {
                    Value::bytevector *v = new Value::bytevector (i - oldi) ;
                    v->clear() ;
                    while (oldi != i) {
                        v->push_back ((*obj.bytevec)[oldi]) ;
                        oldi++ ;
                    }
                    vec->push_back (v) ;
                }
                elem = obj.bytevec->begin() + i ;
                while (i < size) {
                    if (*elem != ssep) {
                        break ;
                    }
                    i++ ;
                    elem++ ;
                }				// comes out with elem = element at separator or i == size
                oldi = i ;
            }
        } while (i < size) ;

        return vec ;
        break ;
        }

    case T_OBJECT:
        if (obj.object == NULL) {
            return 0 ;
        }
        tag = (Tag*)obj.object->block->findVariable (".split", scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (tag != NULL) {
            Operand tmp ;
            vm->callFunction (&tmp, tag->block, obj, sep) ;
            return tmp.val ;
        }
        block = obj.object->block ;
        goto getblockcontents ;

    case T_CLASS:
    case T_FUNCTION:
    case T_THREAD:
    case T_MONITOR:
    case T_PACKAGE: 
        block = obj.block ;
	goto getblockcontents ;

    case T_CLOSURE:
	block = obj.closure->block ;

    getblockcontents: {
        Value::vector *vec = new Value::vector (block->stacksize) ;
        vec->clear() ;
        getBlockContents (vec, block) ;
        return vec ;
        break ;
        }
        
    default:
        throw newParameterException (vm, stack, "split", "Illegal object type") ;
    }
}

//
// reduce a value to a simpler one
//

AIKIDO_NATIVE (reduce) {
    const Value &val = paras[1] ;
    const Value &fval = paras[2] ;
    if (fval.type != T_CLOSURE || fval.closure->type != T_FUNCTION) {
        throw newParameterException (vm, stack, "transform", "Illegal function type") ;
        
    }
    Function *func = (Function*)fval.closure->block ;
    Operand tmp ;
    switch (val.type) {
    default:
        vm->callFunction (&tmp, func, fval.closure->slink, paras[0], val) ;
        return tmp.val ;
        break ;
    case T_STRING: {
        if (val.str->size() == 0) {
            throw newException (vm, stack, string ("Cannot reduce a zero length string")) ;
        }
        string *rval = new string();
        for (int i = 0 ; i < val.str->size() ; i++) {
            Operand op ;
            vm->callFunction (&op, func, fval.closure->slink, rval, (*val.str)[i]) ;
            if (op.val.type != T_CHAR && op.val.type != T_STRING) {
                throw newException (vm, stack, string ("Illegal type returned from reduction function")) ;
            }
            if (op.val.type == T_CHAR) {
                rval->push_back ((char)getInt (op.val)) ;
            } else {
                *rval += *op.val.str ;
            }
        }
        return rval ;
        break ;
        }
    case T_VECTOR: {
        if (val.vec->size() == 0) {
            throw newException (vm, stack, string ("Cannot reduce a zero length vector")) ;
        }
        Value rval = (*val.vec)[0];     // first value
        for (int i = 1 ; i < val.vec->size() ; i++) {
            Operand op ;
            vm->callFunction (&op, func, fval.closure->slink, rval, (*val.vec)[i]) ;
            rval = op.val;
        }
        return rval ;
        break ;
        }
    case T_MAP:
        break ;

    case T_CLASS:
    case T_FUNCTION:
    case T_THREAD:
    case T_MONITOR:
    case T_PACKAGE: 
        break ;
    }
    return val ;
}

//
// merge two vectors or strings
//

AIKIDO_NATIVE (merge) {
    const Value &val1 = paras[1] ;
    const Value &val2 = paras[2] ;
    switch (val1.type) {
    default:
        throw newException (vm, stack, string ("Cannot merge this type")) ;
        
    case T_VECTOR: {
        Value::vector *v = new Value::vector (val1.vec->size()+val2.vec->size()) ;
        std::merge (val1.vec->begin(), val1.vec->end(), val2.vec->begin(), val2.vec->end(), v->vec.begin());
        return v ;
        }
    }
    return val1 ;
}

AIKIDO_NATIVE (reverse) {
    const Value &val = paras[1] ;
    switch (val.type) {
    default:
        throw newException (vm, stack, string ("Cannot merge this type")) ;
        
    case T_VECTOR: {
        Value::vector *v = new Value::vector (val.vec->size()) ;
        *v = *val.vec;
        //for (unsigned int i = 0 ; i < val.vec->size() ; i++) {
            //(*v)[i] = (*val.vec)[i];
        //}
        std::reverse (v->begin(), v->end());
        return v ;
        }
    }
    return val ;
}

AIKIDO_NATIVE (transform) {
    const Value &val = paras[1] ;
    const Value &fval = paras[2] ;
    if (fval.type != T_CLOSURE || fval.closure->type != T_FUNCTION) {
        throw newParameterException (vm, stack, "transform", "Illegal function type") ;
        
    }
    Function *func = (Function*)fval.closure->block ;
    Operand tmp ;
    switch (val.type) {
    default:
        vm->callFunction (&tmp, func, fval.closure->slink, paras[0], val) ;
        return tmp.val ;
        break ;
    case T_STRING: {
        string *s = new string ;
        for (int i = 0 ; i < val.str->size() ; i++) {
            Operand op ;
            vm->callFunction (&op, func, fval.closure->slink, paras[0], (*val.str)[i]) ;
            if (op.val.type != T_CHAR) {
                throw newException (vm, stack, string ("Illegal type returned from transformation function")) ;
            }
            s->push_back ((char)getInt (op.val)) ;
        }
        return s ;
        break ;
        }
    case T_VECTOR: {
        Value::vector *v = new Value::vector (val.vec->size()) ;
        v->clear() ;
        for (int i = 0 ; i < val.vec->size() ; i++) {
            Operand op ;
            vm->callFunction (&op, func, fval.closure->slink, paras[0], (*val.vec)[i]) ;
            v->push_back (op.val) ;
        }
        return v ;
        break ;
        }
    case T_BYTEVECTOR: {
        Value::bytevector *v = new Value::bytevector (val.bytevec->size()) ;
        v->clear() ;
        for (int i = 0 ; i < val.bytevec->size() ; i++) {
            Operand op ;
            vm->callFunction (&op, func, fval.closure->slink, paras[0], (*val.bytevec)[i]) ;
            v->push_back ((unsigned char)getInt (op.val)) ;
        }
        return v ;
        break ;
        }
    case T_MAP:
        break ;

    case T_CLASS:
    case T_FUNCTION:
    case T_THREAD:
    case T_MONITOR:
    case T_PACKAGE: 
        break ;
    }
    return val ;
}


//
// trim space off a value
//
AIKIDO_NATIVE (trim) {
    const Value &val = paras[1] ;
    switch (val.type) {
    case T_STRING: {
        string *s = new string (*val.str) ;			// copy string
        // remove initial white space
        int i = 0 ;
        while (i < s->size() && isspace ((*s)[i])) {
            i++ ;
        }
        if (i != 0) {
            s->erase (0, i) ;
        }

        // remove trailing white space
        i = s->size() - 1 ;
        while (i >= 0 && isspace ((*s)[i])) {
            i-- ;
        }
        i++ ;
        if (i >= 0 && i < s->size()) {
            s->erase (i, s->size()) ;
        }

        return s ;
        break ;
        }
    case T_VECTOR: {
        Value::vector *v = new Value::vector (val.vec->size()) ;
        v->clear() ;
        int len = val.vec->size() ;
        int pos = len - 1 ;
        int end = pos ;
        // find last populated value
        while (pos >= 0) {
            if ((*val.vec)[pos].type != T_NONE) {
                end = pos ;
                break ;
            }
            pos-- ;
        }
        // find first populated value
        pos = 0 ;
        while (pos <= end) {
            if ((*val.vec)[pos].type != T_NONE) {
                break ;
            }
            pos++ ;
        }
        // copy all populated values
        while (pos <= end) {
            v->push_back ((*val.vec)[pos]) ;
            pos++ ;
        }

        return v ;
        break ;
        }
    case T_BYTEVECTOR: {
        Value::bytevector *v = new Value::bytevector (val.bytevec->size()) ;
        v->clear() ;
        int len = val.vec->size() ;
        int pos = len - 1 ;
        int end = pos ;
        // find last nonzero value
        while (pos >= 0) {
            if ((*val.vec)[pos] != 0) {
                end = pos ;
                break ;
            }
            pos-- ;
        }
        // find first populated value
        pos = 0 ;
        while (pos <= end) {
            if ((*val.vec)[pos] != 0) {
                break ;
            }
            pos++ ;
        }
        // copy all populated values
        while (pos <= end) {
            v->push_back ((*val.vec)[pos]) ;
            pos++ ;
        }

        return v ;
        break ;
        }
    case T_MAP:
        break ;

    case T_CLASS:
    case T_FUNCTION:
    case T_THREAD:
    case T_MONITOR:
    case T_PACKAGE: 
        break ;
    }
    return val ;
}

//
// replace things in a value
//
AIKIDO_NATIVE (replace) {
    const Value &val = paras[1] ;
    const Value &findval = paras[2] ;
    const Value &replval = paras[3] ;
    if (!isIntegral (paras[4])) {
        throw newParameterException (vm, stack, "replace", "Illegal type for 'all' parameter") ;
    }
    bool all = paras[4].integer ;

    switch (val.type) {
    case T_STRING: {
        if (findval.type != T_STRING && findval.type != T_CHAR) {
            throw newParameterException (vm, stack, "replace", "Illegal type for find parameter (string or char expected)") ;
        }
        if (replval.type != T_STRING && replval.type != T_CHAR) {
            throw newParameterException (vm, stack, "replace", "Illegal type for repl parameter (string or char expected)") ;
        }
        string *ss = new string (*val.str) ;			// copy string
        std::string &s = ss->str ;
        std::string fc ;
        std::string rc ;
        if (findval.type == T_CHAR) {
            fc += (char)findval.integer ;
        }
        if (replval.type == T_CHAR) {
            rc += (char)replval.integer ;
        }
        std::string &f = findval.type == T_STRING ? findval.str->str : fc ;
        std::string &r = replval.type == T_STRING ? replval.str->str : rc ;

        std::string::size_type i, oldi = 0 ;
        if (all) {
            do {
                i = s.find (f, oldi) ;
                if (i != std::string::npos) {
                    s.replace (i, f.size(), r) ;
                    oldi = i + r.size() ;
                }
         
            } while (i != std::string::npos) ;
        } else {
            i = s.find (f) ;
            if (i != std::string::npos) {
                s.replace (i, f.size(), r) ;
            }
        }
        return ss ;
        break ;
        }
    case T_VECTOR: {
        Value::vector *v = new Value::vector (val.vec->size()) ;
        v->clear() ;
        int len = val.vec->size() ;
        int cmpok = true ;
        for (int i = 0 ; i < len ; i++) {
            if (cmpok && (*val.vec)[i] == findval) {
                v->push_back (replval) ;
                if (!all) {
                    cmpok = false ;		// stop after first one
                }
            } else {
                v->push_back ((*val.vec)[i]) ;
            }
        }
        return v ;
        break ;
        }
    case T_BYTEVECTOR: {
        Value::bytevector *v = new Value::bytevector (val.bytevec->size()) ;
        v->clear() ;
        int len = val.vec->size() ;
        if (!isIntegral(findval)) {
            throw newParameterException (vm, stack, "replace", "Illegal type for find parameter") ;
        }
        if (!isIntegral (replval)) {
            throw newParameterException (vm, stack, "replace", "Illegal type for repl parameter") ;
        }
        unsigned char fv = (unsigned char)findval.integer ;
        unsigned char rv = (unsigned char)replval.integer ;
        int cmpok = true ;
        for (int i = 0 ; i < len ; i++) {
            if (cmpok && (*val.bytevec)[i] == fv) {
                v->push_back (rv) ;
                if (!all) {
                    cmpok = false ;		// stop after first one
                }
            } else {
                v->push_back ((*val.bytevec)[i]) ;
            }
        }
        return v ;
        break ;
        }
    case T_MAP:
        break ;

    case T_CLASS:
    case T_FUNCTION:
    case T_THREAD:
    case T_MONITOR:
    case T_PACKAGE: 
        break ;
    }
    return val ;
}


//
// clear a value
//
AIKIDO_NATIVE (clear) {
    const Value &val = paras[1] ;
    switch (val.type) {
    case T_VECTOR:
        val.vec->vec.clear() ;
        break ;
    case T_BYTEVECTOR:
        val.bytevec->vec.clear() ;
        break ;
    case T_MAP:
        val.m->m.clear() ;
        break ;
    case T_STRING:
#if defined(_OS_MACOSX) || defined(_OS_LINUX) || defined (_CC_GCC)
        val.str->str = "" ;
#else
        val.str->str.clear() ;
#endif
        break ;
     default:
        throw newParameterException (vm, stack, "clear", "Illegal type for parameter") ;
    }
    return 0 ;
}

//
// append to a value
//
AIKIDO_NATIVE (append) {
    const Value &obj = paras[1] ;
    const Value &val = paras[2] ;

    switch (obj.type) {
    case T_STRING: {
        if (val.type != T_STRING && val.type != T_CHAR) {
            throw newParameterException (vm, stack, "append", "Illegal type for val parameter") ;
        }
        if (val.type == T_STRING) {
            obj.str->str += val.str->str ;
        } else {
            obj.str->str +=  (char)val.integer ;
        }
        break ;
        }
    case T_VECTOR: {
        obj.vec->push_back (val) ;
        break ;
        }
    case T_BYTEVECTOR: {
        if (!isIntegral(val)) {
            throw newParameterException (vm, stack, "append", "Illegal type for val parameter") ;
        }
        obj.bytevec->push_back (getInt (val)) ;
        break ;
        }
    case T_MAP:
        (*obj.m)[val] = 1 ;
        break ;

    default:
        throw newException (vm, stack, "Cannot append to this type") ;
        break ;
    }
    return 0 ;
}

//
// insert into a value
//
AIKIDO_NATIVE (insert) {
    const Value &obj = paras[1] ;
    const Value &val = paras[2] ;
    const Value &indexv = paras[3] ;

    if (!isIntegral (indexv)) {
            throw newParameterException (vm, stack, "insert", "Illegal type for index parameter") ;
    }
    int index = getInt (indexv) ;

    switch (obj.type) {
    case T_STRING: {
        if (val.type != T_STRING && val.type != T_CHAR) {
            throw newParameterException (vm, stack, "insert", "Illegal type for val parameter") ;
        }
        if (index < 0 || index > obj.str->size()) {
            throw newParameterException (vm, stack, "insert", "Index out of range") ;
        }
        if (val.type == T_STRING) {
            obj.str->str.insert (index, val.str->str) ;
        } else {
            char buf[2] ;
            buf[0] = (char)val.integer ;
            buf[1] = 0 ;
            obj.str->str.insert (index, buf) ;
        }
        break ;
        }
    case T_VECTOR: {
        if (index < 0 || index > obj.vec->size()) {
            throw newParameterException (vm, stack, "insert", "Index out of range") ;
        }
        obj.vec->vec.insert (obj.vec->vec.begin() + index, val) ;
        break ;
        }
    case T_BYTEVECTOR: {
        if (index < 0 || index > obj.bytevec->size()) {
            throw newParameterException (vm, stack, "insert", "Index out of range") ;
        }
        if (!isIntegral(val)) {
            throw newParameterException (vm, stack, "append", "Illegal type for val parameter") ;
        }
        obj.bytevec->vec.insert (obj.bytevec->vec.begin() + index, getInt (val)) ;
        break ;
        }
    case T_MAP:
        (*obj.m)[val] = 1 ;
        break ;

    default:
        throw newException (vm, stack, "Cannot insert into this type") ;
        break ;
    }
    return 0 ;
}

//
// low level memory allocation
//
AIKIDO_NATIVE (malloc) {
    const Value &size = paras[1] ;
    if (!isIntegral (size)) {
        throw newParameterException (vm, stack, "malloc", "Illegal type for 'size' parameter") ;
    }
    RawMemory *mem = new RawMemory (getInt (size)) ;
    return mem ;
}

//
// poke a value into memory.  Be careful with alignment of address
//

AIKIDO_NATIVE (ipoke) {
    if (b->properties & LOCKDOWN) {
        throw newException (vm, stack, "Security violation") ;
    }
    const Value &addr = paras[1] ;
    const Value &value = paras[2] ;
    const Value &size = paras[3] ;
    if (!isIntegral (addr) && addr.type != T_MEMORY && addr.type != T_POINTER) {
        throw newParameterException (vm, stack, "poke", "Illegal type for 'address' parameter") ;
    }
    if (!isIntegral (value) && value.type != T_BYTEVECTOR && value.type != T_STRING) {
        throw newParameterException (vm, stack, "poke", "Illegal type for 'value' parameter") ;
    }
    if (!isIntegral (size)) {
        throw newParameterException (vm, stack, "poke", "Illegal type for 'size' parameter") ;
    }
    void *address ;
    if (isIntegral(addr)) {
        address =  reinterpret_cast<void*>(getInt(addr)) ;
    } else if (addr.type == T_MEMORY) {
        address = (void*)addr.mem->mem ;
    } else {
        address = (void*)(addr.pointer->mem->mem + addr.pointer->offset) ;
    }
    switch (getInt(size)) {
    case 1:
        if (isIntegral (value)) {
            *(unsigned char*)address = (unsigned char)getInt (value) ;
        } else {
            switch (value.type) {
            case T_BYTEVECTOR:
               *(unsigned char*)address = (*value.bytevec)[0] ;
               break ;
            case T_STRING:
               *(unsigned char*)address = (*value.str)[0] ;
               break ;
            }
        }
        break ;
    case 2:
        if (isIntegral (value)) {
            *(unsigned short*)address = (unsigned short)getInt (value) ;
        } else {
            switch (value.type) {
            case T_BYTEVECTOR:
               *(unsigned char*)address = (*value.bytevec)[0] ;
               *((unsigned char*)address + 1) = (*value.bytevec)[1] ;
               break ;
            case T_STRING:
               *(unsigned char*)address = (*value.str)[0] ;
               *((unsigned char*)address + 1) = (*value.str)[1] ;
               break ;
            }
        }
        break ;
    case 4:
        if (isIntegral (value)) {
            *(unsigned int*)address = (unsigned int)getInt (value) ;
        } else {
            switch (value.type) {
            case T_BYTEVECTOR:
               *(unsigned char*)address = (*value.bytevec)[0] ;
               *((unsigned char*)address + 1) = (*value.bytevec)[1] ;
               *((unsigned char*)address + 2) = (*value.bytevec)[2] ;
               *((unsigned char*)address + 3) = (*value.bytevec)[3] ;
               break ;
            case T_STRING:
               *(unsigned char*)address = (*value.str)[0] ;
               *((unsigned char*)address + 1) = (*value.str)[1] ;
               *((unsigned char*)address + 2) = (*value.str)[2] ;
               *((unsigned char*)address + 3) = (*value.str)[3] ;
               break ;
            }
        }
        break ;
    case 8:
        if (isIntegral (value)) {
            *(INTEGER*)address = (INTEGER)getInt (value) ;
        } else {
            switch (value.type) {
            case T_BYTEVECTOR:
               *(unsigned char*)address = (*value.bytevec)[0] ;
               *((unsigned char*)address + 1) = (*value.bytevec)[1] ;
               *((unsigned char*)address + 2) = (*value.bytevec)[2] ;
               *((unsigned char*)address + 3) = (*value.bytevec)[3] ;
               *((unsigned char*)address + 4) = (*value.bytevec)[4] ;
               *((unsigned char*)address + 5) = (*value.bytevec)[5] ;
               *((unsigned char*)address + 6) = (*value.bytevec)[6] ;
               *((unsigned char*)address + 7) = (*value.bytevec)[7] ;
               break ;
            case T_STRING:
               *(unsigned char*)address = (*value.str)[0] ;
               *((unsigned char*)address + 1) = (*value.str)[1] ;
               *((unsigned char*)address + 2) = (*value.str)[2] ;
               *((unsigned char*)address + 3) = (*value.str)[3] ;
               *((unsigned char*)address + 4) = (*value.str)[4] ;
               *((unsigned char*)address + 5) = (*value.str)[5] ;
               *((unsigned char*)address + 6) = (*value.str)[6] ;
               *((unsigned char*)address + 7) = (*value.str)[7] ;
               break ;
            }
        }
        break ;
    default:
        if (isIntegral (value)) {
            throw newParameterException (vm, stack, "poke", "Illegal type for 'value' parameter for this size") ;
        } else {
            int sz = getInt (size) ;
            for (int i = 0 ; i < sz ; i++) {
               if (value.type == T_STRING) {
                   *((unsigned char*)address + i) = (*value.str)[i] ;
               } else {
                   *((unsigned char*)address + i) = (*value.bytevec)[i] ;
               }
            }
        }
    }
    return 0 ;
}

//
// read a value from memory
//
AIKIDO_NATIVE (ipeek) {
    const Value &addr = paras[1] ;
    const Value &size = paras[2] ;
    if (!isIntegral (addr) && addr.type != T_MEMORY && addr.type != T_POINTER) {
        throw newParameterException (vm, stack, "peek", "Illegal type for 'address' parameter") ;
    }
    if (!isIntegral (size)) {
        throw newParameterException (vm, stack, "peek", "Illegal type for 'size' parameter") ;
    }
    void *address ;
    if (isIntegral(addr)) {
        address =  reinterpret_cast<void*>(getInt(addr)) ;
    } else if (addr.type == T_MEMORY) {
        address = (void*)addr.mem->mem ;
    } else {
        address = (void*)(addr.pointer->mem->mem + addr.pointer->offset) ;
    }
    if (paras[3].integer != 0) {   // force byte vector?
        int sz = getInt(size) ;
        Value::bytevector *vec = new Value::bytevector(sz) ;
        vec->clear() ;
        for (int i = 0 ; i < sz ; i++) {
            vec->push_back (*((unsigned char*)address + i)) ;
        }
        return vec ;
    } else {
        switch (getInt(size)) {
        case 1:
            return (INTEGER)(*(unsigned char*)address) ;
        case 2:
            return (INTEGER)(*(unsigned short*)address) ;
        case 4:
            return (INTEGER)(*(unsigned int*)address) ;
        case 8:
            return *(INTEGER*)address ;
        default: {
            int sz = getInt(size) ;
            Value::bytevector *vec = new Value::bytevector(sz) ;
            vec->clear() ;
            for (int i = 0 ; i < sz ; i++) {
                vec->push_back (*((unsigned char*)address + i)) ;
            }
            return vec ;
            }
        }
    }
}

// 
// convert the bits in an integer into a real value
//
AIKIDO_NATIVE (bitsToReal) {
   const Value &bits = paras[1] ;
   return *(double*)(&bits.integer) ;
}

// the value contains a real number, convert it to an integer as bits
// (that is, no floating point to integer conversion is done)

AIKIDO_NATIVE (realToBits) {
    INTEGER i = *((INTEGER*)&paras[1].real) ;
    return i ;
}

AIKIDO_NATIVE (parsedate) {
    Object *dateobj = paras[2].object ;
    const char *str = paras[1].str->c_str() ;
    parsedate (dateobj, str) ;
    return 0 ;
}


static void fromDate (struct tm *t, Object *obj) {
    t->tm_sec = (int)getInt (obj->varstack[2]) ;
    t->tm_min = (int)getInt (obj->varstack[3]) ;
    t->tm_hour = (int)getInt (obj->varstack[4]) ;
    t->tm_mday = (int)getInt (obj->varstack[5]) ;
    t->tm_mon = (int)getInt (obj->varstack[6]) ;
    t->tm_year = (int)getInt (obj->varstack[7]) ;
    t->tm_wday = (int)getInt (obj->varstack[8]) ;
    t->tm_yday = (int)getInt (obj->varstack[9]) ;
    t->tm_isdst = (int)getInt (obj->varstack[10]) ;
#if defined (_OS_MACOSX)
    if (obj->varstack[12].type == T_STRING) {
        t->tm_zone = (char*)obj->varstack[12].str->str.c_str() ;
    } else {
        t->tm_zone = (char*)"" ;
    }
#elif defined (_OS_LINUX) && !defined(_OS_CYGWIN)
    t->tm_gmtoff = (int)getInt (obj->varstack[11]) ;
    t->tm_zone = (char*)obj->varstack[12].str->str.c_str() ;
#endif
}

AIKIDO_NATIVE (formatdate) {
    char buf[1024] ;
    const char *fmt = NULL ;
    if (paras[1].type == T_STRING) {
        fmt = paras[1].str->c_str() ;
    }
    Object *dateobj = paras[2].object ;
    struct tm time ;
    fromDate (&time, dateobj) ;
    
    size_t len = strftime (buf, 1024, fmt, &time) ;
    return string (buf) ;
}

AIKIDO_NATIVE (normalizedate) {
    Object *dateobj = paras[1].object ;
    struct tm time ;
    time.tm_isdst = -1 ;         // make mktime work for its living
    fromDate (&time, dateobj) ;
    time_t t = mktime (&time) ;
    dateobj->varstack[2] = Value (time.tm_sec) ;
    dateobj->varstack[3] = Value (time.tm_min) ;
    dateobj->varstack[4] = Value (time.tm_hour) ;
    dateobj->varstack[5] = Value (time.tm_mday) ;
    dateobj->varstack[6] = Value (time.tm_mon) ;
    dateobj->varstack[7] = Value (time.tm_year) ;
    dateobj->varstack[8] = Value (time.tm_wday) ;
    dateobj->varstack[9] = Value (time.tm_yday) ;
    // don't set dst flag, leave it as is
    return (INTEGER)t ;
}

AIKIDO_NATIVE (getSystemStatus) {
    return vm->getLastSystemResult() ;
}


// get expansion for file list
AIKIDO_NATIVE (glob) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "glob", "Illegal type for glob (string expected)") ;
    }
    std::string filename = lockFilename (b, paras[1]) ;

    OS::Expansion x ;
    Value::vector *result = new Value::vector() ;
    if (OS::expandFileName (filename.c_str(), x)) {
        try {
            for (int i = 0 ; i < x.names.size() ; i++) {
                if (b->properties & LOCKDOWN) {
                    // if locked down, remove the root dir from the filename
                    std::string fn = x.names[i] ;
                    if (fn.size() > b->lockdownRootdir.size()) {
                        result->push_back (new string (fn.substr (b->lockdownRootdir.size()-1))) ;
                    } else {
                        result->push_back (new string ("/")) ;
                    }
                } else {
                    result->push_back (new string (x.names[i])) ;
                }
            }
        } catch (...) {
            OS::freeExpansion (x) ;
            throw ;
        }
        OS::freeExpansion (x) ;
    }
    return result ;
}

AIKIDO_NATIVE (doprintln) {
    Value nl('\n') ;
    Value out = &paras[2] ;
    vm->stream (NULL, const_cast<Value&>(paras[1]), out) ;
    vm->stream (NULL, nl, out) ;
    return 0 ;
}

AIKIDO_NATIVE (doprint) {
    Value out = &paras[2] ;
    vm->stream (NULL, const_cast<Value&>(paras[1]), out) ;
    return 0 ;
}

AIKIDO_NATIVE (doread) {
    Value in = paras[2] ;
    vm->stream (NULL, in, const_cast<Value&>(paras[1])) ;
    return 0 ;
}

AIKIDO_NATIVE (doreadln) {
    Value in = paras[2] ;
    vm->stream (NULL, in, const_cast<Value&>(paras[1])) ;
    if (in.type == T_NONE) {
        Value &in = vm->input ;
        if (in.type == T_STREAM) {
            char ch;
            do {
                ch = in.stream->get();
            } while (!in.stream->eof() && ch != '\n');
        }
    } else if (in.type == T_STREAM) {
        char ch;
        do {
            ch = in.stream->get();
        } while (!in.stream->eof() && ch != '\n');
    }
    return 0 ;
}

AIKIDO_NATIVE (setOutput) {
    vm->output = paras[1] ;
    return 0 ;
}

AIKIDO_NATIVE (getOutput) {
    return vm->output ;
}

AIKIDO_NATIVE (createVariable) {
    Token access ;
    switch (paras[4].integer) {
    case 1:
    default:
        access = PUBLIC ;
        break ;
    case 2:
        access = PRIVATE ;
        break ;
    case 3:
        access = PROTECTED ;
        break ;
    }
    b->createVariable (paras[1].object, paras[2].str->str, const_cast<Value&>(paras[3]), access, (bool)paras[5].integer, (bool)paras[6].integer) ;
    return 0 ;
}

AIKIDO_NATIVE (resume_coroutine) {
    if (paras[1].type != T_CLOSURE) {
        throw newParameterException (vm, stack, "resume_coroutine", "Illegal type for coroutine (closure expected)") ;
    }
    Closure *c = paras[1].closure ;
    if (c->vm_state == NULL || c->type != T_FUNCTION) {
        throw newException (vm, stack,"Invalid coroutine closure") ;
    }
    return vm->invoke_coroutine (c) ;
}

AIKIDO_NATIVE (get_closure_value) {
    if (paras[1].type == T_CLOSURE) {
        Closure *c = paras[1].closure ;
        if (c->vm_state == NULL || c->type != T_FUNCTION) {
            return paras[1] ;
        }
        return c->value ;
    }
    return paras[1] ;       // for non-closure, return the value itself
}


AIKIDO_NATIVE (findNativeFunction) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "findNativeFunction", "Illegal type for symbol name") ;
    }
    string name = *paras[1].str;
    void *sym = OS::findSymbol (name.c_str());

    // need to make this into a native function
    RawNativeFunction *func = new RawNativeFunction (b, name, -1, (NativeFunction::FuncPtr)sym, currentScopeLevel, currentScope);
    func->parentBlock = b->mainStack->block;
    Value funcvalue (func);
    Value dest;
    vm->make_closure (dest, funcvalue);
    return dest;
}

//
// value serialization
//

enum SerialType {
    SERIALIZE_XML = 1,
    SERIALIZE_JSON,
    SERIALIZE_JSON_EXPORT,
    SERIALIZE_BINARY
};


typedef std::map<Object*, std::string> SerialInfo ;

static std::string lookupSerial (SerialInfo &info, Object* obj) {
    SerialInfo::iterator i = info.find (obj) ;
    if (i == info.end()) {
        return "" ;
    }
    return i->second ;
}

static void addSerialInfo (SerialInfo &info, Object *key, std::string name) {
    info[key] = name ;
}

static std::string getObjectName (SerialInfo &info) {
    char buf[256];
    sprintf (buf, "object-%d", (int)info.size());
    return buf;
}

// serialization type constants
enum Stype{
   STYPE_INTEGER = 0x40,
   STYPE_CHAR,
   STYPE_BYTE,
   STYPE_STRING,
   STYPE_REAL,
   STYPE_VECTOR,
   STYPE_BYTEVECTOR,
   STYPE_MAP,
   STYPE_OBJECT,
   STYPE_MONITOR,
   STYPE_REF,
   STYPE_EOB,
   STYPE_MEMBER,
   
};

static void serialize_leb128 (INTEGER v, std::ostream &output) {
    bool more = true ;
    while (more) {
        int byte = v & 0x7fLL ;
        v >>= 7LL ;
        if ((v == 0 && ((byte & 0x40) == 0)) || (v == -1 && (byte & 0x40))) {
            more = false ;
        } else {
            byte |= 0x80 ;
        }
        output.put (byte) ;
    }
}

static void serialize_string (std::string s, std::ostream &output) {
    serialize_leb128 (s.size(), output);
    for (unsigned int i = 0 ; i < s.size() ; i++) {
        output.put (s[i]);
    }
}

static void serialize (VirtualMachine *vm, const Value &v, SerialInfo &info, std::ostream &output, int type) ;

static void serializeBlock (VirtualMachine *vm, Value *base, Block *blk, SerialInfo &info, std::ostream &output, int type, bool &comma) {
    if (blk->superblock != NULL) {
        serializeBlock (vm, base, blk->superblock->block, info, output, type, comma) ;
    }

    Scope::VarMap::iterator i ;

    for (i = blk->variables.begin() ; i != blk->variables.end() ; i++) {
        Variable *var = (*i).second ;
        if (var->name[0] == '.') {          // tag?  Tags have no storage, the refer to vars
            continue ;
        }
        if (var->name == "this") {
            continue;
        }
        if (var->flags & VAR_STATIC) {          // don't clone static members
            continue ;
        }
        Value &val = base[var->offset] ;
        switch (val.type) {
        case T_FUNCTION:
        case T_CLASS:
        case T_PACKAGE:
        case T_THREAD:
        case T_ENUM:
        case T_ENUMCONST:
            continue;
        default:
            if (type == SERIALIZE_XML) {
                output << "<member name=\"" << var->name << "\">";
                serialize (vm, val, info, output, type);
                output << "</member>";
            } else if (type == SERIALIZE_JSON || type == SERIALIZE_JSON_EXPORT) {
                if (comma) {
                    output << ",";
                }
                output << var->name << ":";
                serialize (vm, val, info, output, type);
                comma = true;
            } else {
                output.put (STYPE_MEMBER);
                serialize_string (var->name.str, output);
                serialize (vm, val, info, output, type);
            }
        }
    }
}

static std::string escape (std::string s) {
    std::string news;
    for (unsigned int i = 0 ; i < s.size() ; i++) {
        char ch = s[i];
        if (ch == '\"') {
            news += "\\\"";
        } else if (ch == '\\') {
            news += "\\\\";
        } else if (ch == '<') {
            news += "&lt;";
        } else if (ch == '>') {
            news += "&gt;";
        } else if (ch == '&') {
            news += "&amp;";
        } else {
            news += ch;
        }
    }
    return news;
}

static std::string unescape (std::string s) {
    std::string news;
    for (unsigned int i = 0 ; i < s.size() ; i++) {
        char ch = s[i];
        if (ch == '\\') {
            i++;
            news += s[i];
        } else if (ch == '&') {
            i++;
            if (s[i] == 'l') {
                news =+ '<';
            } else if (s[i] == 'g') {
                news =+ '>';
            } else if (s[i] == 'a') {
                news =+ '&';
            } else {
                i--;
                news += ch;
            }
        } else {
            news += ch;
        }
    }
    return news;
}

static void serialize (VirtualMachine *vm, const Value &v, SerialInfo &info, std::ostream &output, int type) {
    char buf[256];
    switch (v.type) {
    case T_INTEGER:
        if (type == SERIALIZE_BINARY) {
            output.put (STYPE_INTEGER);
            serialize_leb128 (v.integer, output);
        } else if (type == SERIALIZE_XML) {
            output << "<integer value=\"" << v.integer << "\"/>";
        } else {
            output << v.integer;
        }
        break;
    case T_CHAR:
        if (type == SERIALIZE_BINARY) {
            output.put (STYPE_CHAR);
            serialize_leb128 (v.integer, output);
        } else if (type == SERIALIZE_XML) {
            output << "<char value=\"" << (char)v.integer << "\"/>";
        } else {
            std::string s;
            s += (char)v.integer;
            if (type == SERIALIZE_JSON_EXPORT) {
                output << '"' << escape(s) << '"';        // JSON strings are double quoted
            } else {
                output << "'" << escape(s) << "'";      // output as char
            }
        }
        break;
    case T_BYTE:
        if (type == SERIALIZE_BINARY) {
            output.put (STYPE_BYTE);
            serialize_leb128 (v.integer, output);
        } else if (type == SERIALIZE_XML) {
            output << "<byte value=\"" << (v.integer & 0xff) << "\"/>";
        } else {
            output << (v.integer & 0xff);
        }
        break;
    case T_REAL:
        if (type == SERIALIZE_BINARY) {
            output.put (STYPE_REAL);
            serialize_leb128 (*((INTEGER*)&v.real), output);
        } else if (type == SERIALIZE_XML) {
            output << "<real value=\"" << v.real << "\"/>";
        } else {
            output << v.real;
        }
        break;
    case T_STRING: 
        if (type == SERIALIZE_BINARY) {
            std::string s = v.str->str;
            output.put (STYPE_STRING);
            serialize_string (s, output);
        } else if (type == SERIALIZE_XML) {
            output << "<string value=\"" << escape(v.str->str) << "\"/>";
        } else {
            output << '"' << escape(v.str->str) << '"';
        }
        break;
    case T_VECTOR: {
        std::string begin;
        std::string end;
        std::string sep;
        bool outsep = false;

        if (type == SERIALIZE_XML) {
            sprintf (buf, "<vector size=\"%d\">", v.vec->size());
            begin = buf;
            end = "</vector>";
        } else if (type == SERIALIZE_JSON || type == SERIALIZE_JSON_EXPORT) {
            begin = "[";
            end = "]";
            sep = ",";
        } else if (type == SERIALIZE_BINARY) {
            output.put (STYPE_VECTOR);
            serialize_leb128 (v.vec->size(), output);
            for (int i = 0 ; i < v.vec->size() ; i++) {
                serialize (vm, (*v.vec)[i], info, output, type);
            }
            break;
        }

        // NOTE: only get here for non binary
        output << begin;
        for (int i = 0 ; i < v.vec->size() ; i++) {
            if (outsep) {
                output << sep;
            }
            outsep = true;
          
            serialize (vm, (*v.vec)[i], info, output, type);
        }
        output << end;
        break;
        }
    case T_BYTEVECTOR: {
        std::string begin;
        std::string end;
        std::string sep;
        bool outsep = false;

        if (type == SERIALIZE_XML) {
            sprintf (buf, "<bytevector size=\"%d\">", v.bytevec->size());
            begin = buf;
            end = "</bytevector>";
        } else if (type == SERIALIZE_JSON || type == SERIALIZE_JSON_EXPORT) {
            begin = "[";
            end = "]";
            sep = ",";
        } else if (type == SERIALIZE_BINARY) {
            output.put (STYPE_BYTEVECTOR);
            serialize_leb128 (v.bytevec->size(), output);
            for (int i = 0 ; i < v.bytevec->size() ; i++) {
                output.put ((*v.bytevec)[i]);
            }
            break;
        }

        // NOTE: only get here for non binary
        output << begin;
        for (int i = 0 ; i < v.vec->size() ; i++) {
            if (outsep) {
                output << sep;
            }
            outsep = true;

            if (type == SERIALIZE_XML) {
                output << "<byte value=\"" << (*v.bytevec)[i] << "\"/>";
            } else {
                output << (*v.bytevec)[i];
            }
        }
        output << end;
        break;
        }
    case T_MAP: {
        std::string begin;
        std::string end;
        std::string sep;
        bool outsep = false;

        if (type == SERIALIZE_XML) {
            sprintf (buf, "<map size=\"%d\">", v.m->size());
            begin = buf;
            end = "</map>";
        } else if (type == SERIALIZE_JSON || type == SERIALIZE_JSON_EXPORT) {
            begin = "{";
            end = "}";
            sep = ",";
        } else if (type == SERIALIZE_BINARY) {
            output.put (STYPE_MAP);
            serialize_leb128 (v.m->size(), output);
            map::iterator s;
            for (s = v.m->begin() ; s != v.m->end() ; s++) {
                const Value &key = (*s).first ;
                Value &val = (*s).second ;
                serialize (vm,key, info, output, type);
                serialize (vm,val, info, output, type);
            }
            break;
        }

        output << begin;

        map::iterator s ;
        for (s = v.m->begin() ; s != v.m->end() ; s++) {
            if (outsep) {
                output << sep;
            }
            outsep = true;

            const Value &key = (*s).first ;
            Value &val = (*s).second ;
            if (type == SERIALIZE_XML) {
                output << "<first>";
                serialize (vm, key, info, output, type);
                output << "</first>";

                output << "<second>";
                serialize (vm, val, info, output, type);
                output << "</second>";

            } else if (type == SERIALIZE_JSON || type == SERIALIZE_JSON_EXPORT) {
                serialize (vm, key, info, output, type);
                output << ":";
                serialize (vm,val, info, output, type);
            } else {
                serialize (vm,key, info, output, type);
                serialize (vm,val, info, output, type);
            }
        }
        output << end;
        }
    case T_STREAM:
        break;

    case T_MONITOR:
    case T_OBJECT: {
        if (v.object ==NULL) {
            if (type == SERIALIZE_XML) {
                output << "null";
            } else if (type == SERIALIZE_JSON || type == SERIALIZE_JSON_EXPORT) {
                output << "null";
            } else {
                serialize_leb128 (0, output);
            }
            break;
        }
        std::string name;
        if (type != SERIALIZE_JSON_EXPORT) {
            name = lookupSerial (info, v.object) ;
            if (name != "") {
                if (type == SERIALIZE_XML) {
                    output << "<objectref name=\"" << name << "\"/>";
                } else if (type == SERIALIZE_JSON) {
                    output << "\"@aikidoobjectref@" << name << '"';
                } else {
                    output.put (STYPE_REF);
                    serialize_string (name, output);
                }
                break;
            }
        }
        name = getObjectName (info);

        bool comma = false;

        switch (type) {
        case SERIALIZE_XML:     // XML
            if (v.type == T_OBJECT) {
                output << "<object name=\"" << name << "\" type=\"" << vm->typestring(v) << "\">";
            } else {
                output << "<monitor name=\"" << name << "\" type=\"" << vm->typestring(v) << "\">";
            }
            break;
        case SERIALIZE_JSON:     //JSON
            output << "{__aikido_object_name:\"" << name << "\"" << ",__aikido_object_type:\"" << vm->typestring(v) << "\"";
            if (v.type == T_MONITOR) {
               output << ",__aikido_is_monitor=true";
            }
            comma = true;
            break;
        case SERIALIZE_JSON_EXPORT:     //JSON
            output <<"{";
            break;
        case SERIALIZE_BINARY:
            if (v.type == T_OBJECT) {
                output.put (STYPE_OBJECT);
            } else {
                output.put (STYPE_MONITOR);
            }
            serialize_string (name, output);
            serialize_string (vm->typestring(v).str, output);
            break;
        }
        Block *blk = v.object->block ;
        Value *base = v.object->varstack ;

        addSerialInfo (info, v.object, name) ;		// add to clone info

        serializeBlock (vm, base, blk, info, output, type, comma) ;

        switch (type) {
        case SERIALIZE_XML:     // XML
            output << (v.type == T_OBJECT ? "</object>" : "</monitor>");
            break;
        case SERIALIZE_JSON:     //JSON
        case SERIALIZE_JSON_EXPORT:     //JSON
            output << "}";
            break;
        case SERIALIZE_BINARY:
            output.put (STYPE_EOB);     // end of block
            break;
        }
        break;
        }
        
    case T_FUNCTION:
    case T_CLASS:
    case T_PACKAGE:
    case T_THREAD:
    case T_ENUM:
    case T_ENUMCONST:
        break;
    }
}


AIKIDO_NATIVE (serialize) {
    if (!isIntegral(paras[2])) {
        throw newParameterException (vm, stack, "serialize", "Illegal type for parameter 'type'") ;
    }

    if (paras[3].type != T_STREAM && paras[3].type != T_NONE && paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "serialize", "Illegal type for parameter 'stream'") ;
    }

    int sertype = getInt (paras[2]);
    switch (sertype) {
    case SERIALIZE_XML:     // XML
    case SERIALIZE_JSON:     // JSON
    case SERIALIZE_JSON_EXPORT:     // JSON
    case SERIALIZE_BINARY:     // binary
        {
        SerialInfo info;
        std::ostream *stream;
        std::stringstream sstream;
        if (paras[3].type == T_NONE) {
            stream = &vm->output.stream->getOutStream();
        } else if (paras[3].type == T_STRING) {
            stream = &sstream;
        } else {
            stream = &paras[3].stream->getOutStream();
        }
        serialize (vm, paras[1], info, *stream, sertype);

        // copy string stream to string 
        if (paras[3].type == T_STRING) {
            paras[3].str->str = sstream.str();
        }
        break;
        }
        
    default:
        throw newParameterException (vm, stack, "serialize", "Unknown value for 'type'") ;
    }
    return 0;
}

typedef std::map<std::string, Object *> DeserialInfo ;

static Object *lookupDeserial (DeserialInfo &info, std::string name) {
    DeserialInfo::iterator i = info.find (name) ;
    if (i == info.end()) {
        return NULL ;
    }
    return i->second ;
}

static void addDeserialInfo (DeserialInfo &info, std::string name, Object *obj) {
    info[name] = obj ;
}

static INTEGER deserialize_leb128 (std::istream &in) {
    INTEGER result = 0 ;
    int shift = 0 ;
    int size = 64 ;
    unsigned char byte = 0 ;
    for (;;) { 
        byte = in.get() ;
        result |= (INTEGER)(byte & 0x7f) << shift ;
        shift += 7 ;
        if ((byte & 0x80) == 0) {
            break ;
        }
    }
    if ((shift < size) && (byte & 0x40)) {
        result |= - (1LL << shift) ;
    }
    return result ;

}

static std::string deserialize_string (std::istream &in) {
    INTEGER length = deserialize_leb128 (in);
    std::string s;
    for (unsigned int i = 0 ; i < length ; i++) {
        s += in.get();
    }
    return s;
}



typedef std::map<std::string, Value> MemberMap;
static void deserialize_block (Object *obj, Class *cls, DeserialInfo &info, MemberMap &members, Aikido *aikido, VirtualMachine *vm, aikido::StackFrame *stack, aikido::StaticLink *staticLink, aikido::Scope *currentScope, int currentScopeLevel, int sertype) {

    // first assign the parameters
    for (unsigned int i = 0 ; i < cls->parameters.size() ; i++) {
        Parameter *p = cls->parameters[i];
        MemberMap::iterator memi = members.find (p->name.str);
        if (memi != members.end()) {
            Value &val = memi->second;
            p->setValue (val, obj);
        }
    }

    // now construct the object
    obj->ref++ ;
    vm->execute (cls, obj, staticLink, 0) ;
    obj->ref-- ;

    // now assign the members
    Scope::VarMap::iterator i ;

    for (i = cls->variables.begin() ; i != cls->variables.end() ; i++) {
        Variable *var = (*i).second ;
        if (var->name[0] == '.') {          // tag?  Tags have no storage, the refer to vars
            continue ;
        }
        if (var->flags & VAR_STATIC) {          
            continue ;
        }
        MemberMap::iterator memi = members.find (var->name.str);
        if (memi != members.end()) {
            Value &val = memi->second;
            bool valset = false;
            if (sertype == SERIALIZE_JSON) {
                // for JSON, check for object refs
                if (val.type == T_STRING) {
                    std::string s = val.str->str;
                    if (s.find ("@aikidoobjectref@") != std::string::npos) {
                        s = s.substr (17);
                        Object *o = lookupDeserial (info, s);
                        var->setValue (o, obj);
                        valset = true;
                    }
                }
            }
            if (!valset)  {
                var->setValue (val, obj);
            }
        }
    }
}


static Value deserialize_xml_value (Aikido *aikido, VirtualMachine *vm, XML::Element *node, DeserialInfo &info, aikido::StackFrame *stack, aikido::StaticLink *staticLink, aikido::Scope *currentScope, int currentScopeLevel) {
    if (node->name == "integer") {
        std::string value = node->getAttribute ("value");
		INTEGER n = 0;
		for (unsigned int i = 0 ; i < value.size(); i++) {
			char ch = value[i];
			n = n * 10 + ch - '0';
		}
        return Value (n);
    } else if (node->name == "char") {
        std::string value = node->getAttribute ("value");
        return Value (value[0]);
    } else if (node->name == "byte") {
        std::string value = node->getAttribute ("value");
        return Value (((INTEGER)value[0] & 0xff));
    } else if (node->name == "real") {
        std::string value = node->getAttribute ("value");
        return Value (strtod (value.c_str(), NULL));
    } else if (node->name == "string") {
        std::string value = node->getAttribute ("value");
        return Value (unescape(value));
    } else if (node->name == "vector") {
        int size = atoi(node->getAttribute ("size").c_str());
        Value::vector *vec = new Value::vector (size);
        std::vector<XML::Element*> elements = node->getChildren();
        for (int i = 0 ; i < size; i++) {
            XML::Element *el = elements[i];
            (*vec)[i] = deserialize_xml_value (aikido, vm, el, info, stack, staticLink, currentScope, currentScopeLevel);
        }
        return vec;
    } else if (node->name == "map") {
        map *m = new map();
        std::vector<XML::Element*> children = node->getChildren();
        Value first;
        Value second;
        for (int i = 0 ; i < children.size(); i++) {
            XML::Element *child = children[i];
            if (child->name == "first") {
                XML::Element *v = child->getChildren()[0];
                first = deserialize_xml_value (aikido, vm, v, info, stack, staticLink, currentScope, currentScopeLevel);
            } else if (child->name == "second") {
                XML::Element *v = child->getChildren()[0];
                second = deserialize_xml_value (aikido, vm, v, info, stack, staticLink, currentScope, currentScopeLevel);
                (*m)[first] = second;
            }
        }
        return m;

    } else if (node->name == "object"  || node->name == "monitor") {
        std::string name = node->getAttribute ("name");
        std::string type = node->getAttribute ("type");
        MemberMap members;

        Tag *tag = aikido->findTag ("." + type);
        if (tag != NULL) {
            Class *cls = (Class*)tag->block;
            Object *obj = NULL;
            if (node->name == "object") {
                obj = new (cls->stacksize)  Object (cls, staticLink, stack, &cls->code->code[0]);
            } else {
                obj = new (cls->stacksize)  Monitor (cls, staticLink, stack, &cls->code->code[0]);
            }

            addDeserialInfo (info, name, obj);

            std::vector<XML::Element*> blockchildren = node->getChildren();
            for (unsigned int i = 0 ; i < blockchildren.size() ; i++) {
               XML::Element *child = blockchildren[i];
               if (child->name == "member") {
                   std::string memname = child->getAttribute ("name");
                   Value val;
                   std::vector<XML::Element*> memchildren = child->getChildren();
                   if (memchildren.size() == 0) {
                       continue;
                   }
                   for (unsigned int j = 0 ; j < memchildren.size() ; j++) {
                       XML::Element *memvalue = memchildren[j];
                       val = deserialize_xml_value (aikido, vm, memvalue, info, stack, staticLink, currentScope, currentScopeLevel);
                       break;
                   }
                   
                   members[memname] = val;
               }
            }

            obj->varstack[0] = obj;
            deserialize_block (obj, cls, info, members, aikido, vm, stack, staticLink, currentScope, currentScopeLevel, 1);
            return obj;
        } else {
            throw newException (vm, stack, "Cannot find object class");
        }
    } else if (node->name == "objectref") {
        std::string name = node->getAttribute ("name");
        Object *obj = lookupDeserial (info, name);
        return obj;
    } else {
        throw newException (vm, stack, "Deserialization error: unknown XML node");
    }

    return 0;
}

static Value deserialize_xml (Aikido *aikido, VirtualMachine *vm, DeserialInfo &info, XML::Element *tree, aikido::StackFrame *stack, aikido::StaticLink *staticLink, aikido::Scope *currentScope, int currentScopeLevel) {
    std::vector<XML::Element*> children = tree->getChildren();
    for (unsigned int i = 0 ; i < children.size() ; i++) {
        XML::Element *node = children[i];
        return deserialize_xml_value (aikido, vm, node, info, stack, staticLink, currentScope, currentScopeLevel);
    }
    return 0;
}

static std::string parse_json_string (std::istream &in) {
    char ch = in.get() ;
    std::string s ;
    if (ch == '"') {
        ch = in.get() ;
        while (ch != '"') {
            s += ch ;
            ch = in.get() ;
        }
    } else {
        while (ch != ':' && ch != ',') {
            s += ch;
            ch = in.get();
        }
        in.unget();
    }
    return s ;
}

static void skip_json_spaces (std::istream &in) {
    char ch = in.get();
    while (!in.eof() && isspace (ch)) {
       ch = in.get();
    }
    in.unget();
}


static Value deserialize_json_value (Aikido *aikido, VirtualMachine *vm, DeserialInfo &info, std::istream &in, aikido::StackFrame *stack, aikido::StaticLink *staticLink, aikido::Scope *currentScope, int currentScopeLevel) {
    char ch = in.get() ;
    if (ch == '"') {
        string *s = new string() ;
        ch = in.get() ;
        while (!in.eof() && ch != '"') {
            *s += ch ;
            ch = in.get() ;
        }
        return s ;
    } else if (ch == '{') {
        MemberMap members;

        ch = '\0';
        while (!in.eof() && ch != '}') {
            skip_json_spaces (in);
            std::string propname = parse_json_string (in) ;
            skip_json_spaces (in);
            ch = in.get() ;
            if (ch == ':') {
                skip_json_spaces (in);
                Value v = deserialize_json_value (aikido, vm, info, in, stack, staticLink, currentScope, currentScopeLevel) ;
       
                 if (v.type != T_NONE) {
                     members[propname] = v;
                 }
            }
            skip_json_spaces (in);
            ch = in.get() ;
            if (ch != ',') {
                break ;
            }
        }

        // check for Aikido object by looking for __aikido_object_name and __aikido_object_type as members
        MemberMap::iterator i = members.find ("__aikido_object_name");
        std::string name;
        std::string type;
        bool isobject = false;
        if (i != members.end()) {
            name = i->second.str->str;
            i = members.find ("__aikido_object_type");
            if (i != members.end()) {
                type = i->second.str->str;
                isobject = true;
            }
        }

        if (isobject) {
            Tag *tag = aikido->findTag ("." + type);
            if (tag != NULL) {
                Class *cls = (Class*)tag->block;
                Object *obj = NULL;
                i = members.find ("__aikido_is_monitor");
                if (i == members.end()) {
                    obj = new (cls->stacksize)  Object (cls, staticLink, stack, &cls->code->code[0]);
                } else {
                    obj = new (cls->stacksize)  Monitor (cls, staticLink, stack, &cls->code->code[0]);
                }

                addDeserialInfo (info, name, obj);

                obj->varstack[0] = obj;
                deserialize_block (obj, cls, info, members, aikido, vm, stack, staticLink, currentScope, currentScopeLevel, 2);
                return obj;
            } else {
                isobject = false;       // just a map
            }
        }
        if (!isobject) {
            // map
            map *m = new map();
            for (i = members.begin() ; i != members.end() ; i++) {
                Value propvalue = Value (new string (i->first));
                (*m)[propvalue] = i->second;
            }
            return m; 
        }

    } else if (ch == '[') {
        Value::vector *list = new Value::vector() ;
        while (!in.eof()) {
            skip_json_spaces (in);
            Value v = deserialize_json_value (aikido, vm, info, in, stack, staticLink, currentScope, currentScopeLevel) ;
            if (v.type == T_NONE) {
                break ;
            }
            list->push_back (v) ;
            skip_json_spaces (in);
            ch = in.get() ;
            if (ch != ',') {
                break ;
            }
        }
        return list;
    } else if (ch == ']' || ch == '}') {
        // close vector or map
        return Value() ;
    } else if (isdigit (ch) || ch == '-') {
        // number
        bool negative = ch == '-';
        if (negative) {
           ch = in.get();
        }
        char numbuf[256];
        int n = 0;
        bool fp = false;
        numbuf[n++] = ch;
        while (!in.eof() && n < (sizeof (numbuf)-1)) {
            ch = in.get() ;
            if (ch == '.') {
                fp = true;
            } else if (ch == 'e' || ch == 'E') {
                fp = true;
            } else if (fp && (ch == '+' || ch == '-')) {
                // nothing to do
            } else if (!isdigit(ch)) {
                in.unget() ;
                break ;
            }
            numbuf[n++] = ch;
        }
        numbuf[n] = '\0';
        if (fp) {
            return negative ? -strtod (numbuf, NULL) : strtod (numbuf, NULL);
        } else {
            return negative ? -OS::strtoll (numbuf, NULL, 10) : OS::strtoll (numbuf, NULL, 10);
        }
    } else if (isalpha (ch) || ch == '_') {
        std::string s ;
        s += ch ;
        while (!in.eof()) {
            ch = in.get() ;
            if (ch != '_' && !isalnum (ch)) {
                in.unget() ;
                break ;
            }
            s += ch ;
        }
        if (s == "true") {
            return 1;
        } else if (s == "false") {
            return 0 ;
        } else if (s == "null") {
            return Value((Object*)NULL) ;
        } else {
            return new string (s) ;
        }
    } else {
        throw newException (vm, stack, "JSON Parse error") ;
    }

}

static Value deserialize_binary_value (Aikido *aikido, VirtualMachine *vm, DeserialInfo &info, std::istream &in, aikido::StackFrame *stack, aikido::StaticLink *staticLink, aikido::Scope *currentScope, int currentScopeLevel) {
    int stype = in.get();
    switch (stype) {
    case STYPE_INTEGER:
        return Value (deserialize_leb128 (in));

    case STYPE_CHAR:
        return Value ((char)deserialize_leb128 (in));
    case STYPE_BYTE:
        return Value ((unsigned char)deserialize_leb128 (in));
    case STYPE_STRING:
        return Value (new string (deserialize_string (in)));
    case STYPE_REAL: {
        INTEGER i = deserialize_leb128 (in);
        double d = *(double*)&i;
        return d;
        }
    case STYPE_VECTOR: {
        INTEGER length = deserialize_leb128 (in);
        Value::vector *v = new Value::vector(length);
        for (int i = 0 ; i < length ; i++) {
            (*v)[i] = deserialize_binary_value (aikido, vm, info, in, stack, staticLink, currentScope, currentScopeLevel);
        }
        return v;
        }
    case STYPE_BYTEVECTOR: {
        INTEGER length = deserialize_leb128 (in);
        Value::bytevector *v = new Value::bytevector(length);
        for (int i = 0 ; i < length ; i++) {
            (*v)[i] = in.get();
        }
        return v;
        }
    case STYPE_MAP: {
        INTEGER length = deserialize_leb128 (in);
        map *m = new map();
        for (int i = 0 ; i < length ; i++) {
            Value first = deserialize_binary_value (aikido, vm, info, in, stack, staticLink, currentScope, currentScopeLevel);
            Value second = deserialize_binary_value (aikido, vm, info, in, stack, staticLink, currentScope, currentScopeLevel);
            (*m)[first] = second;
        }
        return m;
        }

    case STYPE_REF: {
        std::string name = deserialize_string (in);
        Object *obj = lookupDeserial (info, name);
        return obj;
        }
        
    case STYPE_OBJECT:
    case STYPE_MONITOR: {
        MemberMap members;

        std::string name = deserialize_string (in);
        std::string type = deserialize_string (in);

        while (!in.eof()) {
            char ch = in.get();
            if (ch == STYPE_MEMBER) {
                std::string member = deserialize_string (in);
                Value val = deserialize_binary_value (aikido, vm, info, in, stack, staticLink, currentScope, currentScopeLevel);
                members[member] = val;
            } else {
                break;
            }
        }

        Tag *tag = aikido->findTag ("." + type);
        if (tag != NULL) {
            Class *cls = (Class*)tag->block;
            Object *obj = NULL;
            if (stype == STYPE_OBJECT) {
                obj = new (cls->stacksize)  Object (cls, staticLink, stack, &cls->code->code[0]);
            } else {
                obj = new (cls->stacksize)  Monitor (cls, staticLink, stack, &cls->code->code[0]);
            }

            addDeserialInfo (info, name, obj);

            obj->varstack[0] = obj;
            deserialize_block (obj, cls, info, members, aikido, vm, stack, staticLink, currentScope, currentScopeLevel, 2);
            return obj;
        } else {
            return Value();
        }
        }
    default:
        return Value();
    }
}



AIKIDO_NATIVE (deserialize) {
    if (!isIntegral(paras[1])) {
        throw newParameterException (vm, stack, "deserialize", "Illegal type for parameter 'type'") ;
    }

    if (paras[2].type != T_STREAM && paras[2].type != T_NONE && paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "deserialize", "Illegal type for parameter 'stream'") ;
    }

    DeserialInfo info;
    std::istream *stream;
    std::stringstream sstream;

    if (paras[2].type == T_NONE) {
        stream = &vm->input.stream->getInStream();
    } else if (paras[2].type == T_STRING) {
        sstream << paras[2].str->str;
        stream = &sstream;
    } else {
        stream = &paras[2].stream->getInStream();
    }
    int sertype = getInt (paras[1]);
    switch (sertype) {
    case SERIALIZE_XML: {    // XML
        XML::Parser parser (*stream);
        XML::Element *tree = NULL;
        Value v;
        try {
            tree = parser.parseStream();
            v = deserialize_xml (b, vm, info, tree, stack, staticLink, currentScope, currentScopeLevel);
        } catch (...) {
            delete tree;
            throw;
        }
        delete tree;
        return v;
        break;
        }
    case SERIALIZE_JSON:     // JSON
    case SERIALIZE_JSON_EXPORT:     // JSON
        return deserialize_json_value (b, vm, info, *stream, stack, staticLink, currentScope, currentScopeLevel);
    case SERIALIZE_BINARY:     // binary
        return deserialize_binary_value (b, vm, info, *stream, stack, staticLink, currentScope, currentScopeLevel);
        
        
    default:
        throw newParameterException (vm, stack, "serialize", "Unknown value for 'type'") ;
    }
    return 0;
}

AIKIDO_NATIVE (constructObject) {
    if (paras[1].type != T_CLASS && paras[1].type != T_MONITOR && paras[1].type != T_CLOSURE) {
        throw newParameterException (vm, stack, "constructObject", "Need a class or monitor for 'type'") ;
    }
    if (paras[2].type != T_MAP) {
        throw newParameterException (vm, stack, "constructObject", "Need a map for 'attributes'") ;
    }

    MemberMap members;
    map::iterator i;
    map *m = paras[2].m;
    for (i = m->begin() ; i != m->end() ; i++) {
        const Value &name = i->first;
        if (name.type != T_STRING) {
            throw newParameterException (vm, stack, "constructObject", "Attributes must have keys of type string") ;
        }
        members[name.str->str] = i->second;
    }

    if (paras[1].type == T_CLOSURE) {
        if (paras[1].closure->type != T_CLASS && paras[1].closure->type != T_MONITOR) {
            throw newParameterException (vm, stack, "constructObject", "Need a class or monitor for 'type'") ;
        }
    }

    Class *cls = paras[1].type == T_CLOSURE ? (Class*)paras[1].closure->block : paras[1].cls;
    Object *obj = NULL;
    if (paras[1].type == T_OBJECT) {
        obj = new (cls->stacksize)  Object (cls, staticLink, stack, &cls->code->code[0]);
    } else {
        obj = new (cls->stacksize)  Monitor (cls, staticLink, stack, &cls->code->code[0]);
    }

    obj->varstack[0] = obj;
    DeserialInfo info;      // not used here
    deserialize_block (obj, cls, info, members, b, vm, stack, staticLink, currentScope, currentScopeLevel, 2);
    return obj;
}

AIKIDO_NATIVE(setParserDelegate) {
    if (paras[1].type != T_OBJECT) {
        throw newParameterException (vm, stack, "setParserDelegate", "Invalid object type (need an object that implements ParserDelegate)") ;
    }
    parserDelegate = paras[1].object;
    incRef (parserDelegate, object);        // prevent garbage collection
    b->worker->initialize();
    return 0;
}




AIKIDO_NATIVE (expand) {
    if (paras[1].type != T_STREAM && paras[1].type != T_NONE && paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "expand", "Illegal type for parameter 'stream'") ;
    }

    std::istream *istr;
    std::stringstream sstream;

    if (paras[1].type == T_NONE) {
        istr = &vm->input.stream->getInStream();
    } else if (paras[1].type == T_STRING) {
        sstream << paras[1].str->str;
        istr = &sstream;
    } else {
        istr = &paras[1].stream->getInStream();
    }
   
   std::string news;
   std::string expr;

   while (!istr->eof()) {
       char ch = istr->get();
       if (ch == '#') {
           ch = istr->get();
           if (ch == '#') {
              // ## is #
              news += '#';
           } else {
               expr = "";
               if (ch == '{') {
                  int nbra = 1;
                  while (!istr->eof()) {
                      ch = istr->get();
                      if (ch == '{') {
                          nbra++;
                          expr += ch;
                      } else if (ch == '}') {
                          nbra--;
                          if (nbra == 0) {
                              break;
                          } else {
                              expr += ch;
                          }
                      } else {
                          expr += ch;
                      }
                  }
               } else {
                   // not #{...}
                   news += "#";
                   continue;        
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
                string filename (vm->getIR()->source->filename) ;

                Context ctx (stream, filename, vm->getIR()->source->lineno) ;
                parserLock.acquire(true);
                b->currentContext = &ctx ;
                b->readLine() ;
                stream.clear() ;
                b->nextToken() ;

                StackFrame *parentStack = stack->dlink ;        // parent stack frame is caller of eval
                //Block *parentBlock = parentStack->block ;     // want to be in this block
                b->currentStack = parentStack ;

                b->currentScope = currentScope ; 
                b->currentScopeLevel = currentScopeLevel ;
                b->currentMajorScope = currentScope->majorScope ;
                b->currentClass = NULL ;
                try {
                    Node *node = b->expression() ;
                    Node *tree = node ;
                    b->currentContext = ctx.getPrevious() ;

                    if (node != NULL) {
                        node = new Node (b, RETURN, node) ;
                        codegen::CodeSequence *code = b->codegen->generate (node) ;
                        vm->execute (code) ;
                        b->currentStack = NULL ;
                        b->deleteTree (tree) ;
                        parserLock.release(true) ;
                        Value result = vm->retval ;
                        vm->retval.destruct() ;
                        delete code ;
                        vm->convertType (result, value);
                    } else {
                        b->currentStack = NULL ;
                        b->deleteTree (tree) ;
                        parserLock.release(true) ;
                        value = Value("none");
                    }
                } catch (...) {
                    parserLock.release(true) ;
                    throw ;
                }

               news += value.str->str;
           }
       } else if (ch == '<') {        // <%...%> is replaced by the output from the code
           ch = istr->get();
           if (ch == '<') {         // <<% is escaped (replaced verbatim)
               news += "<<";
           } else if (ch != '%') {
               news += std::string ("<") + ch;
           } else {
               bool isexpr = false;
               ch = istr->get();
               if (ch == '=') {
                  // <%= is an expression
                  isexpr = true;
               } 
               // ch is first character of code
               std::string code;
               code += ch;
               while (!istr->eof()) {
                   ch = istr->get();
                   if (ch == '%') {
                       ch = istr->get();
                       if (ch == '>') {
                           break;
                       } else {
                           code += std::string("%") + ch;
                       }
                   } else {
                       code += ch;
                   }
               }
               if (istr->eof()) {
                   vm->runtimeError ("Missing close %>") ;
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
                string filename (vm->getIR()->source->filename) ;

                parserLock.acquire(true) ;
                StaticLink newslink (stack->slink, stack) ;

                Block *parentBlock = stack->block ;
                int level = parentBlock->level + 1 ;

                Block *block = new Class (b, "<inline>", level, parentBlock) ;
                block->stacksize = 1 ;      // room for this

                Variable *thisvar = new Variable ("this", 0) ;
                thisvar->setAccess (PROTECTED) ;
                block->variables["this"] = thisvar ;

                block->setParentBlock (parentBlock) ;

                // create a temp stack frame so that variables can be created in it.  The real stack
                // frame will be an object allocated to the appropriate size when the number
                // of variables is known
                StackFrame *currframe = new (1) StackFrame (&newslink, stack, NULL, block, 1, true) ;

                b->currentStack = currframe ;    /// parentStack ;       // we want variables to be created in object,
                b->currentMajorScope = block ;
                b->currentScope = block ;
                b->currentScopeLevel = block->level ;
                b->currentClass = (Class*)block ;

                Context ctx (stream, filename, vm->getIR()->source->lineno, b->currentContext) ;
                b->currentContext = &ctx ;

                Node *tree ;

                try {
                    b->importedTree = NULL ;
                    tree = b->parseStream (stream) ;

                    block->body = new Node (b, SEMICOLON, b->importedTree, tree) ;
                    block->code = b->codegen->generate (static_cast<InterpretedBlock*>(block)) ;

                    b->currentContext = ctx.getPrevious() ;

                    b->currentStack = NULL ;
                } catch (...) {
                    parserLock.release(true) ;
                    throw ;
                }
                parserLock.release(true) ;

                Object *obj = new (block->stacksize) Object (block, &newslink, stack, &block->code->code[0]) ;
                obj->ref++ ;
                obj->varstack[0] = Value (obj) ;

                VirtualMachine newvm (b);
                newvm.output = new StdOutStream (&outstream) ;
                newvm.output.stream->setAutoFlush (true) ;

                newvm.execute (static_cast<InterpretedBlock*>(block), obj, &newslink, 0) ;

                news += outstream.str();
           }
       } else {
           if (!istr->eof()) {
               news += ch;
           }
       }
   }
   return new string (news);

}



}		// end of extern "C"


AIKIDO_NATIVE(monitorWait) {
    if (paras[0].type != T_OBJECT) {
        throw newParameterException (vm, stack, "wait", "Illegal monitor") ;
    }
    Monitor *mon = (Monitor*)paras[0].object ;
    mon->wait() ;
    return 0 ;
}

AIKIDO_NATIVE(monitorTimewait) {
    if (paras[0].type != T_OBJECT) {
        throw newParameterException (vm, stack, "timedwait", "Illegal monitor") ;
    }
    Monitor *mon = (Monitor*)paras[0].object ;
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "timedwait", "Illegal wait value") ;
    }
    return mon->timewait(getInt (paras[1])) ;
}

AIKIDO_NATIVE(monitorNotify) {
    if (paras[0].type != T_OBJECT) {
        throw newParameterException (vm, stack, "notify", "Illegal monitor") ;
    }
    Monitor *mon = (Monitor*)paras[0].object ;
    mon->notify() ;
    return 0 ;
}

AIKIDO_NATIVE(monitorNotifyAll) {
    if (paras[0].type != T_OBJECT) {
        throw newParameterException (vm, stack, "notifyAll", "Illegal monitor") ;
    }
    Monitor *mon = (Monitor*)paras[0].object ;
    mon->notifyAll() ;
    return 0 ;
}

AIKIDO_NATIVE(setThreadPriority) {
    if (!isIntegral (paras[0])) {
        throw newParameterException (vm, stack, "setPriority", "Illegal priority") ;
    }
    int pri = getInt (paras[0]) ;
    if (pri < 0 || pri > 99) {
        throw newParameterException (vm, stack, "setPriority", "Illegal priority") ;
    }
    OSThread *thr = b->findThread (OSThread::self()) ;
    if (thr != NULL) {
        thr->setPriority (toOSPriority (pri)) ;
    }
    return 0 ;
}

AIKIDO_NATIVE(getThreadPriority) {
    OSThread *thr = b->findThread (OSThread::self()) ;
    if (thr != NULL) {
        return Value (fromOSPriority (thr->getPriority ())) ;
    }
    return 0 ;
}

AIKIDO_NATIVE(getThreadID) {
    OSThread_t self = OSThread::self() ;
    return Value ((INTEGER)self) ;
}

AIKIDO_NATIVE(threadJoin) {
    if (paras[0].type != T_STREAM) {
        throw newParameterException (vm, stack, "join", "Illegal thread") ;
    }
    OSThread_t tid = paras[0].stream->getThreadID() ;
    OSThread *thr = b->findThread (tid) ;
    if (thr != NULL) {
        thr->join() ;
    }
    return 0 ;
}


}


#if defined(_OS_SOLARIS)
#include "unix/osnative.cc"		// OS specific native functions
#elif defined(_OS_LINUX)
#include "unix/osnative.cc"		// OS specific native functions
#elif defined(_OS_MACOSX)
#include "unix/osnative.cc"		// OS specific native functions
#elif defined (_OS_WINDOWS)
#include "win32/osnative.cc"
#else
#error "Unknown OS for native package"
#endif



