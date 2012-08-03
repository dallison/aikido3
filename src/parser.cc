/*
 * parser.cc
 *
 * Aikido Language System,
 * export version: 1.00
 * Copyright (c) 2002-2003 Sun Microsystems, Inc. 2003
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
 * Contributor(s): dallison
 *
 * Version:  1.79
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */

// CHANGE LOG
// 1/21/2004 	David Allison of PathScale Inc.
//		Added object code loading and dumping
//		Added insignalhandler variable
//
// 6/5/2007   David Allison of Xsigo Systems Inc.
//      Various bug fixes
//      Added preloaded programs
//      Added auto vectors for assignment and value (you can omit the [])
//      Added yield reserved word



// this is the binary for the aikido.zip file genetated by xxd
extern "C" {
   extern unsigned char bin_aikido_zip[];
   extern int bin_aikido_zip_len;
}

#if defined(_OS_MACOSX)
#include <sys/stat.h>		// for some reason this needs to be before aikido.h
#endif

#include "aikido.h"
#include <ctype.h>
#include "string.h"
#include <stdlib.h>
#include <time.h>
#if !defined(_CC_GCC)
#include <fcntl.h>
#endif

#include <new>

#include <signal.h>
#if !defined(_OS_MACOSX)
#include <sys/stat.h>
#endif



#include "aikidocodegen.h"
#include "aikidointerpret.h"
#include "aikidodebug.h"

#ifdef JAVA
#include "java/vm.h"
#endif

namespace aikido {

bool afterinit = false ;		// flag to say we have initialized the aikido object

extern Object *parserDelegate;

#ifdef GCDEBUG

ObjectRegistry objectRegistry ;
bool registryon ;

extern "C" {
void recordObjects(bool v) {
    registryon = v ;
}

}


#endif

extern Monitor parserLock ;          // in native.cc

int verbosegc ;
int nogc ;
bool interrupt ;
bool noaccesscheck ;		// don't check access (for debugger)
bool readingsystem ;		// currently reading system (for debugger)
bool localimport ;          // import into local scope

std::string programname ;

// current signal handlers
Closure *signals[MAXSIGS] ;
int signalnum = -1 ;
bool insignalhandler = false ;

// the heapLock is a mutex that protects the reference counts in heap
// objects
OSMutex heapLock ;

// number of active threads in the program
int numThreads ;

bool interrupted;

// called when ^C is hit
void interruptHit (int v) {
    interrupt = true ;
}

// registry of filename/lineno objects
SourceRegistry sourceRegistry ;

// for debugging with dbx, do the following:
// 
// in your program: 
//
//  native breakpoint (s)
//
// then when you want to stop:
//    breakpoint(reason)
//
// in dbx:
// (dbx) stop in breakpoint

extern "C" void breakpoint(char *s) {
    std::cout << "Breakpoint hit: " << s << "\n" ;
}

void outOfMemory() {
    std::cerr << "Runtime error: out of memory\n" ;
    exit(1) ;
}

Aikido::Aikido (std::string objfile, std::string dir, std::string programnm, 
                std::istream &cin, std::ostream &cout, std::ostream &cerr,
                int props, int nincs, char **incs) : properties(props), directory(dir), mainos(cout) {
    init() ;
    programname = programnm ;
    mystdout = new StdOutStream (&cout) ;
    mystdin = new StdInStream (&cin) ;
    mystderr = new StdOutStream (&cerr) ;

    interrupted = false ;

    // make the import paths vector
    importpaths.push_back (directory) ;		// search run directory first

    // add paths specified on the command line
    for (int i = 0 ; i < nincs ; i++) {
        string s (incs[i] + 2) ;			// remove the -I
        importpaths.push_back (s) ;
    }

    // if we are running from a bin directory, replace it with /lib
    std::string::size_type bin = directory.find ("/bin") ;
    if (bin != std::string::npos) {
        string lib = directory ;
        if (sizeof(long) == 8) {
            string lib64 = directory ;
            lib64.replace (bin, 4, "/lib64") ;
            importpaths.push_back (lib64) ;
        }
        lib.replace (bin, 4, "/lib") ;
        importpaths.push_back (lib) ;
    }
    char *path = getenv ("AIKIDOPATH") ;
    if (path == NULL) {
        path = getenv ("DARWINPATH") ;          // legacy
    }
    while (path != NULL && *path != 0) {
        char *s = path ;
        string p ;
        while (*s != 0 && *s != ':') {
            p += *s++ ;
        }
        importpaths.push_back (p) ;
        if (*s == ':') {
            s++ ;
        }
        path = s ;
    }

#if !defined(_OS_WINDOWS)
    // add LD_LIBRARY_PATH paths so that we can find .so files easier
    path = getenv ("LD_LIBRARY_PATH") ;
    if (path != NULL) {
        while (path != NULL && *path != 0) {
            char *s = path ;
            string p ;
            while (*s != 0 && *s != ':') {
                p += *s++ ;
            }
            importpaths.push_back (p) ;
            if (*s == ':') {
                s++ ;
            }
            path = s ;
        }
    }
    
    if (sizeof(long) == 8) {
        importpaths.push_back ("/usr/lib64") ;		// always look in /usr/lib64
    } else {
        importpaths.push_back ("/usr/lib") ;		// always look in /usr/lib
    }
#endif
#if defined (_OS_MACOSX)
    // for MAC OS X the /sw/lib is a common directory for locating extension
    // libraries.  If it exists, add it to the importpaths.
    struct stat st ;
    if (stat ("/sw/lib", &st) == 0) {
        importpaths.push_back ("/sw/lib") ;
    }
#endif

    importpaths.push_back (".") ;

    loadObjectCode (objfile) ;		//XXX: catch exceptions
    mainPackage = (Package*)scopevec[0] ;		// first scope is main

    currentMajorScope = mainPackage ;
    pushScope (mainPackage) ;

}


// construtor for main aikido object
Aikido::Aikido (std::string dir, std::string programnm, 
                int argc, char **argv, 
                std::istream &cin, std::ostream &cout, std::ostream &cerr,
                int props, int nincs, char **incs) : 
                   directory (dir), properties (props), mainos(cout) {
    init() ;
    programname = programnm ;

    // main tree
    Node *tree = NULL ;
    // current block level
    blockLevel = 0 ;

    // create main package
    mainPackage = new Package (this, "main", 0, NULL) ;

    importedTree = NULL ;
    interrupted = false ;

    // create this variable and add to main
    Variable *thisvar = new Variable ("this", 0) ;
    thisvar->setAccess (PROTECTED) ;
    mainPackage->variables["this"] = thisvar ;
    mainPackage->stacksize++ ;
    thisvar->oldValue.ptr = (void*)mainPackage ;

    // initialize some vars
    currentMajorScope = mainPackage ;
    pushScope (mainPackage) ;

    // create the program arguments vector
    Parameter *args = addParameter ("args") ;
    args->setAccess (PUBLIC) ;
    mainPackage->parameters.push_back (args) ;

    // if we are compiling, the args are passed at the runtime, not compile time
    if (!(properties & COMPILE)) {
        std::vector<Node*> argvec (argc) ;
        argvec.clear() ;
        for (int i = 0 ; i < argc ; i++) {
            Node *node = new Node (this, STRING, Value (new string (argv[i]))) ;
            argvec.push_back (node) ;
        }
    
        // add args vector to main
        Node *argvalue = new Node (this, VECTOR, argvec) ;
        Node *argvar = new Node (this, IDENTIFIER, args) ;
        argvar->value.integer = 0 ;				// static link level is main
        tree = new Node (this, CONSTASSIGN, argvar, argvalue) ;
    }


    // add the system variables
    tree = new Node (this,SEMICOLON, tree, addSystemVar ("main", PACKAGE, Value (mainPackage))) ;
    tree = new Node (this,SEMICOLON, tree, addNativeFunction ("sleep", 1, Aikido__threadSleep)) ;
    tree = new Node (this,SEMICOLON, tree, addNativeFunction ("setPriority", 1, Aikido__setThreadPriority)) ;
    tree = new Node (this,SEMICOLON, tree, addNativeFunction ("getPriority", 0, Aikido__getThreadPriority)) ;
    tree = new Node (this,SEMICOLON, tree, addNativeFunction ("getID", 0, Aikido__getThreadID)) ;
    tree = new Node (this,SEMICOLON, tree, addNativeFunction ("join", 1, Aikido__threadJoin)) ;
    
    // these are local parameters, not std:: globals
    mystdout = new StdOutStream (&cout) ;
    mystdin = new StdInStream (&cin) ;
    mystderr = new StdOutStream (&cerr) ;

    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("stdout", STREAM, Value ((Stream*)mystdout))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("stdin", STREAM, Value ((Stream*)mystdin))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("stderr", STREAM, Value ((Stream*)mystderr))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("version", NUMBER, Value (version_number))) ;

    // the main input and output streams
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("output", STREAM, Value (), false)) ;  // type is none
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("input", STREAM, Value (), false)) ;    // type is none

    // make a variable for the builtin types
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("int", NUMBER, Value ((INTEGER)0))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("integer", NUMBER, Value ((INTEGER)0))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("bool", NUMBER, Value (false))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("boolean", NUMBER, Value (false))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("byte", NUMBER, Value ((unsigned char)0))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("char", CHAR, Value ('a'))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("string", STRING, Value (""))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("vector", VECTOR, Value (new Value::vector))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("bytevector", VECTOR, Value (new Value::bytevector))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("map", MAP, Value (new map))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("stream", STREAM, Value ((Stream*)mystdout))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("real", FNUMBER, Value (0.0))) ;
    tree = new Node (this,SEMICOLON, tree, addFixedTypeVar ("object", NUMBER, Value ((Object*)NULL))) ;
    tree = new Node (this,SEMICOLON, tree, addSystemVar ("none", NUMBER, Value ())) ;

    // now we are in private mode
    currentAccessMode = PRIVATE ;

    // make the import paths vector
    importpaths.push_back (directory) ;		// search run directory first

    // add paths specified on the command line
    for (int i = 0 ; i < nincs ; i++) {
        string s (incs[i] + 2) ;			// remove the -I
        importpaths.push_back (s) ;
    }

    // if we are running from a bin directory, replace it with /lib
    std::string::size_type bin = directory.find ("/bin") ;
    if (bin != std::string::npos) {
        string lib = directory ;
        if (sizeof(long) == 8) {
            string lib64 = directory ;
            lib64.replace (bin, 4, "/lib64") ;
            importpaths.push_back (lib64) ;
        }
        lib.replace (bin, 4, "/lib") ;
        importpaths.push_back (lib) ;
    }
    char *path = getenv ("AIKIDOPATH") ;
    if (path == NULL) {
        path = getenv ("DARWINPATH") ;          // legacy
    }
    while (path != NULL && *path != 0) {
        char *s = path ;
        string p ;
        while (*s != 0 && *s != ':') {
            p += *s++ ;
        }
        importpaths.push_back (p) ;
        if (*s == ':') {
            s++ ;
        }
        path = s ;
    }

#if !defined(_OS_WINDOWS)
    // add LD_LIBRARY_PATH paths so that we can find .so files easier
    path = getenv ("LD_LIBRARY_PATH") ;
    if (path != NULL) {
        while (path != NULL && *path != 0) {
            char *s = path ;
            string p ;
            while (*s != 0 && *s != ':') {
                p += *s++ ;
            }
            importpaths.push_back (p) ;
            if (*s == ':') {
                s++ ;
            }
            path = s ;
        }
    }
    if (sizeof(long) == 8) {
        importpaths.push_back ("/usr/lib64") ;		// always look in /usr/lib64
    } else {
        importpaths.push_back ("/usr/lib") ;		// always look in /usr/lib
    }

#endif
#if defined (_OS_MACOSX)
    // for MAC OS X the /sw/lib is a common directory for locating extension
    // libraries.  If it exists, add it to the importpaths.
    struct stat st ;
    if (stat ("/sw/lib", &st) == 0) {
        importpaths.push_back ("/sw/lib") ;
    }
#endif

    importpaths.push_back (".") ;
#ifdef INSTALLDIR
    path = (char*)INSTALLDIR ;
    while (path != NULL && *path != 0) {
        char *s = path ;
        string p ;
        while (*s != 0 && *s != ':') {
            p += *s++ ;
        }
        importpaths.push_back (p) ;
        if (*s == ':') {
            s++ ;
        }
        path = s ;
    }

#endif

    // look for aikido.zip
    findSystemZip() ;

    // import system.aikido
    readingsystem = true ;
    tree = new Node (this,SEMICOLON, tree, importSystem()) ;

    // add the imported tree now,  This is any imports from system.aikido
    mainPackage->body = new Node (this,SEMICOLON, mainPackage->body, importedTree) ;

    mainPackage->body = new Node (this,SEMICOLON, mainPackage->body, tree) ;

    readingsystem = false ;
    importedTree = NULL ;
   
    program = NULL ;

#ifdef JAVA
    // create the Java Objects
    javaEnabled = java::init(this) ;
#endif

}

// maybe should do something here someday
Aikido::~Aikido() {
}

void Aikido::init() {
    currentContext = NULL ;
    currentStack = NULL ;
    numErrors = 0 ;
    numRuntimeErrors = 0 ;
    maxErrors = MAXERRORS ;
    nloops = nfuncs = nswitches = nbackticks = 0 ;
    worker = new LoopbackWorker (this);
    linebased = false ;
    interrupt = false ;
    pass = NULL ;
    dot = NULL ;
    nscopes = 0 ;
    afterinit = true ;
    loadpass = 1 ;
    currentClass = NULL ;
    currentFunction = NULL ;
    interrupted = false ;

    mainStack = NULL ;
    mainNum = 0 ;
    mainFound = false ;
    currentMajorScope = NULL ;
    currentScopeLevel = 0 ;
    currentScope = NULL ;

    // we can do nothing if new fails
    std::set_new_handler (outOfMemory) ;

    codegen = new codegen::CodeGenerator() ;		// code generator object

    // reserved words and tokens
    reservedWords ["macro"] = MACRO ;
    reservedWords ["if"] = IF ;
    reservedWords ["else"] = ELSE ;
    reservedWords ["elif"] = ELIF ;
    reservedWords ["foreach"] = FOREACH ;
    reservedWords ["while"] = WHILE ;
    reservedWords ["function"] = FUNCTION ;
    reservedWords ["switch"] = SWITCH ;
    reservedWords ["case"] = CASE ;
    reservedWords ["default"] = DEFAULT   ;
    reservedWords ["break"] = BREAK  ;
    reservedWords ["continue"] = CONTINUE ;
    reservedWords ["return"] = RETURN ;
    reservedWords ["yield"] = YIELD ;
    reservedWords["true"] = TTRUE ;
    reservedWords["false"] = TFALSE ;
    reservedWords["var"] = VAR ;
    reservedWords["sizeof"] = SIZEOF ;
    reservedWords["typeof"] = TYPEOF ;
    reservedWords["for"] = FOR ;
    reservedWords["thread"] = THREAD ;
    reservedWords["const"] = TCONST ;
    reservedWords["try"] = TRY ;
    reservedWords["catch"] = CATCH ;
    reservedWords["finally"] = FINALLY ;
    reservedWords["throw"] = THROW ;
    reservedWords["class"] = CLASS ;
    reservedWords["new"] = NEW ;
    reservedWords["delete"] = TDELETE ;
    reservedWords["import"] = IMPORT ;
    reservedWords["package"] = PACKAGE ;
    reservedWords["public"] = PUBLIC ;
    reservedWords["private"] = PRIVATE ;
    reservedWords["protected"] = PROTECTED ;
    reservedWords["enum"] = ENUM ;
    reservedWords["static"] = STATIC ;
    reservedWords["using"] = USING ;
    reservedWords["extend"] = EXTEND ;
    reservedWords["operator"] = OPERATOR ;
    reservedWords["generic"] = GENERIC ;
    reservedWords["null"] = KNULL ;
    reservedWords["native"] = NATIVE ;
    reservedWords["monitor"] = MONITOR ;
    reservedWords["extends"] = EXTENDS ;
    reservedWords["cast"] = CAST ;
    reservedWords["interface"] = TINTERFACE ;
    reservedWords["implements"] = IMPLEMENTS ;
    reservedWords["do"] = DO ;
    reservedWords["instanceof"] = INSTANCEOF ;
    reservedWords["synchronized"] = SYNCHRONIZED ;
    reservedWords["in"] = TIN ;


    addToken ("..", ELLIPSIS) ;
    addToken ("...", ELLIPSIS) ;
    addToken ("@", ANNOTATE) ;
    addToken ("*", STAR) ;
    addToken (")", RBRACK) ;
    addToken ("(", LBRACK) ;
    addToken (".", DOT) ;
    addToken ("[", LSQUARE) ;
    addToken ("]", RSQUARE) ;
    addToken ("+", PLUS) ;
    addToken ("-", MINUS) ;
    addToken ("/", SLASH) ;
    addToken ("%", PERCENT) ;
    addToken ("?", QUESTION) ;
    addToken (":", COLON) ;
    addToken (",", COMMA) ;
    addToken ("{", LBRACE) ;
    addToken ("}", RBRACE) ;
    addToken ("~", TILDE) ;
    addToken ("^", CARET) ;
    addToken ("!", BANG) ;
    addToken ("&", AMPERSAND) ;
    addToken ("|", BITOR) ;
    addToken ("&&", LOGAND) ;
    addToken ("||", LOGOR) ;
    addToken ("<<", LSHIFT) ;
    addToken (">>", RSHIFT) ;
    addToken (">>>", ZRSHIFT) ;
    addToken ("<", LESS) ;
    addToken (">", GREATER) ;
    addToken ("<=", LESSEQ) ;
    addToken (">=", GREATEREQ) ;
    addToken ("==", EQUAL) ;
    addToken ("!=", NOTEQ) ;
    addToken ("++", PLUSPLUS) ;
    addToken ("--", MINUSMINUS) ;
    addToken ("=", ASSIGN) ;
    addToken ("+=", PLUSEQ) ;
    addToken ("-=", MINUSEQ) ;
    addToken ("*=", STAREQ) ;
    addToken ("/=", SLASHEQ) ;
    addToken ("%=", PERCENTEQ) ;
    addToken ("<<=", LSHIFTEQ) ;
    addToken (">>=", RSHIFTEQ) ;
    addToken (">>>=", ZRSHIFTEQ) ;
    addToken ("&=", ANDEQ) ;
    addToken ("|=", OREQ) ;
    addToken ("^=", XOREQ) ;
    addToken (";", SEMICOLON) ;
    addToken ("`", BACKTICK) ;
    addToken ("->", ARROW) ;

    // map of operator name vs number of parameters
    opinfo["operator[]"] = 3 ;
    opinfo["operator sizeof"] = 1 ;
    opinfo["operator typeof"] = 1 ;
    opinfo["operator cast"] = 2 ;
    opinfo["operator*"] = 2 ;
    opinfo["operator+"] = 2 ;
    opinfo["operator-"] = 2 ;
    opinfo["operator/"] = 2 ;
    opinfo["operator%"] = 2 ;
    opinfo["operator~"] = 1 ;
    opinfo["operator!"] = 1 ;
    opinfo["operator^"] = 2 ;
    opinfo["operator&"] = 2 ;
    opinfo["operator|"] = 2 ;
    opinfo["operator<<"] = 2 ;
    opinfo["operator>>"] = 2 ;
    opinfo["operator>>>"] = 2 ;
    opinfo["operator<"] = 2 ;
    opinfo["operator>"] = 2 ;
    opinfo["operator>="] = 2 ;
    opinfo["operator<="] = 2 ;
    opinfo["operator=="] = 2 ;
    opinfo["operator!="] = 2 ;
    opinfo["operator->"] = 3 ;

    // initialize the operating system dependent stuff
    OS::init() ;

    // current default access mode is public
    currentAccessMode = PUBLIC ;

    // create debugger
    debugger = new Debugger (this, properties & DEBUG) ;
    if (properties & DEBUG) {
        signal (SIGINT, interruptHit) ;
    }
    tracer = new Tracer (this, debugger, 0, std::cout) ;

}

// 
// set the worker (possibly an assembler)
//
void Aikido::setWorker (Worker *w) {
    delete worker;
    worker = w ;
    if (worker != NULL) {
        worker->initialize() ;
        if (pass == NULL) {
            pass = addVariable ("pass") ;
            dot = addVariable (".") ;
        }
    }
}

//
// add a native function to the main package
//

Node *Aikido::addNativeFunction (string name, int np, NativeFunction::FuncPtr p, int level, Scope *parentScope, Class *parentClass, int pmask, bool israw) {
    Node *node, *left, *right ;
    NativeFunction *f = NULL ;
    if (israw) {
        f = new RawNativeFunction (this, name, np, p, level, parentScope) ;
    } else {
        f = new NativeFunction (this, name, np, p, level, parentScope) ;
    }
    f->paramask = pmask ;
    f->parentClass = parentClass ;
    f->setParentBlock (currentMajorScope) ;
    Variable *v = addVariable (name) ;
    left = new Node (this,IDENTIFIER, v) ;
    right = new Node (this,FUNCTION, Value ((Function *)f)) ;
    node = new Node (this,CONSTASSIGN, left, right) ;
    return node ;
}

//
// add a system variable
//

Node *Aikido::addSystemVar (string name, Token t, const Value &v, bool isconst) {
    Node *node, *left, *right ;
    Variable *var = addVariable (name) ;
    if (isconst) {
        var->flags |= VAR_CONST ;
    }
    left = new Node (this,IDENTIFIER, var) ;
    right = new Node (this,t, v) ;
    node = new Node (this,CONSTASSIGN, left, right) ;
    return node ;
}

Node *Aikido::addFixedTypeVar (string name, Token t, const Value &v, bool isconst) {
    Node *node, *left, *right ;
    Variable *var = addVariable (name) ;
    if (isconst) {
        var->flags |= VAR_CONST ;
    }
    var->flags |= VAR_TYPEFIXED ;
    left = new Node (this,IDENTIFIER, var) ;
    right = new Node (this,t, v) ;
    node = new Node (this,CONSTASSIGN, left, right) ;
    return node ;
}


//
// find the aikido.zip file
//

void Aikido::findSystemZip() {
    systemZip = new zip::ZipFile (bin_aikido_zip, bin_aikido_zip_len);
#if 0

#if defined(_OS_WINDOWS)
    string sysfilename = "\\aikido.zip" ;
#else
    string sysfilename = "/aikido.zip" ;
#endif
    struct stat st ;
    systemZip = NULL ;
    
    // look in . first
    filename = "." + sysfilename ;
    if (OS::fileStats (filename.c_str(), &st)) {
        systemZip = new zip::ZipFile (filename.c_str()) ;
        return ;
    }

    // now look in install location
    filename = directory + sysfilename ;

    if (OS::fileStats (filename.c_str(), &st)) {
        systemZip = new zip::ZipFile (filename.c_str()) ;
        return ;
    }
#ifdef INSTALLDIR
    char *s = (char*)INSTALLDIR ;
    while (*s != 0) {
        filename = "" ;
        while (*s != 0 && *s != ':') {
            filename += *s++ ;
        }
        if (*s == ':') {
            s++ ;
        }
        filename += sysfilename ;
        if (OS::fileStats (filename.c_str(), &st)) {
            systemZip = new zip::ZipFile (filename.c_str()) ;
            return ;
        }
    }
#endif

#endif
}


static bool findSystem(string directory, string postfix, std::ifstream &in, string &filename) {
    string sysfilename = string ("/system") + postfix + ".aikido" ;
    
    filename = "." + sysfilename ;
    in.open (filename.c_str()) ;
    if (!in) {
        filename = directory + sysfilename ;
        in.open (filename.c_str()) ;
        if (!in) {
#ifdef INSTALLDIR
            char *s = (char*)INSTALLDIR ;
            while (*s != 0) {
                filename = "" ;
                while (*s != 0 && *s != ':') {
                    filename += *s++ ;
                }
                if (*s == ':') {
                    s++ ;
                }
                filename += sysfilename ;
                in.open (filename.c_str()) ;
                if (in) {
                    return true ;
                }
            }
#endif
        return false ;
        }
    }
    return true ;
}


//
// import the system file containing all the system data
//

Node *Aikido::importSystem() {
    char postfix[10] ;
    sprintf (postfix, "%d", version_number) ;
    string fn ;			

    std::ifstream in ;
    
    if (systemZip != NULL) {
        fn = "system.aikido" ;
        zip::ZipEntry *entry = systemZip->open (fn.str) ;
        if (entry != NULL) {
#if defined(_CC_GCC) && __GNUC__ == 2
            std::strstream *zs = entry->getStream() ;
#else
            std::stringstream *zs = entry->getStream() ;
#endif
            mainPackage->imports.insert (entry->uniqueId()) ;
            Context ctx (*zs, fn, 0, currentContext) ;
            debugger->registerFile ("zip:" + fn) ;
            currentContext = &ctx ;
            Node *node = parseStream (*zs) ;
            delete zs ;
            currentContext = ctx.getPrevious() ;
            return node ;
        }
    }


    if (!findSystem (directory, postfix, in, fn) && !findSystem (directory, "", in, fn)) {
        std::cerr << "*** Aikido fatal error: Unable to open system package\n" ;
        ::exit (1) ;
    }
    struct stat s ;
    OS::fileStats (fn.c_str(), &s) ;
    mainPackage->imports.insert (OS::uniqueId (&s)) ;
    Context ctx (in, fn, 0, currentContext) ;
    debugger->registerFile (fn) ;
    currentContext = &ctx ;
    Node *node = parseStream (in) ;
    in.close() ;
    currentContext = ctx.getPrevious() ;
    return node ;
}

//
// reset the input stream
//

void Aikido::resetInput() {
    ch = line ;
    *ch = 0 ;
    nextToken() ;
}


//
// can be called multiple times
//

void Aikido::parse(string file, std::istream &in) {
    filename = file ;
    debugger->registerFile (file) ;
    currentContext = new Context (in, filename, 0) ;
    program = new Node (this, SEMICOLON, program, parseStream (in)) ;
}


//
// parse a aikido program from a stream
//

Node *Aikido::parseStream (std::istream &in) {
	Node *node = NULL;
    resetInput() ;
    skipSpaces() ;
    try {
        node = parseStatementSequence (currentScope) ;
    } catch (char *s) {
        error ("Exception : %s", s) ;
    }
    return node ;
}

// called when any signal is received
void signalHit (int sig) {
    signalnum = sig ;
}

// generate code for all blocks

void Aikido::generate() {
    if (numErrors != 0) {
        return ;
    }
    // add the imports to the main package
    mainPackage->body = new Node (this, SEMICOLON, mainPackage->body, importedTree) ;
 
    // generate code sequence for main package
    preamble = codegen->generate (mainPackage->body) ;
 
    // generate code for whole program
    mainPackage->code = codegen->generate (program) ;
}

// execute the code in the input stream
// if an exception occurs, it is thrown to the caller
Value Aikido::execute (std::istream &in, Object *root, std::ostream *output, std::istream *input, bool setCurrentClass, bool add_return) {
    parserLock.acquire(true) ;
    Scope *oldscope = currentScope ;
    int oldsl = currentScopeLevel ;
    interrupted = false ;

    

    currentStack = root ;
    currentScope = root->block ;
    currentMajorScope = root->block ;
    currentScopeLevel = root->block->level ;
    if (setCurrentClass) {
        currentClass = dynamic_cast<Class*>(currentMajorScope) ;
    }
    int errors = numErrors ;

    string fn = "" ;
    Context ctx (in, fn, 0, currentContext) ;
    currentContext = &ctx ;
    Node *node = NULL ;
    try {
        node = parseStream (in) ;
        if (add_return) {
            node = new Node (this, RETURN, node) ;
        }
    } catch (Exception e) {
        currentContext = ctx.getPrevious() ;
        currentScope = oldscope ;
        currentScopeLevel = oldsl ;
        parserLock.release(true) ;
        throw ;
    }
    
    currentContext = ctx.getPrevious() ;
    Value v ;

    if (numErrors == errors && node != NULL) {
        codegen::CodeSequence *code = codegen->generate (node) ;
        parserLock.release(true) ;
        VirtualMachine newvm (this) ;
        if (output != NULL) {
            newvm.output = new StdOutStream (output) ;
            newvm.output.stream->setAutoFlush (true) ;
        }
        if (input != NULL) {
            newvm.input = new StdInStream (input) ;
        }
        try {
            newvm.execute (dynamic_cast<InterpretedBlock*>(root->block), code, root, &root->objslink, 0) ;
            v = newvm.retval ;
            parserLock.acquire(true);
        } catch (Exception e) {
            parserLock.acquire(true);
            delete code ;
            if (node != NULL) {
                deleteTree (node) ;
            }
            currentScope = oldscope ;
            currentScopeLevel = oldsl ;
            throw ;
        }

        delete code ;
    }
    if (node != NULL) {
        deleteTree (node) ;
    }
    currentScope = oldscope ;
    currentScopeLevel = oldsl ;
    parserLock.release(true) ;
    return v ;
}

// load aikido code from a file into an object and create the object
// load the file under main, the parent block will be main

Value Aikido::loadFile (std::string filename, std::vector<Value> &args, StackFrame *stack, StaticLink *slink, Scope *scope, int sl, std::string outfile, std::ostream *output, std::istream *input) {
    std::vector<std::string> filenames;
    filenames.push_back (filename);
    return loadFile (filenames, args, stack, slink, scope, sl, outfile, output, input);
}

Value Aikido::loadFile (std::vector<std::string> &filenames, std::vector<Value> &args, StackFrame *stack, StaticLink *slink, Scope *scope, int sl, std::string outfile, std::ostream *output, std::istream *input) {
    parserLock.acquire(true) ;
    Block *parentBlock = mainStack->block ;

    int level = parentBlock->level + 1 ;

    interrupted = false ;

    string name = "<loaded>";
    Block *block = new Class (this, name, level, parentBlock) ;
    block->stacksize = 2 ;              // room for this and args
    incRef (block, block) ;

    Variable *thisvar = new Variable ("this", 0) ;
    thisvar->setAccess (PROTECTED) ;
    block->variables["this"] = thisvar ;

    Variable *argsvar = new Variable ("args", 1) ;
    argsvar->setAccess (PROTECTED) ;
    block->variables["args"] = argsvar ;
    
    block->setParentBlock (parentBlock) ;

    currentStack = NULL ;           // this means to create variables in the current scope
    currentMajorScope = block ;
    currentScope = block ;
    currentScopeLevel = block->level ;
    currentClass = (Class*)block ;
    bool oldcache = cacheVariables ;

    for (unsigned int i = 0 ; i < filenames.size() ; i++) {
        std::string filename = filenames[i];

        std::ifstream in (filename.c_str()) ;
        if (!in) {
            parserLock.release(true) ;
            error ("Cannot open file %s", filename.c_str()) ;
            return 0 ;
        } 
        string ctxfile = filename ;
        Context ctx (in, ctxfile, 0, currentContext) ;
        currentContext = &ctx ;


        try {
            importedTree = NULL ;
            Node *tree = parseStream (in) ;

#if 0           // this doesn't work.  Let's ignore enums for now
            if (importedTree != NULL) {             // any imports?
                std::vector<Parameter*> paras ;
                codegen::Extension *x = codegen->generateExtension (importedTree, paras) ;      // generate code for them
                Object *mainobj = dynamic_cast<Object*>(mainStack) ;
                printf ("executing extension\n") ;
                vm->execute (x->code, mainStack, &mainobj->objslink, 0) ;                      // execute code in main stack
                printf ("done\n") ;
                deleteTree (importedTree) ;
                delete x ;
            }
#endif
            //block->body = new Node (this, SEMICOLON, importedTree, tree) ;
            block->body = new Node (this, SEMICOLON, block->body, tree) ;
        } catch (...) {
            currentContext = ctx.getPrevious() ;

            cacheVariables = oldcache ;
            parserLock.release(true) ;
            throw ;
        }

        currentContext = ctx.getPrevious() ;
        cacheVariables = oldcache ;
    }

    try {
        block->code = codegen->generate (static_cast<InterpretedBlock*>(block)) ;
    } catch (...) {
        parserLock.release(true) ;
        throw;
    }

    parserLock.release(true) ;

    // construct the args vector from the passed in args
    Value::vector *vec = new Value::vector () ;
    for (unsigned int i = 0 ; i < args.size() ; i++) {
        vec->push_back (args[i]) ;
    }

    Object *obj = new (block->stacksize) Object (block, &mainSLink, stack, &block->code->code[0]) ;
    obj->ref++ ;
    obj->varstack[0] = Value (obj) ;        // assign this 
    obj->varstack[1] = Value (vec) ;        // assign args

    VirtualMachine newvm (this) ;

    std::fstream *outstream = NULL ;
    if (outfile != "") {
        outstream = new std::fstream (outfile.c_str(), std::ios_base::out|std::ios_base::binary) ;
        if (!outstream->good()) {
            error ("Cannot open file %s for output", outfile.c_str()) ;
            decRef (block, block) ;
            if (decRef(obj, object) == 1) {
                if (verbosegc) {
                    std::cerr << "gc: deleting preload object " << (void*)obj << " (instance of " << obj->block->name << ")\n" ;
                }
                GCDELETE (obj, object) ;
            }
            
            return 0 ;
        }
        newvm.output = new StdStream (outstream) ;
    } else if (output != NULL) {
        newvm.output = new StdOutStream (output) ;
        newvm.output.stream->setAutoFlush (true) ;
    }
    if (input != NULL) {
        newvm.input = new StdInStream (input) ;
    }
    try {
        newvm.execute (static_cast<InterpretedBlock*>(block), obj, &mainSLink, 0) ;
    } catch (...) {
        decRef (block, block) ;
        if (decRef(obj, object) == 1) {
            if (verbosegc) {
                std::cerr << "gc: deleting preload object " << (void*)obj << " (instance of " << obj->block->name << ")\n" ;
            }
            GCDELETE (obj, object) ;
        }
        
        // overwriting vm->output will delete the fstream
        throw ;
    }
    obj->ref-- ;
    decRef (block, block) ;
    return obj ;

}


void Aikido::readFile (std::string filename, StackFrame *stack, StaticLink *slink, Scope *scope, int sl, std::string outfile, std::ostream *output, std::istream *input) {
    std::vector<std::string> filenames;
    filenames.push_back (filename);
    readFile(filenames, stack, slink, scope, sl, outfile, output, input);
}


void Aikido::readFile (std::vector<std::string> &filenames, StackFrame *stack, StaticLink *slink, Scope *scope, int sl, std::string outfile, std::ostream *output, std::istream *input) {
    parserLock.acquire(true) ;

    interrupted = false ;
    StackFrame *oldstack = currentStack ;
    currentStack = stack ;
    currentMajorScope = scope->majorScope ;
    currentScope = scope ;
    currentScopeLevel = currentMajorScope->level ;
    currentClass = NULL ;

    importedTree = NULL ;
    Node *tree = NULL ;

    for (unsigned int i = 0 ; i < filenames.size() ; i++) {
        std::string filename = filenames[i];

        std::ifstream in (filename.c_str()) ;
        if (!in) {
            parserLock.release(true) ;
            error ("Cannot open file %s", filename.c_str()) ;
            return ;
        } 
        string ctxfile = filename ;
        Context ctx (in, ctxfile, 0, currentContext) ;
        currentContext = &ctx ;

        try {
            Node *newtree = parseStream (in) ;
            tree = new Node (this, SEMICOLON, tree, newtree) ;
        } catch (...) {
            currentContext = ctx.getPrevious() ;
            currentStack = oldstack ;
            deleteTree (tree) ;
            parserLock.release(true) ;
            
            throw ;
        }
        currentContext = ctx.getPrevious() ;
    }

    currentStack = oldstack ;
    codegen::CodeSequence *code = NULL ;

    try {
        code = codegen->generate (tree) ;
    } catch (...) {
        parserLock.release(true) ;
        deleteTree (tree) ;
        delete code ;
        throw;
    }

    parserLock.release(true) ;

    VirtualMachine newvm (this) ;

    std::fstream *outstream = NULL ;
    if (outfile != "") {
        outstream = new std::fstream (outfile.c_str(), std::ios_base::out|std::ios_base::binary) ;
        if (!outstream->good()) {
            error ("Cannot open file %s for output", outfile.c_str()) ;
            return ;
        }
        newvm.output = new StdStream (outstream) ;
    } else if (output != NULL) {
        newvm.output = new StdOutStream (output) ;
        newvm.output.stream->setAutoFlush (true) ;
    }
    if (input != NULL) {
        newvm.input = new StdInStream (input) ;
    }
    try {
        newvm.execute ((InterpretedBlock*)scope, code, stack, slink, 0) ;
    } catch (...) {
        delete code ;
        deleteTree (tree) ;
        throw ;
    }
    delete code ;
    deleteTree (tree) ;
}


// preload just under main

Value Aikido::preload (std::string filename, StackFrame *stack, StaticLink *slink, Scope *scope, int sl) {
    parserLock.acquire(true) ;
    Block *parentBlock = mainStack->block ;

    int level = parentBlock->level + 1 ;

    interrupted = false ;

    string name = filename ;
    Block *block = new Class (this, name, level, parentBlock) ;
    block->stacksize = 2 ;              // room for this and args

    Variable *thisvar = new Variable ("this", 0) ;
    thisvar->setAccess (PROTECTED) ;
    block->variables["this"] = thisvar ;

    Variable *argsvar = new Variable ("args", 1) ;
    argsvar->setAccess (PROTECTED) ;
    block->variables["args"] = argsvar ;
    
    block->setParentBlock (parentBlock) ;

   

    currentStack = NULL ;           // this means to create variables in the current scope
    currentMajorScope = block ;
    currentScope = block ;
    currentScopeLevel = block->level ;
    currentClass = (Class*)block ;

    std::ifstream in (filename.c_str()) ;
    if (!in) {
        parserLock.release (true) ;
        error ("Cannot open file %s", filename.c_str()) ;
        return 0 ;
    } 
    string ctxfile = filename ;
    Context ctx (in, ctxfile, 0, currentContext) ;
    currentContext = &ctx ;

    Node *tree ;

    try {
        importedTree = NULL ;
        tree = parseStream (in) ;

#if 0           // this doesn't work.  Let's ignore enums for now
        if (importedTree != NULL) {             // any imports?
            std::vector<Parameter*> paras ;
            codegen::Extension *x = codegen->generateExtension (importedTree, paras) ;      // generate code for them
            Object *mainobj = dynamic_cast<Object*>(mainStack) ;
            printf ("executing extension\n") ;
            vm->execute (x->code, mainStack, &mainobj->objslink, 0) ;                      // execute code in main stack
            printf ("done\n") ;
            deleteTree (importedTree) ;
            delete x ;
        }
#endif
        //block->body = new Node (this, SEMICOLON, importedTree, tree) ;
        block->body = tree ;
        block->code = codegen->generate (static_cast<InterpretedBlock*>(block)) ;

    } catch (...) {
        currentContext = ctx.getPrevious() ;


        parserLock.release(true) ;
        throw ;
    }
    currentContext = ctx.getPrevious() ;
    parserLock.release(true) ;
    

    return (Class*)block ;
}

//
// preload a set of files into a block
//
Value Aikido::preload (std::vector<std::string> &filenames, StackFrame *stack, StaticLink *slink, Scope *scope, int sl) {
    parserLock.acquire(true) ;
    Block *parentBlock = mainStack->block ;

    int level = parentBlock->level + 1 ;

    interrupted = false ;

    string name = filename ;
    Block *block = new Class (this, name, level, parentBlock) ;
    block->stacksize = 2 ;              // room for this and args

    Variable *thisvar = new Variable ("this", 0) ;
    thisvar->setAccess (PROTECTED) ;
    block->variables["this"] = thisvar ;

    Variable *argsvar = new Variable ("args", 1) ;
    argsvar->setAccess (PROTECTED) ;
    block->variables["args"] = argsvar ;
    
    block->setParentBlock (parentBlock) ;

   

    currentStack = NULL ;           // this means to create variables in the current scope
    currentMajorScope = block ;
    currentScope = block ;
    currentScopeLevel = block->level ;
    currentClass = (Class*)block ;

    for (unsigned int i = 0 ; i < filenames.size() ; i++) {
        std::string filename = filenames[i];
        std::ifstream in (filename.c_str()) ;
        if (!in) {
            parserLock.release (true) ;
            error ("Cannot open file %s", filename.c_str()) ;
            return 0 ;
        } 
        string ctxfile = filename ;
        Context ctx (in, ctxfile, 0, currentContext) ;
        currentContext = &ctx ;

        Node *tree ;

        try {
            importedTree = NULL ;
            tree = parseStream (in) ;

            block->body = new Node (this, SEMICOLON, block->body, tree) ;
        } catch (...) {
            currentContext = ctx.getPrevious() ;

            parserLock.release(true) ;
            throw ;
        }
        currentContext = ctx.getPrevious() ;
    }

    block->code = codegen->generate (static_cast<InterpretedBlock*>(block)) ;
    parserLock.release(true) ;

    return (Class*)block ;
}

Value Aikido::executePreload (Block *block, std::vector<Value> &args, StackFrame *stack, StaticLink *slink, Scope *scope, int sl, std::string outfile, std::ostream *output, std::istream *input) {
    parserLock.acquire (true) ;

    // construct the args vector from the passed in args
    Value::vector *vec = new Value::vector () ;
    for (unsigned int i = 0 ; i < args.size() ; i++) {
        vec->push_back (args[i]) ;
    }

    Object *obj = new (block->stacksize) Object (block, &mainSLink, stack, &block->code->code[0]) ;
    obj->ref++ ;
    obj->varstack[0] = Value (obj) ;        // assign this 
    obj->varstack[1] = Value (vec) ;        // assign args

    VirtualMachine newvm (this) ;

    std::fstream *outstream = NULL ;
    if (outfile != "") {
        outstream = new std::fstream (outfile.c_str(), std::ios_base::out|std::ios_base::binary) ;
        if (!outstream->good()) {
            error ("Cannot open file %s for output", outfile.c_str()) ;
            if (decRef(obj, object) == 1) {
                if (verbosegc) {
                    std::cerr << "gc: deleting preload object " << (void*)obj << " (instance of " << obj->block->name << ")\n" ;
                }
                GCDELETE (obj, object) ;
            }
            return 0 ;
        }
        newvm.output = new StdStream (outstream) ;
    } else if (output != NULL) {
        newvm.output = new StdOutStream (output) ;
        newvm.output.stream->setAutoFlush (true) ;
    }

    if (input != NULL) {
        newvm.input = new StdInStream (input) ;
    }

    Context *oldctx = currentContext ;
    currentContext = NULL ;


    try {
        newvm.execute (static_cast<InterpretedBlock*>(block), obj, &mainSLink, 0) ;
    } catch (...) {
        if (decRef(obj, object) == 1) {
            if (verbosegc) {
                std::cerr << "gc: deleting preload object " << (void*)obj << " (instance of " << obj->block->name << ")\n" ;
            }
            GCDELETE (obj, object) ;
        }
        currentContext = oldctx ;
        throw ;
    }
    obj->ref-- ;
    currentContext = oldctx ;
    return obj ;

}


Value Aikido::call (Value func, StackFrame *frame, StaticLink *slink, int nargs, Value *args) {
    return vm->call (func.block, frame, slink, nargs, args) ;
}

Value Aikido::newObject (std::string classname, std::vector<Value> &args) {
    // find the class
    Tag *tag = findTag ("." + classname);
    if (tag == NULL) {
        return Value((Object*)NULL);
    }
    return vm->newObject (tag->block, mainStack, &mainSLink, args) ;
}

Value Aikido::newObject (std::string classname, StackFrame *frame, StaticLink *slink, std::vector<Value> &args) {
    // find the class
    Tag *tag = findTag ("." + classname);
    if (tag == NULL) {
        return Value((Object*)NULL);
    }
    return vm->newObject (tag->block, frame, slink, args) ;
}

// create a new variable in the given stack frame (or object)
void Aikido::createVariable (StackFrame *stack, std::string name, Value &value, Token access, bool isconst, bool isgeneric) {
    stack->checkCapacity() ;

    Variable *var = new Variable (name, stack->block->stacksize++) ;       // create the variable
    stack->block->insertVariable (var) ;
    addVariableName (name, stack->block, var) ;

    var->setValue (value, stack) ;              // set the value of the variable in the stack frame

    // now set up the access control
    var->setAccess (access) ;		
    if (isconst) {       
        var->flags |= VAR_CONST ;
    }
    if (isgeneric) {       
        var->flags |= VAR_GENERIC ;
    }
}



//
// main execution function.  Executes the main package
//

void Aikido::execute(int passes, int argc, char **argv) {
    // if any parser error we just stop here
    if (numErrors != 0) {
        return ;
    }

    if (argc == -1) {
        // register instructions with debugger 
        if (properties & DEBUG) {
            mainPackage->code->registerInstructions (debugger) ;
        }
    
        program = new Node (this,SEMICOLON, mainPackage->body, program) ;		// add program to main package
        debugger->registerBlock (mainPackage, program->source, mainPackage->code) ;

        deleteTree (program) ;
        program = NULL ;
        mainPackage->body = NULL ;
    }

    bool debugprompt = true ;
    localimport = true;         // XXX: let's try this
rerun:
    // create the main instance
    Object *mainobj = new (mainPackage->stacksize) Object (mainPackage, NULL, NULL, NULL) ;
    mainStack = mainobj ;
    mainobj->ref++ ;
    mainobj->varstack[0] = mainobj ;		// set the "this" pointer for main

    // set up a static link for the main stack
    mainSLink.frame = mainStack;
    mainSLink.next = &mainSLink;
    
    // declare the top level static link.  This must be allocated on the heap because this function
    // might return before further code is executed
    StaticLink *slink = new StaticLink (NULL, mainStack) ;
    slink->next = slink ;
    mainobj->slink = slink ;
    mainobj->objslink = *slink ;
    mainStack->ref++ ;                  // static link points to it

    // if we are running from an object file, assignt the arguments
    if (argc != -1) {
        // mainStack.varstack[1] is the args variable
        Value::vector *vec = new Value::vector() ;
        for (int i = 0 ; i < argc ; i++) {
            vec->push_back (new string (argv[i])) ;
        }
        mainStack->varstack[1] = vec ;
    }

    // create the Virtual Machine
    vm = new VirtualMachine (this) ;

    if (preamble != NULL) {
        // execute the preamble code
        try {
            vm->execute (mainPackage, preamble, mainStack, slink, 0) ;
        } catch (Exception e) {
            //std::cerr << "Runtime error: " << e.value << "\n" ;
            reportException (e) ;
            ::exit (1) ;
        } catch (const char *s) {
            std::cerr << "Internal error: " << s << '\n' ;
            ::exit (2) ;
        }
    }

    if (debugprompt && properties & DEBUG) {
        debugger->run (vm, NULL, NULL, 0, NULL, NULL) ;		// will return when run or step command issued
        if (vm->debugState == STEP || vm->debugState == NEXT) {
            debugger->stopInMain() ;
        }
    } 
    vm->debugState = RUN ;

    // look for a main function
    InterpretedBlock *main = findMain() ;

    // now run the program
    for (int i = 1 ; i <= passes ; i++) {
        if (pass != NULL) {
            pass->setValue (i, mainStack) ;
        }
        currentPass = i ;
        if (worker != NULL) {
            worker->preparePass (i) ;
        }
        if (numErrors == 0) {
            try {
                vm->execute (mainPackage, mainStack, slink, 0) ;
            } catch (RerunException e) {
                delete vm ;
                delete mainobj ;
                debugprompt = false ;
                goto rerun ;
            } catch (Exception e) {
                //std::cerr << "Runtime error: " << e.value << "\n" ;
                reportException (e) ;
                ::exit (1) ;
            } catch (const char *s) {
                std::cerr << "Internal error: " << s << '\n' ;
                ::exit (2) ;
            }
        }
        if (numErrors == 0 && worker != NULL) {
            worker->execute() ;
        }

        // if we had a main function at the top level, invoke it now
        if (main != NULL) {
            Operand tmp ;
            try {
                vm->randomCall (&tmp, main, mainStack, slink) ;		// invoke main
            } catch (RerunException e) {
                delete vm ;
                delete mainobj ;
                debugprompt = false ;
                goto rerun ;
            } catch (Exception e) {
                //std::cerr << "Runtime error: " << e.value << "\n" ;
                reportException (e) ;
                ::exit (1) ;
            } catch (const char *s) {
                std::cerr << "Internal error: " << s << '\n' ;
                ::exit (2) ;
            }
        }
    }
}

// call a random member function of an object
Value Aikido::call (Object *object, std::string func, std::vector<Value> &args) {
    currentContext = NULL ;

    Scope *scope ;
    Variable *var = object->block->findVariable (func, scope, aikido::VAR_PUBLIC, NULL, NULL) ;
    if (var == NULL) {
        error ("No such function %s in this object", func.c_str()) ;
    }
    Value &v = var->getValue (object) ;
    if (v.type != T_FUNCTION) {
        error ("Cannot call %s: not a function", func.c_str()) ;
    }
    // now that we have the function pointer, set up the environment and make the call
    Operand tmp ;
    Value thisptr (object) ;
    object->dlink = NULL ;
    vm->randomCall (&tmp, v.func, object, &object->objslink, thisptr, args) ;
    return tmp.val ;
}

// call a random member function of an object
Value Aikido::call (Object *object, std::string func, std::vector<Value> &args, std::ostream *output, std::istream *input) {
    currentContext = NULL ;

    Scope *scope ;
    Variable *var = object->block->findVariable (func, scope, aikido::VAR_PUBLIC, NULL, NULL) ;
    if (var == NULL) {
        error ("No such function %s in this object", func.c_str()) ;
    }
    Value &v = var->getValue (object) ;
    if (v.type != T_FUNCTION) {
        error ("Cannot call %s: not a function", func.c_str()) ;
    }
    // now that we have the function pointer, set up the environment and make the call
    Operand tmp ;
    Value thisptr (object) ;
    object->dlink = NULL ;

    VirtualMachine newvm (this);
    if (output != NULL) {
        newvm.output = new StdOutStream (output) ;
        newvm.output.stream->setAutoFlush (true) ;
    }

    if (input != NULL) {
        newvm.input = new StdInStream (input) ;
    }

    newvm.randomCall (&tmp, v.func, object, &object->objslink, thisptr, args) ;
    return tmp.val ;
}

// call a random member function of an object
Value Aikido::call (Object *object, Function *func, std::vector<Value> &args) {
    Operand tmp ;
    Value thisptr (object) ;
    object->dlink = NULL ;
    vm->randomCall (&tmp, func, object, &object->objslink, thisptr, args) ;
    return tmp.val ;
}

// call a random member function of an object
Value Aikido::call (Object *object, Function *func, std::vector<Value> &args, std::ostream *output, std::istream *input) {
    Operand tmp ;
    Value thisptr (object) ;
    object->dlink = NULL ;
    VirtualMachine newvm (this);
    if (output != NULL) {
        newvm.output = new StdOutStream (output) ;
        newvm.output.stream->setAutoFlush (true) ;
    }

    if (input != NULL) {
        newvm.input = new StdInStream (input) ;
    }

    newvm.randomCall (&tmp, func, object, &object->objslink, thisptr, args) ;
    return tmp.val ;
}

// call a random function in a closure
Value Aikido::call (Closure *closure, std::vector<Value> &args) {
    Operand tmp ;
    Value thisptr ((Object*)NULL) ;
    vm->randomCall (&tmp, dynamic_cast<Function*>(closure->block), closure->slink->frame, closure->slink, thisptr, args, true) ;
    return tmp.val ;
}

#ifdef GCDEBUG
inline void printObjects() {
    int n = 0 ;
    ObjectRegistry::iterator i = objectRegistry.begin() ;
    for ( ; i != objectRegistry.end() ; i++, n++) {
        std::cout << n << ". Object " << i->obj << " type: " << i->type << " " ;
        switch (i->type) {
        case T_STRING:
            std::cout << ((string*)i->obj)->str ;
            break ;
        case T_OBJECT:
            std::cout << " instance of " << ((Object*)i->obj)->block->name ;
            break ;
        case T_FUNCTION:
        case T_CLASS:
        case T_THREAD:
        case T_MONITOR:
        case T_ENUM:
            std::cout << " block of type " << ((Block*)i->obj)->name ;
            break ;
        }
        std::cout << "\n" ;
    }
}
#endif

// end of run, tidy up
void Aikido::finalize() {
    if (numErrors == 0 && worker != NULL) {
        worker->finalize() ;
    }
#ifdef GCDEBUG
    printObjects() ;
#endif
    //while (thr_join (NULL, NULL, NULL) == 0) ;		// wait for all threads

    //ThreadMap::iterator thread ;
    //for (thread = threads.begin() ; thread != threads.end() ; thread++) {
    //}
}

void Aikido::print(std::ostream &os) {
    if (numErrors == 0 && worker != NULL) {
        worker->print (os) ;
    }
}

void Exception::fillStackTrace(SourceLocation *src, Block *blk) {
    StackFrame *f = stack ;
    stacktrace.resize (10) ;
    stacktrace.clear() ;
    if (src != NULL) {
        // first location is passed as parameters
        stacktrace.push_back (ExceptionFrame (src, blk)) ;
    }
    int i = 0 ;
    while (i++ < 100 && f != NULL && f->inst != NULL && f->dlink != NULL && f->block != NULL) {
        stacktrace.push_back (ExceptionFrame (f->inst->source, f->dlink->block)) ;
        f = f->dlink ;                  // follow dynamic link to next frame
    }
}


// 
// report an exception
//
void Aikido::reportException (Exception &e) {
    if (properties & CLI) {
    } else {
        std::cerr << "Runtime error: " ;
    }
    switch (e.value.type) {
    case T_OBJECT: {
        if (e.value.object == NULL) {
            std::cerr << "null" ;
            break ;
        }
        Value stream (mystderr) ;
        InterpretedBlock *opfunc ;
        
        opfunc = vm->checkForOperator (e.value, ARROW) ;
        if (opfunc != NULL) {
            StackFrame *stack = mainStack ; // e.stack == NULL ? mainStack : e.stack ;
            StaticLink sl (NULL, mainStack) ;
            StaticLink *staticLink = e.staticLink == NULL ? &sl : e.staticLink ;
            Operand result ;
            try {
                vm->randomCall (&result, opfunc, stack, staticLink, e.value.object, stream, Value(false)) ;
            } catch (...) {
                std::cerr << "Exception while in an exception\n" ;
            }
        } else {
            std::cerr << "indescribable object" ;
        }
        break ;
        }
    default:
        std::cerr << e.value ;
        break ;
    }
    std::cerr << '\n' ;

    if (properties & TRACEBACK) {
        for (int i = 0 ; i < e.stacktrace.size() ; i++) {
            ExceptionFrame &f = e.stacktrace[i] ;
            std::cerr << "        at " ;
            if (f.block == NULL) {
                std::cerr << "<unknown>(" ;
            } else {
                std::cerr << f.block->fullname << "(";
            }
            if (f.source != NULL) {
                std::cerr << f.source->filename << ":" << f.source->lineno << ")\n" ;
            } else {
                std::cerr << "<unknown:unknown>)\n" ;
            }
        }
    }
}


void Aikido::reportException (Exception &e, std::ostream &os) {
    if (properties & CLI) {
    } else {
        os << "Runtime error: " ;
    }
    switch (e.value.type) {
    case T_OBJECT: {
        if (e.value.object == NULL) {
            os << "null" ;
            break ;
        }
        Value stream (new StdOutStream(&os)) ;
        InterpretedBlock *opfunc ;
        
        opfunc = vm->checkForOperator (e.value, ARROW) ;
        if (opfunc != NULL) {
            StackFrame *stack = mainStack ; // e.stack == NULL ? mainStack : e.stack ;
            StaticLink sl (NULL, mainStack) ;
            StaticLink *staticLink = e.staticLink == NULL ? &sl : e.staticLink ;
            Operand result ;
            try {
                vm->randomCall (&result, opfunc, stack, staticLink, e.value.object, stream, Value(false)) ;
            } catch (...) {
                os << "Exception while in an exception\n" ;
            }
        } else {
            os << "indescribable object" ;
        }
        break ;
        }
    default:
        os << e.value ;
        break ;
    }
    os << '\n' ;

    if (properties & TRACEBACK) {
        for (int i = 0 ; i < e.stacktrace.size() ; i++) {
            ExceptionFrame &f = e.stacktrace[i] ;
            os << "        at " ;
            if (f.block == NULL) {
                os << "<unknown>(" ;
            } else {
                os << f.block->fullname << "(";
            }
            if (f.source != NULL) {
                os << f.source->filename << ":" << f.source->lineno << ")\n" ;
            } else {
                os << "<unknown:unknown>)\n" ;
            }
        }
    }
}



//
// lexical analyser tree building.  The tokens are added to a tree
//

void Aikido::addToken (string s, Token tok) {
    operators[tok] = s ;
    for (int i = 0 ; i < tokens.size() ; i++) {
        TokenNode *node = tokens[i] ;
        if (node->add (s, 0, tok)) {
            return ;
        }
    }
    TokenNode *node = new TokenNode (s[0], BAD, NULL) ;
    tokens.push_back (node) ;
    node->add (s, 0, tok) ;
}

void Aikido::printToken (std::ostream &os, Token tok) {
    string s = findOperator (tok) ;
    if (s != "") {
       os << s ;
    } else {
       os << "unknown" ;
    }
}

string Aikido::findOperator (Token tok) {
    const OperatorMap::key_type key (tok) ;
    OperatorMap::iterator s = operators.find (key) ;
    if (s != operators.end()) {
        return (*s).second ;
    }
    return "" ;
}


bool TokenNode::add (string &s, int index, Token tok) {
    if (s[index] != ch) {
        return false ;
    }
    if (index == (s.size() - 1)) {		// last char in s
        if (value == BAD) {
            value = tok ;
            return true ;
        } else {
            throw "Duplicate token" ;
        }
    }

    index++ ;    
    for (int i = 0 ; i < children.size() ; i++) {
        TokenNode *child = children[i] ;
        if (child->add (s, index, tok)) {
            return true ;
        }
    }
    TokenNode *node = new TokenNode (s[index], BAD, this) ;
    children.push_back (node) ;
    return node->add (s, index, tok) ;
}

Token TokenNode::find (char *&s) {
//    std::cout << "comparing " << *s << " with token node " << ch << '\n' ;
    if (*s != ch) {
        return BAD ;
    }
//    std::cout << "found match, looking at children\n" ;
    Token tok ;
    s++ ;
    for (int i = 0 ; i < children.size() ; i++) {
        if ((tok = children[i]->find (s)) != BAD) {
            return tok ;
        }
    }
//    std::cout << "returning value " << value << '\n' ;
    return value ;
}

void TokenNode::print (std::ostream &os, Token tok) {
    if (value == tok) {
        if (parent != NULL) {
            parent->print (os) ;
        }
        print (os) ;
    } else {
        for (int i = 0 ; i < children.size() ; i++) {
            children[i]->print (os, tok) ;
        }
    }
}


void TokenNode::print (std::ostream &os) {
    os << value ;
}

//
// read the next non-blank line from the input
//



void Aikido::readLine(bool skipblank) {
    int c = 'a';
    while (c != EOF) {
        std::istream &in = currentContext->getStream() ;
        if (in.eof()) {
            break ;
        }
        ch = line ;
        while ((c = in.get()) != '\n' && c != EOF) {
            if ((ch - line) >= MAXLINE) {
                error ("Line too long") ;
                ch[-1] = '\0' ;
                break ;
            }
            if (c != '\t' && c != '\r' && c != '\v' && (c < ' ' || c > 126)) {
                error ("Illegal character '\\x%x'", c) ;
                c = ' ' ;
            }
            *ch++ = (char)c ;
        }
        *ch++ = '\0' ;
        ch = line ;

#if 0
        // look for aliases and replace them
        std::set<std::string> disabled_aliases ;            // holds disabled aliases to prevent recursion
        int num_replacements = 0 ;                          // number of aliases replaced in a single pass
        
        // process all possible aliases in the line, stop when there were no replacements done
        do {
            std::string newcmd ;
            int i = 0 ;
            num_replacements = 0 ;
            std::set<std::string> used_aliases ;        // aliases we used in this pass
            
            while (line[i] != '\0') {
                // extract the command root for a possible alias match
                std::string root ;
                while (line[i] != '\0' && isspace (line[i])) {
                    i++ ;
                }
                // semicolon means continue
                if (line[i] != '\0' && line[i] == ';') {
                    newcmd += line[i++] ;
                    continue ;
                }
                bool instring = false ;
                while (line[i] != '\0') {
                    if (!instring && isspace (line[i])) {
                        break ;
                    } else if (line[i] == '\\') {
                        root += '\\' ;
                        i++ ;
                        if (line[i] != '\0') {
                            root += line[i++] ;
                        }
                    } else if (!instring && line[i] == ';') {
                        break ;
                    } else if (line[i] == '"' || line[i] == '\'') {
                        instring = !instring ;
                    }
                    root += line[i++] ;
                }
                
                // this is really low level.  The alias and unalias commands need to take affect right away and
                // are not reserved words
                if (root == "alias") {
                    // set an alias
                    std::string name ;
                    std::string value ;
                    // skip spaces before name
                    while (line[i] != '\0' && isspace (line[i])) {
                        i++ ;
                    }
                    // read the alias name
                    while (line[i] != '\0' && !isspace (line[i])) {
                        name += line[i++] ;
                    }
                    
                    // skip spaces before value
                    while (line[i] != '\0' && isspace (line[i])) {
                        i++ ;
                    }
                    // read the alias value
                    while (line[i] != '\0') {
                        value += line[i++] ;
                    }
                    if (value == "") {
                        show_alias (name, mainos) ;
                    } else {
                        set_alias (name, value, mainos) ;
                    }
                } else if (root == "unalias") {
                    std::string name ;
                    // skip spaces before name
                    while (line[i] != '\0' && isspace (line[i])) {
                        i++ ;
                    }
                    // read the alias name
                    while (line[i] != '\0' && !isspace (line[i])) {
                        name += line[i++] ;
                    }
                    unset_alias (name, mainos) ;
                } else {
                    std::string aliasvalue ;
                    bool aliasfound = find_alias(root, aliasvalue) && disabled_aliases.count(root) == 0 ;
                    if (aliasfound) {
                        while (line[i] != '\0' && isspace (line[i])) {
                            i++ ;
                        }
                        std::string tail ;
                        while (line[i] != '\0') {
                            if (line[i] == '\\') {
                                tail += '\\' ;
                                i++ ;
                                if (line[i] != '\0') {
                                    tail += line[i++] ;
                                }
                            } else if (line[i] == ';') {
                                break ;
                            }
                            tail += line[i++] ;
                        }
                        
                        expand_alias (aliasvalue, tail) ;
                        newcmd += tail ;
                        used_aliases.insert (root) ;
                        num_replacements++ ;
                    } else {
                        newcmd += " " + root ;
                        instring = false ;
                        while (line[i] != 0) {
                            if (!instring && isspace (line[i])) {
                                break ;
                            } else if (line[i] == '\\') {
                                newcmd += '\\' ;
                                i++ ;
                                if (line[i] != '\0') {
                                    newcmd += line[i++] ;
                                }
                            } else if (!instring && line[i] == ';') {
                                break ;
                            } else if (line[i] == '"' || line[i] == '\'') {
                                instring = !instring ;
                            }
                            newcmd += line[i++] ;
                        }
                    }
                }
            }
            
            // now globally disable all the aliases we used
            for (std::set<std::string>::iterator i = used_aliases.begin(); i != used_aliases.end() ; i++) {
                disabled_aliases.insert (*i) ;
            }
            
            // copy the new line back to the old one for the next pass
            strcpy (line, newcmd.c_str()) ;
        } while (num_replacements > 0) ;
#endif
        
        if (*ch == '#') {		// CPP # line "file"
            if (properties & NOCPP) {
               continue ;
            }
            char fn[256] ;		
            int lineNumber ;
            sscanf (ch+1, "%d \"%s\"", &lineNumber, fn) ;
            fn[strlen(fn) - 1] = 0 ;		// remove " from end of filename
            lineNumber-- ;					// will be incremented next time round loop
            currentContext->setFilename (fn) ;
            currentContext->setLineNumber (lineNumber) ;
            continue ;
        } else {
            currentContext->incLineNumber() ;
        }
        
        // replace any macros parameters in the line
        if (currentContext->hasMacro()) {
            replaceMacroParameters(ch, line, currentContext->getParaMap()) ;
            ch = line ;
        }
        
        if (!skipblank) {
            break ;
        }
        while (*ch != '\0' && isspace (*ch)) {
            ch++ ;
        }
        if (*ch == '/' && ch[1] == '/') {		// starts with comment
            continue ;
        }
        if (properties & NOCPP && *ch == '#') {
            continue ;
        }
        if (*ch != '\0') {
            break ;
        }
    }
}

//
// skip spaces in the current line
//

void Aikido::skipSpaces() {
    while (*ch != '\0' && isspace (*ch)) {
        ch++ ;
    }
}

std::string Aikido::getSpelling() {
    if (longString) {
        return longspelling;
    }
    return spelling;
}

//
// a long string is a string started by """ or ''' and ending with the same sequence.  Line feeds are maintained within the string.
//

void Aikido::readLongString (char terminator) {
    longspelling = "";
    
    std::istream &in = currentContext->getStream() ;
    while (!in.eof()) {
        // read to end of line
        while (*ch != '\0') {
            if (*ch == terminator && ch[1] == terminator && ch[2] == terminator) {
                // end of long string, stop
                ch += 3;
                return;
            }

            if (*ch == '\\') {
                switch (ch[1]) {
                case 'n': longspelling += '\n' ; break ;
                case 'r': longspelling += '\r' ; break ;
                case 't': longspelling += '\t' ; break ;
                case 'a': longspelling += '\a' ; break ;
                case 'b': longspelling += '\b' ; break ;
                case 'v': longspelling += '\v' ; break ;
                case 'x': {				// hex constant
                    int val = 0 ;
                    ch += 2 ;
                    int i = 0 ;
                    while (i < 2 && isxdigit (*ch)) {
                        if (*ch >= 'A') {
                            val = (val << 4) | toupper (*ch) - 'A' + 10 ;
                        } else {
                           val = (val << 4) | *ch - '0' ; 
                        }
                        i++ ; ch++ ;
                    }
                    longspelling += (char)val ;
                    ch -= 2 ;
                    break ;
                    }
                case '0': {				// octal constant
                    int val = 0 ;
                    ch += 1 ;
                    int i = 0 ;
                    while (i < 3 && (*ch >= '0' && *ch <= '7')) {
                        val = (val << 3) | *ch - '0' ; 
                        i++ ; ch++ ;
                    }
                    longspelling += (char)val ;
                    ch -= 2 ;
                    break ;
                    }
                default:
                    longspelling += ch[1] ;
                }
                ch += 2 ;
            } else {
                longspelling += *ch++ ;
            }
        }

        // maintain the line feed
        longspelling += '\n';
        // read another line
        readLine (false);
    }
    error ("End of file within long string literal");
}


//
// read the next token from the current position in the line.  The char pointer
// ch is incremented to the character after the token is read.  Spaces are
// skipped before the token.
//
// This will ignore comments and line feeds.  If a new line is read, it sets
// the newLineRead flag to true, otherwise it is set to false
//


void Aikido::nextToken() {
    char c ;
    Token tok = BAD ;
    newLineRead = false ;
anotherline:
    std::istream &in = currentContext->getStream() ;
    if (in.eof()) {
        currentToken = EOL ;
        return ;
    }
    skipSpaces() ;

    oldch = ch ;		// keep location at start of token
    if (*ch == '\0') {
        if (linebased) {
            currentToken = EOL ;
            return ;
        }
        readLine() ;
        newLineRead = true ;
        goto anotherline ;
    }
        c = *ch++ ;

        if (c == '/' && *ch == '/') {		// // comment, skip rest of line
            if (linebased) {
                currentToken = EOL ;
                return ;
            }
            readLine() ;
            newLineRead = true ;
            goto anotherline ;
        }
        if (properties & NOCPP && c == '#') {		// CPP comment, skip rest of line
            if (linebased) {
                currentToken = EOL ;
                return ;
            }
            readLine() ;
            newLineRead = true ;
            goto anotherline ;
        }
        if (c == '/' && *ch == '*') {			// C style comment
            ch++ ;
            for (;;) {
                while (*ch != '*') {
                    if (in.eof()) {
                        currentToken = EOL ;
                        return ;
                    }
                    if (*ch == '\0') {			// end of line
                        readLine(false) ;
                        newLineRead = true ;
                    } else {
                        ch++ ;
                    }
                }
                ch++ ;			// ch is *
                if (*ch == '/') {		// if next is /
                    ch++ ;			// skip it
                    goto anotherline ;		// and get next token
                }
            }
        }

        if (c == '.' && *ch != '.' && !isdigit (*ch)) {		// special case for .
            strcpy (spelling, ".") ;
            tok = IDENTIFIER ;
        } else if (isalpha (c) || c == '_') {			// identifier?
            char *s = spelling ;
            int n = 0 ;
            *s++ = c ;
            while (n < sizeof(spelling) - 1 && (isalnum (*ch) || *ch == '_')) {
                *s++ = *ch++ ;
                n++ ;
            }
            if (n == sizeof(spelling) - 1) {
                // skip to end of identifier
                while (isalnum (*ch) || *ch == '_') {
                    ch++ ;
                }
                s[-1] = 0 ;
                error ("Identifier too long") ;
                tok = IDENTIFIER ;
            } else {
                *s = '\0' ;
                TokenMap::iterator i = reservedWords.find (spelling) ;
                if (i != reservedWords.end()) {
                    tok = (*i).second ;
                } else {
                    tok = IDENTIFIER ;
                }
            }
        } else if (c == '\"' || c == '\'') {			// string literal or character constant?
            char *s = spelling ;
            int n = 0 ;
            char terminator = c;
            // check for a long string (three quote marks at the start of the string)
            if (ch[0] == terminator && ch[1] == terminator) {
                ch += 2;
                readLongString (terminator);
                currentToken = STRING;
                longString = true;
                return;
            }

            // regular short string
            while (n < sizeof(spelling)- 1 && (*ch != '\0' && *ch != terminator)) {
                if (*ch == '\\') {
                    switch (ch[1]) {
                    case 'n': *s = '\n' ; break ;
                    case 'r': *s = '\r' ; break ;
                    case 't': *s = '\t' ; break ;
                    case 'a': *s = '\a' ; break ;
                    case 'b': *s = '\b' ; break ;
                    case 'v': *s = '\v' ; break ;
                    case 'x': {				// hex constant
                        int val = 0 ;
                        ch += 2 ;
                        int i = 0 ;
                        while (i < 2 && isxdigit (*ch)) {
                            if (*ch >= 'A') {
                                val = (val << 4) | toupper (*ch) - 'A' + 10 ;
                            } else {
                               val = (val << 4) | *ch - '0' ; 
                            }
                            i++ ; ch++ ;
                        }
                        *s = (char)val ;
                        ch -= 2 ;
                        break ;
                        }
                    case '0': {				// octal constant
                        int val = 0 ;
                        ch += 1 ;
                        int i = 0 ;
                        while (i < 3 && (*ch >= '0' && *ch <= '7')) {
                            val = (val << 3) | *ch - '0' ; 
                            i++ ; ch++ ;
                        }
                        *s = (char)val ;
                        ch -= 2 ;
                        break ;
                        }
                    default:
                        *s = ch[1] ;
                    }
                    s++ ;
                    ch += 2 ;
                    n++ ;
                } else {
                    *s++ = *ch++ ;
                    n++ ;
                }
            }
            if (n == sizeof(spelling)-1) {
                error ("String too long") ;
                while (*ch != '\0' && *ch != '\"') {
                    ch++ ;
                }
            }
            if (*ch == terminator) {
                ch++ ;
            } else {
                error ("Unterminated string literal") ;
            }
            *s = '\0' ;
            if (terminator == '\'' && (s - spelling) == 1) {
                // single character?
                number = (int)spelling[0];
                number &= 0xff;  
                tok = CHAR;
            } else {
                longString = false;
                tok = STRING ;
            }
            //std::cout << "found string " << spelling << '\n' ;
        } else if (isdigit (c) || (c == '.' && isdigit (*ch))) {		// number
            bool fp = false ;
            if (c == '.') {
                fp = true ;
            } else {
                char *p = ch ;
                for (;;) {
                    if (*p == '.') {
                       if (p[1] != '.') {		// detect 1.. as integer
                           fp = true ;
                       }
                       break ;
                    }
                    if (*p == 'e' || *p == 'E') {
                        fp = true ;
                        break ;
                    }
                    if (!isdigit (*p)) {
                        break ;
                    }
                    p++ ;
                }
            }
            if (fp) {
                fnumber = strtod (ch - 1, &ch) ;
                tok = FNUMBER ;
            } else {
                int base = 10 ;			// assume decimal
                int t ;
                number = 0 ;
                if (c == '0') {
                    if (*ch == 'x' || *ch == 'X') {		// hex?
                        ch++ ;
                        base = 16 ;
                    } else if (*ch == 'b' || *ch == 'B') {		// binary?
                        ch++ ;
                        base = 2 ;
                    } else {
                        base = 8 ;
                        ch-- ;
                    }
                } else {
                    ch-- ;
                }
                number = OS::strtoll (ch, &ch, base) ;
                tok = NUMBER ;
            }
#if 0
    // now handled above
        } else if (c == '\'') {			// character
               int chcount = 0 ;
               spelling = "":
               number = 0 ;
               while (*ch != 0 && *ch != '\'') {
                   chcount++ ;
                   if (*ch == '\\') {
                       switch (ch[1]) {
                       case 'n': c = '\n' ; break ;
                       case 'r': c = '\r' ; break ;
                       case 't': c = '\t' ; break ;
                       case 'a': c = '\a' ; break ;
                       case 'b': c = '\b' ; break ;
                       case 'v': c = '\v' ; break ;
                       case 'x': {				// hex constant
                           int val = 0 ;
                           ch += 2 ;
                           int i = 0 ;
                           while (i < 2 && isxdigit (*ch)) {
                               if (*ch >= 'A') {
                                   val = (val << 4) | toupper (*ch) - 'A' + 10 ;
                               } else {
                                  val = (val << 4) | *ch - '0' ; 
                               }
                               i++ ; ch++ ;
                           }
                          c = (char)val ;
                           ch -= 2 ;
                            break ;
                           }
                       case '0': {				// octal constant
                           int val = 0 ;
                           ch += 1 ;
                           int i = 0 ;
                           while (i < 3 && (*ch >= '0' && *ch <= '7')) {
                               val = (val << 3) | *ch - '0' ; 
                               i++ ; ch++ ;
                          }
                           c = (char)val ;
                           ch -= 2 ;
                           break ;
                           }
                      default:
                          c = ch[1] ;
                      }
                      ch++ ;
                 } else {
                      c = *ch ;
                 }
                 number = (number << 8) | c ;
                 ch++ ;
            }
            if (*ch == '\'') {
                ch++ ;
            } else {
                error ("Unterminated character constant") ;
            }
            if (chcount > 1) {
                // make a character constant with more than one character into a string
                
                error ("Too many characters in character constant, max 8") ;
            } else {
                tok = CHAR;
            }
            tok = chcount == 1 ? CHAR : NUMBER ;
#endif
        } else {       				// check for one of the lexical tokens
            ch-- ;
//            std::cout << "looking for token " << ch << '\n' ;
            for (int i = 0 ; i < tokens.size() ; i++) {
                TokenNode *node = tokens[i] ;
                if ((tok = node->find (ch)) != BAD) {
                    break ;
                }
            }
        }

    if (tok == BAD) {			// need to advance current position or we get stuck
        ch++ ;
    }
    currentToken = tok ;					// end of scan, set currentToken
}

//
// is the character a valid operator token?
//
bool Aikido::isOperator (char c) {
    switch (c) {
    case '=':
    case '+':
    case '-':
    case '*':
    case '/':
    case '%':
    case '&':
    case '|':
    case '^':
    case '(':
    case '<':
    case '>':
    case '[':
    case '\"':
    case '\'':
    case '{':
    case '}':
    case '.':
        return true ;
    }
    return false ;
}


//
// is the token one of the set of overloadable operators?
//

bool Aikido::isOverloadableOperator (Token tok) {
    switch (tok) {
    case STAR:
    case LBRACK:
    case DOT:
    case LSQUARE:
    case PLUS:
    case MINUS:
    case SLASH:
    case PERCENT:
    case TILDE:
    case CARET:
    case BANG:
    case AMPERSAND:
    case BITOR:
    case LSHIFT:
    case RSHIFT:
    case ZRSHIFT:
    case LESS:
    case LESSEQ:
    case GREATER:
    case GREATEREQ:
    case EQUAL:
    case NOTEQ:
    case ARROW:
    case SIZEOF:
    case TYPEOF:
    case FOREACH:
    case TIN:
    case CAST:
       return true ;
    }
    return false ;
}

//
// is the start of the current line recognized?
//

bool Aikido::isRecognized() {
    int i ;
    char buf[1024] ;
    char *oh = ch ;

    if (*ch == '\0') {			// end of line?
        return false ;
    }
    skipSpaces() ;
    if (*ch == '/') {                   // foreign command can start with / (filename)
        return false ;                  // this is never a valid aikido statement
    }
    char *firstword = ch ;
    
    i = 0 ;					// gather first word in line
    while (*ch != 0 && (isalnum (*ch) || *ch == '.' || *ch == '_')) {
        buf[i++] = *ch++ ;
    }
    buf[i] = 0 ;
    skipSpaces() ;

    string str = buf ;
    //
    // check for macro instantiation
    //
    
    Package *p ;
    Macro *mac = findMacro (str, p) ;
    if (mac != NULL) {
        return true ;
    }

    if (worker != NULL && worker->isClaimed (firstword)) {
        return false ;
    }

    //
    // check for reserved word
    //
    
    TokenMap::iterator t = reservedWords.find (buf) ;
    if (t != reservedWords.end()) {
        return true ;
    }
    
    if (isOperator (*ch)) {
        return true ;
    }
    
    return false ;
}

//
// parse and evaluate an expression
//

Value Aikido::getExpression(StackFrame *stack, StaticLink *staticLink, Scope *cs, int csl) {
    currentScope = cs ;			// findVariable (called by expression()) need these
    currentScopeLevel = csl ;
    int errors = numErrors ;
    Node *node = expression() ;
    node = new Node (this, RETURN, node) ;

    if (numErrors == errors && node != NULL) {
        codegen::CodeSequence *code = codegen->generate (node) ;
        DebugState oldstate = vm->debugState ;
        vm->debugState = RUN ;
        vm->execute (code, stack, staticLink, 0) ;
        deleteTree (node) ;
        Value result = vm->retval ;
        vm->retval.destruct() ;
        vm->debugState = oldstate ;
        delete code ;
        return result ;
    } else {
        return 0 ;
    }
}


Value Aikido::getExpression (std::string expr, StackFrame *stack, StaticLink *slink, Scope *cs, int csl) {
    parserLock.acquire(true) ;

    Scope *oldscope = currentScope ;
    int oldsl = currentScopeLevel ;
    Block *oldmsl = currentMajorScope ;
    StackFrame *oldstack = currentStack ;

#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream stream ;
#else
    std::stringstream stream ;
#endif
    stream << expr ;

    currentScope = cs ;			// findVariable (called by expression()) need these
    currentScopeLevel = csl ;

    currentStack = stack ;
    currentMajorScope = currentScope->majorScope ;
    currentScopeLevel = csl ;

    string filename ("unknown") ;
    Context ctx (stream, filename, 1) ;
    currentContext = &ctx ;
    readLine() ;
    stream.clear() ;
    nextToken() ;
    
    currentClass = NULL ;
    try {
        Node *node = expression() ;
        Node *tree = node ;
        currentContext = ctx.getPrevious() ;

        if (node != NULL) {
            node = new Node (this, RETURN, node) ;
            codegen::CodeSequence *code = codegen->generate (node) ;
            parserLock.release(true) ;

            // lock is released before code is executed
            vm->execute (static_cast<InterpretedBlock*>(currentMajorScope), 
                code,  currentStack, currentStack->slink, 0) ;

            parserLock.acquire(true) ;
            currentStack = oldstack ;
            currentScope = oldscope ;
            currentScopeLevel = oldsl ;
            currentMajorScope = oldmsl ;

            deleteTree (node) ;
            parserLock.release(true) ;
            Value result = vm->retval ;
            vm->retval.destruct() ;
            delete code ;
            return result ;
        } else {
            currentStack = oldstack ;
            currentScope = oldscope ;
            currentScopeLevel = oldsl ;
            currentMajorScope = oldmsl ;
            deleteTree (tree) ;
            parserLock.release(true) ;
            return Value() ;
        }
    } catch (...) {
        currentStack = oldstack ;
        currentScope = oldscope ;
        currentScopeLevel = oldsl ;
        currentMajorScope = oldmsl ;
        parserLock.release(true) ;
        throw ;
    }
}


//
// instantiate a macro
//

Node *Aikido::instantiateMacro (char *args, Macro *macro, Scope *scope) {
    if (macro->isDisabled()) {
        return NULL ;
    }
    Context ctx (macro, currentContext) ;
    Context::ParaMap &map = ctx.getParaMap() ;
    collectActualArgs (args, map, macro) ;
    macro->disable() ;		// prevent runaway recusion
    Node *node = NULL ;
    if (macro->super != NULL) {
        char newline[4096] ;
        const char *pch = macro->superargs.c_str() ;
        replaceMacroParameters ((char*)pch, newline, map) ;
        node = instantiateMacro (newline, macro->super, scope) ;
    }
    ctx.rewind() ;
    currentContext = &ctx ;
    resetInput() ;				// read first line of macro
    try {
        node = new Node (this, SEMICOLON, node, parseStatementSequence(scope)) ;
    } catch (...) {
        currentContext = ctx.getPrevious() ;
        throw ;
    }
   
    node->scope = macro->getInstanceScope() ;		// store instanceScope in MACRO node
    currentContext = ctx.getPrevious() ;
    return node ;
}

//
// this is called to parse a sequence of statements.  The sequence
// is terminated either by the end of file or a RBRACE token.  If terminated
// by the RBRACE, CASE or DEFAULT tokens, the token is not absorbed.  If the scope parameter is
// NULL, a new scope is created for the sequence of statements
//

Node *Aikido::parseStatementSequence (Scope *scope) {
    Node *node = NULL ;
    int startLevel = blockLevel++ ;
    std::istream &in = currentContext->getStream() ;
    Scope *oldCurrentScope = currentScope ;
    if (scope == NULL) {
        scope = new Scope (this, currentScopeLevel, currentScope, currentMajorScope) ;
        currentScope->addChild (scope) ;		// so that it can be deleted
        currentScope = scope ;
    }

    while (!in.eof()) {
        if (currentToken == BACKTICK || currentToken == RBRACE || currentToken == CASE || currentToken == DEFAULT) {
            break ;
        }
        if (currentToken == SEMICOLON) {
            nextToken() ;
            continue ;
        }
        Node *state = parseStatement() ;
        if (state != NULL) {			// can return null
            if (node != NULL) {
                state = new Node (this,SEMICOLON, node, state) ;
            }
            node = state ;
        }
    }
    currentScope = oldCurrentScope ;
    if (node != NULL) {
        node->scope = scope ;
    }
    return node ;
}

//
// parse a single statement
//
Node *Aikido::parseStatement() {
    if (currentToken == IDENTIFIER) {			// check if possible macro at start of statement
        string str = spelling ;
        Package *pkg ;
        Macro *macro = findMacro (str, pkg) ;
        if (macro != NULL) {
            //printf ("found macro %s\n", buf) ;
            if (!macro->isDisabled()) {
                macro->instancePackage = pkg ;
                macro->instantiate (currentScope) ;
                pushScope (macro) ;
                Scope *scope = new Scope (this, currentScopeLevel, currentScope, currentMajorScope) ;
                currentScope->addChild (scope) ;
                Scope *oldscope = currentScope ;
                currentScope = scope ;
                Node *node = instantiateMacro (ch, macro, scope) ;
                node = new Node (this, MACRO, node) ;		// make macro node to hold scope
                node->value.ptr = (void*)macro ;			// save macro pointer
                node->scope = node->left->scope ;		// store instanceScope in MACRO node
                macro->uninstantiate () ;
                macro->instancePackage = NULL ;
                popScope() ;
                resetInput() ;
                return node ;
            }
        }
    }


    if (currentToken == LBRACE) {
        nextToken() ;
        Node *node = parseStatementSequence() ;		// compound statement in a new scope
        if (node != NULL) {
            node = new Node (this, COMPOUND, node) ;
            if (node->left != NULL) {
                node->scope = node->left->scope ;
            }
        }
        needbrack (RBRACE) ;
        return node ;
    }


    //
    // check for things that are valid starts for statements or expressions
    //
    
    switch (currentToken) {
    case SIZEOF:
    case TYPEOF:
    case NEW:
    case TTRUE:
    case TFALSE:
    case KNULL:
    case CAST:
        return parseExpressionStatement() ;

    case ELLIPSIS:
    case MACRO:
    case VAR:
    case GENERIC:
    case TCONST:
    case IF:
    case FOREACH:
    case WHILE:
    case DO:
    case FUNCTION:
    case MONITOR:
    case THREAD:
    case SWITCH:
    case BREAK:
    case CONTINUE:
    case RETURN:
    case YIELD:
    case FOR:
    case TRY:
    case THROW:
    case CLASS:
    case TDELETE:
    case IMPORT:
    case PACKAGE:
    case PUBLIC:
    case PRIVATE:
    case PROTECTED:
    case ENUM:
    case STATIC:
    case SYNCHRONIZED:
    case OPERATOR:
    case USING:
    case EXTEND:
    case TINTERFACE:
    case ANNOTATE:
    case NATIVE: {
        return parseCommand(currentAccessMode, false, false) ;
        }

    case ELSE:
    case ELIF:
        error ("Dangling 'else' or 'elif' with no corresponding 'if'") ;
        nextToken() ;
        return NULL ;

    case CASE:
    case DEFAULT:
        error ("'case' or 'default' outside switch statement") ;
        nextToken() ;
        return NULL ;

    case CATCH:
        error ("'catch' outside try statement") ;
        nextToken() ;
        return NULL ;

    case FINALLY:
        error ("'finally' outside try statement") ;
        nextToken() ;
        return NULL ;
    }

    // expressions can't begin with /
    if (currentToken != IDENTIFIER && currentToken != SLASH) {
        return parseExpressionStatement() ;
    }

    // must be an identifier, so check if worker wants it

    if (worker != NULL) {
        char word[256] ;				// copy first word on line to see
        char *s = word ;				// if worker wants it
        char *p = oldch ;
        while (*p != 0 && (isalnum (*p) || *p == '.' || *p == '_' || *p == '/')) {
            *s++ = *p++ ;
        }
        *s = 0 ;
        while (*p != 0 && isspace (*p)) {
            p++ ;
        }
        if (isOperator (*p) && !worker->isClaimed (oldch))  {
            return parseExpressionStatement() ;
        }
    } else {
        return parseExpressionStatement() ;
    }
    
    
    // if we have a worker then collect a set of unrecognized lines for processing
    // by the worker during the execute phase
    if (worker != NULL) {
        ch = oldch ;			// reset current character
    
#if defined(_CC_GCC) && __GNUC__ == 2
        std::strstream *str = new std::strstream ;
#else
        std::stringstream *str = new std::stringstream ;
#endif
        Node *node = new Node (this,str, &currentContext->getFilename(), currentContext->getLineNumber() - 1) ;
        readForeignLines (*str) ;
        return node ;
    } else {
        error ("Syntax error") ;			// no worker, we have to punt
        nextToken() ;
    }
}


//
// read a set of lines from the input into the given stream, terminating when we come
// across something that is recognized.  The current line has not been recognized so this
// is the first line in the sequence.
//

// stop at a semicolon or backtick, unless it's inside a string.  
// comments are not removed

void Aikido::readForeignLines(std::iostream &stream) {
    std::istream &in = currentContext->getStream() ;
    bool instring = false ;
    while (!in.eof()) {
        //std::cout << "read foreign line: " << line << '\n' ;
        while (*ch != '\0') {
            if (*ch == '\\') {          // escape?
                ch++ ;
                if (*ch == 0) {         // continuation...
                    stream.put (' ') ;
                    readLine(false) ;
                    continue ;
                }
                stream.put (*ch++) ;
            } else {
                if (*ch == '"' || *ch == '\'') {
                    if (instring) {
                        instring = false ;
                    } else {
                        instring = true ;
                    }
                }
                if (!instring && nbackticks != 0 && *ch == '`') {           // backtick terminates
                    break ;
                }
                if (*ch == ';' && !instring) {
                    break ;
                }
#if 0
                if (*ch == '/' && ch[1] == '/' && !instring) {              // comment
                    stream.put (' ') ;
                    break ;
                }
#endif
                // XXX: handle mutiline comments
                stream.put (*ch++) ;
            }
        }
        stream.put ('\n') ;
        if (*ch == '\0') {          // if end of line, read another
            readLine (false) ;
        } else if (*ch == '`' && nbackticks != 0) {        // end of inline foreign, get token and get outta here
            nextToken() ;
            break ;
        } else if (*ch == ';') {
            nextToken() ;
            break ;
        }
        oldch = ch ;
        if (isRecognized()) {
           ch = oldch ;
           nextToken() ;			// read first token of recognized line
           break ;
        }
        ch = oldch ;
    }
    //std::cout << "end of foreign lines at: " << line << '\n' ;
}


Node *Aikido::parseExpressionStatement() {
    return expression() ;
}


static const char *blockName (Token t) {
    switch (t) {
    case THREAD:
        return "thread" ;
    case FUNCTION:
        return "function" ;
    case CLASS:
        return "class" ;
    case MONITOR:
        return "monitor" ;
    case PACKAGE:
        return "package" ;
    case OPERATOR:
        return "operator" ;
    case ENUM:
        return "enum" ;
    case TINTERFACE:
        return "interface" ;
    case ANNOTATE:
        return "@interface" ;
    }
    return "block" ;
}

//
// parse an if statement
//

Node *Aikido::parseIf() {
    Node *node ;
    Node *ifpart ;
    if (properties & ALLOW_OMITTED_PARENS) {
        node = single_expression() ;
    } else {
        needbrack (LBRACK) ;
        node = single_expression() ;
        needbrack (RBRACK) ;
    }
    if (node->op == ASSIGN) {
        warning ("Assignment used as condition") ;
    }
    ifpart = parseStatement() ;
    if (match (ELSE)) {                                 // else part?
        Node *elsepart = parseStatement() ;
        Node *oldif = ifpart ;
	ifpart = new Node (this,ELSE, ifpart, elsepart) ;
        if (elsepart != NULL) {
	    ifpart->scope = elsepart->scope ;
        } else {
            if (oldif != NULL) {
	        ifpart->scope = oldif->scope ;
            }
        }
    } else if (match (ELIF)) {                          // elif part?
	Node *elif = parseIf() ;
	Node *oldif = ifpart ;
	ifpart = new Node (this, ELSE, ifpart, elif) ;
        if (elif != NULL) {
            ifpart->scope = elif->scope ;
        } else {
            if (oldif != NULL) {
	        ifpart->scope = oldif->scope ;
            }
        }
    }

    node = new Node (this,IF, node, ifpart) ;
    if (ifpart != NULL) {
        node->scope = ifpart->scope ;
    }
    return node ;
}

// is the node a complex expression?   A complex expression is deemed to be
// one whose value may be a different type on each invocation

bool Aikido::isComplex (Node *n) {
    if (n == NULL) {
        return false ;
    }
    if (isComplex (n->left)) {
        return true ;
    }
    if (isComplex (n->right)) {
        return true ;
    }
    switch (n->op) {
    case LSQUARE:		// vector indices may be different types
    case LBRACK:		// function calls 
    case DOT:			// don't know what type this is
    case IDENTIFIER:		// can be any type
        return true ;
    }
    return false ;
}

//
// parse a known command.  Curent token is the first parameter of the command
//

Node *Aikido::parseCommand (Token access, bool isstatic, bool issync) {
    Node *node = NULL ;
    Token oldaccess = currentAccessMode ;
    currentAccessMode = access ;
    AnnotationList annotations;

    Token command = currentToken ;
    if (command == ANNOTATE) {      // @ symbol
        nextToken();
        if (currentToken == TINTERFACE) {
            // definition of an annotation
            return getBlock (command, isstatic, issync, annotations);
        } else {
            annotations = parseAnnotationSequence();
            command = currentToken;
        }
    }

    switch (command) {
    case TCONST:
    case GENERIC:
    case VAR: {
        Variable *var ;
        nextToken() ;
        for (;;) {
            if (currentToken == TIN) {
               strcpy (spelling, "in") ;
               currentToken = IDENTIFIER ;
            }
            if (currentToken != IDENTIFIER) {
                error ("Identifier expected in variable/constant declaration") ;
            } else {
                string varname = spelling ;
                if ((var = findTopScopeVariable (varname)) == NULL) {
                    if (currentStack != NULL) {
                        currentStack->checkCapacity() ;
                        var = new Variable (varname, currentStack->block->stacksize++) ;
                        currentStack->block->insertVariable (var) ;
                        addVariableName (varname, currentStack->block, var) ;
                        var->setAccess (PUBLIC) ;		
                    } else {
                        var = addVariable (varname) ;
                    }
                    var->annotations = annotations;
                } else {
                    error ("Multiple definition of variable/constant %s", spelling) ;
                }
                if (command == TCONST) {
                    var->flags |= VAR_CONST ;
                }
                if (command == GENERIC) {
                    var->flags |= VAR_GENERIC ;
                }
                if (isstatic) {
                    var->flags |= VAR_STATIC ;
                }
                nextToken() ;


                bool typedefined = false;
                Type vartype;
                Node *typenode;

                // 6/5/2012: DA.  Allow type definitions for variables (var x:int [=foo()])
                if (match (COLON)) {
                    typenode = postfixExpression();
                    if (typenode != NULL && typenode->flags & NODE_TYPEFIXED) {
                        vartype = typenode->type;
                        typedefined = true;
                    } else {
                        error ("The type of a variable or constant must be a compile-time constant");
                    }

                    // check for subtype specification
                    if (match (LESS)) {
                        bool ok = true;
                        switch (vartype) {
                        case T_VECTOR:
                        case T_MAP:
                        case T_CLASS:
                        case T_MONITOR:
                            break;
                        default:
                            error ("Subtype specification is not applicable for this type");
                            ok = false;
                        }

                        if (ok) {
                            std::vector<Node*> types;
                            while (currentToken != GREATER) {
                                Node *n = primaryExpression();
                                types.push_back (n);
                                if (!match (COMMA)) {
                                    break;
                                }
                            }
                            needbrack (GREATER);

                            switch (vartype) {
                            case T_VECTOR:
                                if (types.size() != 1) {
                                    error ("Vectors must have one subtype");
                                }
                                break;
                            case T_MAP:
                                if (types.size() != 2) {
                                    error ("Maps must have two subtypes (key and value)");
                                }
                                break;
                            case T_CLASS:
                            case T_MONITOR: {
                               
                                break;
                                }
                            }
                        } else {
                            while (currentToken != BAD && currentToken != GREATER) {
                                nextToken();
                            }
                        }
                    }
                }


                if (match (ASSIGN)) {      
                    int oldp = properties ;
                    if (isstatic) {
                        properties |= STATIC_ONLY ;		// allow static ids only
                    }
                    Node *varnode = new Node (this,IDENTIFIER, var) ;
                    Node *assign = new Node (this,command == TCONST ? CONSTASSIGN : ASSIGN, varnode) ;
                    if (typedefined) {
                        assign->right = new Node (this, CAST, typenode, single_expression()) ;      // cast initial value to the type defined
                    } else {
                        assign->right = single_expression() ;
                    }
                    properties = oldp ;
                    if (command != GENERIC && isComplex (assign->right)) {
                        var->flags |= VAR_NEEDINIT ;
                    }

                    // if we are assigning a tree whose type is fixed then
                    // the variable and the whole assignment tree is fixed in type
                    if (typedefined) {
                        // variables with defined types are always fixed type
                        assign->left->flags |= NODE_TYPEFIXED ;
                        varnode->flags |= NODE_TYPEFIXED ;
                        var->flags |= VAR_TYPEFIXED ;
                        var->type = vartype ;
                        assign->type = vartype ;
                    } else {
                        if (command != GENERIC && assign->right->flags & NODE_TYPEFIXED) {
                            assign->left->flags |= NODE_TYPEFIXED ;
                            varnode->flags |= NODE_TYPEFIXED ;
                            var->flags |= VAR_TYPEFIXED ;
                            var->type = assign->right->type ;
                            assign->type = assign->right->type ;
                        }
                    }

                    if (node == NULL) {
                        node = assign ;
                    } else {
                        node = new Node (this,SEMICOLON, node, assign) ;
                    }

                } else {
                    if (!typedefined && command != GENERIC) {
                        error ("Variables and constants must be assigned a value at declaration") ;
                    } else if (typedefined) {
                        // we don't have an initializer for the variable.  Make one by assigning the typenode itself
                        int oldp = properties ;
                        if (isstatic) {
                            properties |= STATIC_ONLY ;		// allow static ids only
                        }
                        Node *varnode = new Node (this,IDENTIFIER, var) ;
                        Node *assign = new Node (this,command == TCONST ? CONSTASSIGN : ASSIGN, varnode) ;
                        assign->right = typenode;
                        properties = oldp ;

                        assign->left->flags |= NODE_TYPEFIXED ;
                        varnode->flags |= NODE_TYPEFIXED ;
                        var->flags |= VAR_TYPEFIXED ;
                        var->type = assign->right->type ;
                        assign->type = assign->right->type ;
 
                        if (node == NULL) {
                            node = assign ;
                        } else {
                            node = new Node (this,SEMICOLON, node, assign) ;
                        }
                    }
                }
            }
            if (!isComma()) {
                break ;
            }
        }
        break ;
        }

    case IF: {
        nextToken() ;
        node = parseIf() ;
        break ;
        }

    case BREAK:
        if (nloops > 0 || nswitches > 0) {
            node = new Node (this,BREAK) ;
        } else {
            error ("break outside loop or switch") ;
        }
        nextToken() ;
        break ;

    case CONTINUE:
        if (nloops > 0) {
            node = new Node (this,CONTINUE) ;
        } else {
            error ("continue outside loop") ;
        }
        nextToken() ;
        break ;

    case RETURN:		// complex to work out if we have an expression
    case YIELD:
        if (nfuncs > 0) {
            skipSpaces() ;			// if next token is ;, comment or EOL
            node = new Node (this, command, (Node *)NULL) ;
            if (*ch == ';' || *ch == 0 || (*ch == '/' && (ch[1] == '/' || ch[1] == '*'))) {
                if (currentMajorScope->type == T_FUNCTION) {
                    Function *f = (Function*)currentMajorScope ;
                    if (f->returnType != NULL) {
                        error ("Must return a value") ;
                    }
                }
                nextToken() ;
            } else {
                nextToken() ;
                node->left = expression() ;		// expression there
                if (currentMajorScope->type == T_FUNCTION) {
                    Function *f = (Function*)currentMajorScope ;
                    node->right = copyTree (f->returnType) ;		// copy return type to node
                }
            }
        } else {
            if (command == RETURN) {
                error ("return outside function") ;
            } else {
                error ("yield outside function") ;
            }
            nextToken() ;
        }
        break ;
    case WHILE: {
        nextToken() ;
        node = new Node (this,WHILE, NULL, NULL) ;
        Node *cond ;
        if (properties & ALLOW_OMITTED_PARENS) {
            cond = single_expression() ;
        } else {
            needbrack (LBRACK) ;
            cond = single_expression() ;
            needbrack (RBRACK) ;
        }
        if (cond->op == ASSIGN) {
            warning ("Assignment used as condition") ;
        }
        nloops++ ;
        Node *body = parseStatement() ;
        nloops-- ;
        node->left = cond ;
        node->right = body ;
        break ;
        }

    case DO: {
        nextToken() ;
        node = new Node (this,DO, NULL, NULL) ;
        nloops++ ;
        Node *body = parseStatement() ;
        nloops-- ;
        if (!match (WHILE)) {
            error ("Missing 'while' for 'do' loop") ;
            node = body ;
        } else {
            Node *cond ;
            if (properties & ALLOW_OMITTED_PARENS) {
                cond = single_expression() ;
            } else {
                needbrack (LBRACK) ;
                cond = single_expression() ;
                needbrack (RBRACK) ;
            }
            if (cond->op == ASSIGN) {
                warning ("Assignment used as condition") ;
            }
            node->left = cond ;
            node->right = body ;
        }
        break ;
        }

    case FOR: {
        nextToken() ;
        Scope *scope = new Scope (this, currentScopeLevel, currentScope, currentMajorScope) ;
        currentScope->addChild (scope) ;
        pushScope (scope) ;
        needbrack (LBRACK) ;
        Node *e1 = NULL, *e2 = NULL, *e3 = NULL ;
        Variable *var = NULL;
        if (currentToken != SEMICOLON) {
            if (match (VAR)) {
                if (currentToken == TIN) {
                   strcpy (spelling, "in") ;
                   currentToken = IDENTIFIER ;
                }

                if (currentToken == IDENTIFIER) {
                    var = addVariable (spelling) ;
                    nextToken() ;
                    if (match (ASSIGN)) {
                        Node *varnode = new Node (this,IDENTIFIER, var) ;
                        Node *val = single_expression() ;
                        e1 = new Node (this,ASSIGN, varnode, val) ;
                        // check for fixed type assignment (likely)
                        if (val->flags & NODE_TYPEFIXED) {
                            varnode->flags |= NODE_TYPEFIXED ;
                            var->flags |= VAR_TYPEFIXED ;
                            var->type = val->type ;
                            e1->type = val->type ;
                        }
                    } else {
                        // allow 'for (var x in y)' and 'for (var x in 1..4)'
                        if (currentToken == COLON || currentToken == TIN) {
                            // convert to FOREACH
                            node = new Node (this,FOREACH) ;
                            nextToken();

                            Node *expr = expression() ;

                            // check for range - foreach i in 1 .. 4
                            if (match (ELLIPSIS)) {
                                Node *r = single_expression() ;
                                expr = new Node (this, RANGE, expr, r) ;
                            }

                            needbrack (RBRACK);

                            nloops++ ;
                            node->right = parseStatement() ;
                            nloops-- ;

                            Node *ctrl = new Node (this,IDENTIFIER, var) ;
                            node->left = new Node (this,COMMA, ctrl, expr) ;
                            node->scope = scope ;
                            popScope() ;
                            break;          // end of FOR case
                        }
                    }
                } else {
                    error ("Identifier expected for variable name") ;
                }
            } else {
                // 6/5/2012: DA, allow comma in first expression
                for (;;) {
                    Node *e = single_expression() ;
                    if (e1 != NULL) {
                        e1 = new Node (this, SEMICOLON, e1, e);
                    } else {
                        e1 = e;
                    }

                    if (!match (COMMA)) {
                        break;
                    }
                }
            }
        }

        node = new Node (this,FOR, NULL, NULL) ;
        needsemi() ;
        if (currentToken != SEMICOLON) {
            e2 = single_expression() ;
            if (e2->op == ASSIGN) {
                warning ("Assignment used as condition") ;
            }
        }
        needsemi() ;
        if (currentToken != RBRACK) {
            // 6/5/2012: DA, allow comma in third expression
            for (;;) {
                Node *e = single_expression() ;
                if (e3 != NULL) {
                    e3 = new Node (this, SEMICOLON, e3, e);
                } else {
                    e3 = e;
                }
                if (!match (COMMA)) {
                    break;
                }
            }
        }
        needbrack (RBRACK) ;
        e1 = new Node (this,SEMICOLON, e1, e2) ;
        e3 = new Node (this,SEMICOLON, e1, e3) ;

        nloops++ ;
        Node *body = parseStatement() ;
        nloops-- ;

        node->left = e3 ;
        node->right = body ;

        node->scope = scope ;
        popScope() ;
        break ;
        }

    case FOREACH: {
        nextToken() ;
        Scope *scope = new Scope (this, currentScopeLevel, currentScope, currentMajorScope) ;
        currentScope->addChild (scope) ;
        pushScope (scope) ;
        node = new Node (this,FOREACH) ;

        if (currentToken == TIN) {
           strcpy (spelling, "in") ;
           currentToken = IDENTIFIER ;
        }

        if (currentToken == IDENTIFIER) {
            Variable *var = NULL ;
            if (currentStack != NULL) {
                currentStack->checkCapacity() ;
                var = new Variable (spelling, currentStack->block->stacksize++) ;
                currentStack->block->insertVariable (var) ;
                addVariableName (spelling, currentStack->block, var) ;
                var->setAccess (PUBLIC) ;		
            } else {
                var = addVariable (spelling) ;
            }
            var->flags |= VAR_GENERIC ;
            nextToken() ;
            match (TIN) ;		// allow optional 'in'
            
            Node *expr = expression() ;

            // check for range - foreach i in 1 .. 4
            if (match (ELLIPSIS)) {
                Node *r = single_expression() ;
                expr = new Node (this, RANGE, expr, r) ;
            }

            nloops++ ;
            node->right = parseStatement() ;
            nloops-- ;

            Node *ctrl = new Node (this,IDENTIFIER, var) ;
            node->left = new Node (this,COMMA, ctrl, expr) ;
            node->scope = scope ;
        } else {
            error ("foreach requires a control variable") ;
        }
        popScope() ;
        break ;
        }

    case SWITCH: {
        nextToken() ;
        Node *switchnode = new Node (this,SWITCH) ;
        needbrack (LBRACK) ;
        switchnode->left = single_expression() ;
        needbrack (RBRACK) ;
        Node *expr, *block ;
        nswitches++ ;
        needbrack (LBRACE) ;
        bool defaultseen = false ;
        while (currentToken != RBRACE) {
            switch (currentToken) {
            case CASE: {
                Token tok = EQUAL ;
                nextToken() ;
                node = new Node (this,CASE) ;
                // check for operator after case
                switch (currentToken) {
                case TIN:
                case LESS:
                case GREATER:
                case LESSEQ:
                case GREATEREQ:
                case EQUAL:
                case NOTEQ:
                    tok = currentToken ;
                    nextToken() ;
                    break ;
                }
                node->value.integer = tok ;     // assign comparison token into node

                if (tok == TIN) {
                    node->left = rangeExpression() ;        // allow case in a..b
                } else {
                    node->left = single_expression() ;
                }
                match (COLON) ;     // optional
                block = parseStatementSequence() ;
                block = new Node (this, COMPOUND, block) ;
                if (block->left != NULL) {
                    block->scope = block->left->scope ;
                }
                node->right = block ;
                switchnode->vec.push_back (node) ;
                break ;
                }
            case DEFAULT:
                if (defaultseen) {
                    error ("Multiple defaults in switch statement") ;
                }
                defaultseen = true ;
                nextToken() ;
                match (COLON) ;
                node = new Node (this,DEFAULT) ;
                block = parseStatementSequence() ;
                block = new Node (this, COMPOUND, block) ;
                if (block->left != NULL) {
                    block->scope = block->left->scope ;
                }
                node->left = block ;
                switchnode->vec.push_back (node) ;
                break ;
            default:
                error ("switch statement syntax error, case or default expected") ;
                nextToken() ;
            }
        }
        needbrack (RBRACE) ;
        nswitches-- ;
        node = switchnode ;
        break ;
        }

    case MACRO: {
        Macro *macro ;
        std::istream &in = currentContext->getStream() ;
        nextToken() ;
        if (currentToken == IDENTIFIER) {
            string macroname = spelling ;
            if ((macro = findTopScopeMacro (macroname)) != NULL) {
                error ("Duplicate definition of macro %s", spelling) ;
                break ;
            }
            macro = new Macro (this, currentScopeLevel, currentScope, currentMajorScope, macroname, currentContext->getFilename(), currentContext->getLineNumber()) ;
            nextToken() ;
            while (currentToken == IDENTIFIER) {		// collect arguments
                string paraname = spelling ;
                macro->addParameter (paraname) ;
                nextToken() ;
                if (match (ASSIGN)) {
                    if (currentToken != STRING) {
                        error ("Default value of macro parameter must be a string") ;
                    } else {
                        macro->addParaDef (paraname, getSpelling()) ;
                    }
                    nextToken() ;
                }
                match (COMMA) ;			// ignore if there
            }
            if (match (EXTENDS)) {
                if (currentToken != IDENTIFIER) {
                    error ("Identifier expected after : or extends") ;
                } else {
                    string supername = spelling ;
                    Package *superpkg ;
                    Macro *super = findMacro (supername, superpkg) ;
                    if (super == NULL) {
                        error ("Undefined macro %s used as base macro in derivation", supername.c_str()) ;
                    } else {
                        macro->derive (super) ;
                    }
                    macro->superargs = ch ;			// copy rest of line into superargs of macro
                    nextToken() ;
                }
            }
            copyToEnd (macro->getBody()) ;

            insertMacro (macro) ;
        } else {
            error ("Macro name expected") ;
            // recover
            skipToEnd() ;
        }
        break ;
        }

    case ELLIPSIS: {
       if (!currentContext->hasMacro()) {
           error ("Inner statement (...) is only valid inside macros") ;
           nextToken() ;
       } else {
           Context *old = currentContext ;
           currentContext = currentContext->getPrevious() ;
           resetInput() ;
           node = parseStatementSequence() ;
           currentContext = old ;
           resetInput() ;
       }
       break ;
       }

    case TRY: {
       nextToken() ;
       node = new Node (this,TRY) ;
       node->left = parseStatement () ;
       Node *catchNode = NULL ;
       Node *finallyNode = NULL;

       if (match (CATCH)) {
           catchNode = parseCatch();
           if (match (FINALLY)) {
               finallyNode = parseFinally();
           }
       } else if (match (FINALLY)){
           finallyNode = parseFinally();
           if (match (CATCH)) {
               catchNode = parseCatch();
           }
       }
       if (catchNode == NULL && finallyNode == NULL) {
          error ("catch or finally clause expected for try block") ;
       }
       if (finallyNode == NULL) {
           node->right = catchNode ;
       } else if (catchNode == NULL) {
           node->right = new Node (this, FINALLY, finallyNode) ;
       } else {
           node->right = new Node (this, SEMICOLON, catchNode, finallyNode);
       }
       break ;
       }

    case THROW: {
       nextToken() ;
       node = new Node (this,THROW) ;
       node->left = expression() ;
       break ;
       }

    case OPERATOR:
        if (currentClass == NULL) {
            error ("Operators can only be defined within a class") ;
        }
        goto getblock ;
        
    case THREAD:
        if (!(properties & MULTITHREADED)) {
            error ("Threading has been disabled for this application") ;
            break ;
        }
        // fall through

    case CLASS: 
    case FUNCTION:
    case MONITOR:
    case TINTERFACE:
    //case PACKAGE:
    getblock:
        node = getBlock (command, isstatic, issync, annotations) ;
        break ;

    case PACKAGE: {
        nextToken() ;
        string name ;
        if (currentToken != IDENTIFIER) {
            error ("Identifier expected for package name") ;
            name = "$unknown" ;
        } else {
            name = spelling ;
        }
        node = parsePackage (name, isstatic, issync) ;
        break ;
        }
        

    case IMPORT: {		// import is followed by a series of identifiers separated by dots
        skipSpaces() ;
        string name = "" ;
        bool instring = false ;
        for (;;) {
           if (*ch == '"') {
               if (instring) {
                   name += *ch++ ;		// end of string
                   break ;
               } else {
                   instring = true ;
               }
           } else if (*ch == ';') {
               if (!instring) {		// semicolon outside string
                   break ;
               }
           } else if (*ch == 0) {
               break ;
           } else if (!instring && isspace(*ch)) {	// any space outside string 
               break ;
           }
           name += *ch++ ;
        }

        std::vector<ImportFile> files ;
        expandImportName (name, files) ;
        char savedline[4096] ;
        char *savedch ;

        if (files.size() == 0) {
            error ("Unable to open import file %s", name.c_str()) ;
        } else {

            // for a local import (this is a global variable), we just import directly into the current tree
            if (localimport) {
                strcpy (savedline, line) ;
                savedch = ch ;
                for (int i = 0 ; i < files.size() ; i++) {
                    if (files[i].inzip) {
                        string &zipname = files[i].name ;
                        zip::ZipEntry *entry = systemZip->open (zipname.str) ;
                        if (entry == NULL) {
                           throw "Can't reopen zip file entry" ;
                        }
#if defined(_CC_GCC) && __GNUC__ == 2
                        std::strstream *zs = entry->getStream() ;
#else
                        std::stringstream *zs = entry->getStream() ;
#endif
                        if (properties & VERBOSE) {
                            std::cout << "Opening (zip) import file " << zipname << '\n' ;
                        }

                        Context ctx (*zs, name, 0, currentContext) ;
                        currentContext = &ctx ;
                        void *dbgctx = debugger->pushFile (name) ;
                        node = parseStream (*zs) ;
                        debugger->popFile (dbgctx) ;
                        currentContext = ctx.getPrevious() ;

                        delete zs ;
                    } else {
                        if (files[i].name.find (OS::libraryExt) != std::string::npos
#if defined (_OS_MACOSX) 
                || files[i].name.find (OS::libraryExt2) != std::string::npos
#endif
                        ) {   // shared library
                            if (!OS::libraryLoaded (files[i].name.str)) {
                                void *handle = OS::openLibrary (files[i].name.c_str()) ;
                                if (handle == NULL) {
                                    error ("Unable to reopen shared library: %s", OS::libraryError()) ;
                                } else {
                                    libraries.push_back (files[i].name) ;		// full path to library
                                }
                            }
                        } else {
                            string name = files[i].name ;
                            name = lockFilename (this, new string(name)) ;
                            std::ifstream in (name.c_str()) ;
                            if (!in) {
                                throw "Unable to reopen import file" ;
                            } else {
                                if (properties & VERBOSE) {
                                    std::cout << "Opening import file " << name << '\n' ;
                                }
                                Context ctx (in, name, 0, currentContext) ;
                                currentContext = &ctx ;
                                void *dbgctx = debugger->pushFile (name) ;
                                node = parseStream (in) ;
                                debugger->popFile (dbgctx) ;
                                currentContext = ctx.getPrevious() ;

                                in.close() ;
                            }
                        }
                    }
                }
                // restore input
                ch = savedch ;
                strcpy (line, savedline) ;
            } else {
                strcpy (savedline, line) ;
                savedch = ch ;
                for (int i = 0 ; i < files.size() ; i++) {
                    if (mainPackage->imports.count (files[i].id) == 0) {
                        mainPackage->imports.insert (files[i].id) ;
                        if (files[i].inzip) {
                            string &zipname = files[i].name ;
                            zip::ZipEntry *entry = systemZip->open (zipname.str) ;
                            if (entry == NULL) {
                               throw "Can't reopen zip file entry" ;
                            }
#if defined(_CC_GCC) && __GNUC__ == 2
                            std::strstream *zs = entry->getStream() ;
#else
                            std::stringstream *zs = entry->getStream() ;
#endif
                            if (properties & VERBOSE) {
                                std::cout << "Opening (zip) import file " << zipname << '\n' ;
                            }
                            node = importStream (zipname, zipname, *zs, "zip:" + zipname) ;
                            delete zs ;
                        } else {
                            if (files[i].name.find (OS::libraryExt) != std::string::npos
#if defined (_OS_MACOSX) 
                    || files[i].name.find (OS::libraryExt2) != std::string::npos
#endif
                    ) {   // shared library
                                if (!OS::libraryLoaded (files[i].name.str)) {
                                    void *handle = OS::openLibrary (files[i].name.c_str()) ;
                                    if (handle == NULL) {
                                        error ("Unable to reopen shared library: %s", OS::libraryError()) ;
                                    } else {
                                        libraries.push_back (files[i].name) ;		// full path to library
                                    }
                                }
#ifdef JAVA
                            } else if (javaEnabled && files[i].name.find (".class") != std::string::npos) {   // java class
                                char classname[1024] ;
                                strcpy (classname, files[i].name.c_str()) ;
                                java::vm->findAndLoadClass (classname) ;		// can't pass c_str() directly as it is const
#endif
                            } else {
                                string &name = files[i].name ;
                                std::ifstream in (name.c_str()) ;
                                if (!in) {
                                    throw "Unable to reopen import file" ;
                                } else {
                                    if (properties & VERBOSE) {
                                        std::cout << "Opening import file " << name << '\n' ;
                                    }
                                    node = importStream (name, name, in, name) ;
                                    in.close() ;
                                }
                            }
                        }
                    }
                }
                // restore input
                ch = savedch ;
                strcpy (line, savedline) ;
            }
        }
        nextToken() ;
        break ;
        }


    // access control specifier
    case PUBLIC:
    case PRIVATE:
    case PROTECTED:
        nextToken() ;
        if (!match (COLON)) {                   // C++ style?
            switch (currentToken) {
            case VAR:
            case TCONST: 
            case GENERIC:
            case FUNCTION:
            case THREAD:
            case CLASS:
            case MONITOR:
            case PACKAGE:
            case ENUM:
            case MACRO:
            case NATIVE:
            case STATIC:
            case SYNCHRONIZED:
            case OPERATOR:
            case TINTERFACE:
                break ;
            default:
                error ("Expected a declaration after access mode") ;
            }
            node = parseCommand (command, false, false) ;
        } else {                                // Java style
            currentAccessMode = command ;
            oldaccess = command ;			// stop it being reset at end of function
        }
        break ;

    // static.  This can either be a static initializer or a static
    // block declaration
    case STATIC: {
        Block *old = currentMajorScope ;
        int oldscopelevel = currentScopeLevel ;
        currentMajorScope = mainPackage ;		// put statics in main
        currentScopeLevel = 0 ;
        nextToken() ;

        // check for static initializer
        if (currentToken == LBRACE) {
            nextToken() ;
            node = parseStatementSequence (NULL) ;
            needbrack (RBRACE) ;
        } else {
            switch (currentToken) {
            case VAR:
            case TCONST: 
            case GENERIC:
            case FUNCTION:
            case THREAD:
            case CLASS:
            case TINTERFACE:
            case MONITOR:
            case PACKAGE:
            case ENUM:
            case OPERATOR:
            case PUBLIC:
            case PRIVATE:
            case PROTECTED:
                break ;
            default:
                error ("Expected a declaration after static") ;
            }
            node = parseCommand (access, true, issync) ;
        }
        currentMajorScope->body = new Node (this,SEMICOLON, currentMajorScope->body, node) ;
        currentMajorScope = old ;
        currentAccessMode = oldaccess ;
        currentScopeLevel = oldscopelevel ;
        return NULL ;
        break ;
        }

    // Java style synchronized
    case SYNCHRONIZED:
        nextToken() ;
        if (match (LBRACK)) {
            node = expression() ;
            needbrack (RBRACK) ;
            Node *state = parseStatement() ;
            node = new Node (this, SYNCHRONIZED, node, state) ;
        } else {
            node = parseCommand (access, isstatic, true) ;
        }
        break ;

    // enumeration
    case ENUM: {
        node = getBlock (ENUM, false, false, annotations) ;
        break ;
        }
                
    // foreign sequence.  This is typically handled by the worker
    case FOREIGN: {
#if defined(_CC_GCC) && __GNUC__ == 2
        std::strstream *s = new std::strstream ;
#else
        std::stringstream *s = new std::stringstream ;
#endif
        node = new Node (this,s, &currentContext->getFilename(), currentContext->getLineNumber()) ;
        nextToken() ;
        needbrack (LBRACE) ;
        copyToEnd (*s) ;
        break ;
        }

    // using declaration
    case USING:
        nextToken() ;
        parseUsing() ;
        break ;
        

    // block extension
    case EXTEND: {
        nextToken() ;
        Token oldAccessMode = currentAccessMode ;
        currentAccessMode = PRIVATE ; 
       
        if (currentToken == IDENTIFIER) {                       // need an identifier
            string name = spelling ;
            string tagname = "." + name ;
            Tag *tag = findTag (tagname) ;
            if (tag == NULL) {
                error ("Can't extend %s, it doesn't exist", spelling) ;
                skipToEnd() ;
            } else {
                nextToken() ;
                extendBlock (tag->block) ;
            }
        } else {
            error ("extend requires an identifier") ;
        }
        break ;
        }

    // delete
    case TDELETE: {
        nextToken() ;
        Node *expr = single_expression() ;
        //convertAssign (expr) ;				// this behaves like an assignment wrt [] and . ops
        node = new Node (this, TDELETE, expr) ;
        break ;
        }


    // native functions
    case NATIVE: {
        nextToken() ;
        match (FUNCTION) ;              // allow 'native function'
        if (currentToken == IDENTIFIER) {
            string name = spelling ;
            Variable *var ;
            var = findTopScopeVariable (name) ;
            if (var != NULL && ((var->flags & VAR_PREDEFINED) == 0)) {
                error ("Native function name %s already has a body defined", name.c_str()) ;
                break ;
            }
            if (var != NULL) {
                var->flags &= ~VAR_PREDEFINED ;
            }
            nextToken() ;

            int np = currentClass != NULL ;					// number of parameters (1 if in a class)
            int mask = 0 ;

            // collect the arguments
            if (match (LBRACK)) {
                while (currentToken != RBRACK) {
                    if (match (VAR)) {
                        mask |= (1 << np) ;             // reference parameter, set bit in mask
                    }
                    if (match (IDENTIFIER)) {
                        np++ ;
                    } else if (match (ELLIPSIS)) {
                        np = -np ;                      // variable args, -ve number of args
                        break ;
                    }
                    if (!match (COMMA)) {
                        break ;
                    }
                }
                needbrack (RBRACK) ;
            }

            // look for the native function as a symbol in the program
            string nativename = "Aikido__" + name ;
            const char *symname = nativename.c_str() ;
            void *sym = OS::findSymbol (symname) ;
            if (sym != NULL) {
                NativeFunction::FuncPtr p = (NativeFunction::FuncPtr)sym ;
                node = addNativeFunction (name, np, p, currentScopeLevel + 1, currentScope, currentClass, mask) ;
            } else {
                sym = OS::findSymbol (name.c_str()) ;	
                if (sym != NULL) {
                    NativeFunction::FuncPtr p = (NativeFunction::FuncPtr)sym ;
                    node = addNativeFunction (name, np, p, currentScopeLevel + 1, currentScope, currentClass, mask, true) ;
                } else {
                    error ("Unable to find native function %s in any shared library", name.c_str()) ;
                }
            }
        } else {
            error ("Native function name expected") ;
        }
        break ;
        }
    } 		// end switch

    currentAccessMode = oldaccess ;

    return node ;
}

Node *Aikido::parseCatch() {
       needbrack (LBRACK) ;
       if (currentToken == TIN) {
           strcpy (spelling, "in") ;
           currentToken = IDENTIFIER ;
       }

       if (currentToken == IDENTIFIER) {
           Scope *scope = new Scope (this, currentScopeLevel, currentScope, currentMajorScope) ;
           currentScope->addChild (scope) ;
           pushScope (scope) ;
           Variable *var = addVariable (spelling) ;
           var->flags |= VAR_GENERIC ;
           Node *varnode = new Node (this,IDENTIFIER, var) ;
           nextToken() ;
           Node *catchNode = new Node (this,CATCH, varnode) ;
           needbrack (RBRACK) ;

           catchNode->right = parseStatement() ;

           catchNode->scope = scope ;
           popScope() ;
           return catchNode;
       } else {
           error ("Badly formed catch clause") ;
           return NULL;
       }
}

Node *Aikido::parseFinally() {
    return parseStatement();
}


//
// import the file if it exists.  Return true if it was imported.  Any code in the
// import file is appended to the mainpackage body
//

bool Aikido::importFile (const string &name) {
    for (int i = 0 ; i < importpaths.size() ; i++) {
#if defined(_OS_WINDOWS)
        string path = importpaths[i] + "\\" + name ;
#else
        string path = importpaths[i] + "/" + name ;
#endif
        struct stat s ;
        if (OS::fileStats (path.c_str(), &s)) {
#if defined(_OS_WINDOWS)
            __int64 id = OS::uniqueId (&s) ;
#else
            long long id = OS::uniqueId (&s) ;
#endif
            if (mainPackage->imports.count (id) == 0) {
                mainPackage->imports.insert (id) ;
                std::ifstream in (path.c_str()) ;
                if (!in) {
                    throw "Unable to reopen import file" ;
                } else {
                    if (properties & VERBOSE) {
                        std::cout << "Opening import file " << name << '\n' ;
                    }
                    Node *node = importStream (path, name, in, name) ;
                    in.close() ;
                    if (node != NULL) {
                        mainPackage->body = new Node (this, SEMICOLON, mainPackage->body, node) ;
                    }
                }
            } 
            return true ;
        }
    }
    return false ;
}


//
// append a file to the vector if it exists
//
bool Aikido::appendIfExists (const string &name, std::vector<ImportFile> &result) {
    struct stat s ;
    if (OS::fileStats (name.c_str(), &s)) {
#if defined(_OS_WINDOWS)
        __int64 id = OS::uniqueId (&s) ;
#else
        long long id = OS::uniqueId (&s) ;
#endif
        for (int i = 0 ; i < result.size() ; i++) {
            if (result[i].id == id) {
                return true ;		// duplicate
            }
        }
        ImportFile f (name, id) ;
        result.push_back (f) ;
        return true ;
    }
    return false ;
}

//
// given the parameter to an import statement, expand it into a set of filenames
// that are valid and can be opened.  The import name can be:
//
// 1. a quoted string containing a set of characters.  The characters may contain
//    wildcards.  This string is either a file that is rooted in the file system 
//    of is appended to the importpaths elements to form the filename
//
// 2. an unquoted string of identifiers separated by .  We form this into a path
//    and look for .aikido and .so files for each importpath element.   We may have
//    wildcards in 

//

void Aikido::expandImportName (string &name, std::vector<ImportFile> &result) {
#if defined(_OS_WINDOWS)
    const char *fs = "\\" ;
#else
    const char *fs = "/" ;
#endif
    if (name[0] == '"') {
        string filename = "" ;
        bool wildcard = false ;
#if defined(_OS_WINDOWS)
        bool rooted = name[2] == ':' && name[3] == '\\' ;
#else
        bool rooted = name[1] == '/' ;
#endif
        for (int i = 1 ; i < name.size() ; i++) {
            if (name[i] == '"') {
                break ;
            }
            if (name[i] == '*' || name[i] == '?' || name[i] == '[') {
                wildcard = true ;
            }
            filename += name[i] ;
        }
        if (rooted) {
            if (wildcard) {
                OS::Expansion x ;
                if (OS::expandFileName (filename.c_str(), x)) {
                    for (int i = 0 ; i < x.names.size() ; i++) {
                        appendIfExists (x.names[i], result) ;
                    }
                    OS::freeExpansion (x) ;
                }
            } else {
                appendIfExists (filename, result) ;
            }
        } else {		// !rooted
            if (wildcard) {
                OS::Expansion x ;
                for (int i = 0 ; i < importpaths.size() ; i++) {
                    string path = importpaths[i] + fs + filename ;
                    if (OS::expandFileName (path.c_str(), x)) {
                        for (int i = 0 ; i < x.names.size() ; i++) {
                            appendIfExists (x.names[i], result) ;
                        }
                        OS::freeExpansion (x) ;
                    }
                }
            } else {
                for (int i = 0 ; i < importpaths.size() ; i++) {
                    string path = importpaths[i] + fs + filename ;
                    bool done = appendIfExists (path, result) ;
                    if (done) {
                        break ;
                    }
                }
            }
        }
    } else {			// not quoted name
        string newname = name ;
        bool wildcard = false ;
        for (int i = 0 ; i < newname.size() ; i++) {
            if (newname[i] == '.') {
                newname.str[i] = '/' ;		// can't assign to newname[] directly
            } else if (newname[i] == '*' || newname[i] == '?' || newname[i] == '[') {
                wildcard = true ;
            }
        }
        if (wildcard) {
            OS::Expansion x ;
            for (int i = 0 ; i < importpaths.size() ; i++) {
                string path = importpaths[i] + fs + newname + ".aikido" ;
                if (OS::expandFileName (path.c_str(), x)) {
                    for (int i = 0 ; i < x.names.size() ; i++) {
                        appendIfExists (x.names[i], result) ;
                    }
                    OS::freeExpansion (x) ;
                }
                // look for .dwn (legacy)
                path = importpaths[i] + fs + newname + ".dwn" ;
                if (OS::expandFileName (path.c_str(), x)) {
                    for (int i = 0 ; i < x.names.size() ; i++) {
                        appendIfExists (x.names[i], result) ;
                    }
                    OS::freeExpansion (x) ;
                }
#if defined (_OS_MACOSX)
                path = importpaths[i] + fs + newname + OS::libraryExt2 ;
                if (OS::expandFileName (path.c_str(), x)) {
                    for (int i = 0 ; i < x.names.size() ; i++) {
                        appendIfExists (x.names[i], result) ;
                    }
                    OS::freeExpansion (x) ;
                }
#endif
                path = importpaths[i] + fs + newname + OS::libraryExt ;
                if (OS::expandFileName (path.c_str(), x)) {
                    for (int i = 0 ; i < x.names.size() ; i++) {
                        appendIfExists (x.names[i], result) ;
                    }
                    OS::freeExpansion (x) ;
                }
            }
        } else {
            // look for file in system zip (XXX: other zip files too?)
            if (systemZip != NULL) {
                string zipname = newname + ".aikido" ;
                zip::ZipEntry *entry = systemZip->open (zipname.str) ;
                if (entry != NULL) {
#if defined(_OS_WINDOWS)
                    __int64 id = entry->uniqueId() ;
#else
                    long long id = entry->uniqueId() ;
#endif
                    delete entry ;
                    for (int i = 0 ; i < result.size() ; i++) {
                        if (result[i].id == id) {
                            return ;		// duplicate
                        }
                    }
                    ImportFile f (zipname, id, true) ;
                    result.push_back (f) ;
                    return ;
                }
            }
            for (int i = 0 ; i < importpaths.size() ; i++) {
                string path = importpaths[i] + fs + newname ;
                bool done = appendIfExists (path + ".aikido", result) ;
                if (!done) {
                    done = appendIfExists (path + ".dwn", result) ;             // legacy
                }
#if defined (_OS_MACOSX)
                if (!done) {
                    done = appendIfExists (path + OS::libraryExt2, result) ;
                }
#endif
                if (!done) {
                    done = appendIfExists (path + OS::libraryExt, result) ;
                }
#ifdef JAVA
                if (javaEnabled && !done) {
                    appendIfExists (path + ".class", result) ;
                }
#endif
                if (done) {
                    break ;
                }
            }
        }
    }
}

// import a file.  Importing is done to the main package but it
// must be arranged so that all the code of the imports is
// executed before any user code.  This is because the import
// may be between a package and an extension of the same
// package.  Because the extension is added to the original
// package location, the import code will not have been
// executed before the extension if the import code was
// inserted in line.
//
// Although this returns a Node *, this is a lecacy as the
// code is added to the 'importedTree' tree and then appended
// at the end of the parse


Node *Aikido::importStream (string fullfilename, string fn, std::istream &in, string dbgname) {
    Block *oldblock = currentMajorScope ;
    Scope *oldscope = currentScope ;
    int oldlevel = currentScopeLevel ;
    Class *oldclass = currentClass ;

    currentMajorScope = mainPackage ;
    currentScope = mainPackage ;
    currentScopeLevel = 0 ;
    currentClass = NULL ;

    Context ctx (in, fn, 0, currentContext) ;
    currentContext = &ctx ;
    void *dbgctx = debugger->pushFile (dbgname) ;
    Node *extension = parseStream (in) ;
    debugger->popFile (dbgctx) ;
    currentContext = ctx.getPrevious() ;
    currentMajorScope = oldblock ;
    currentScope = oldscope ;
    currentScopeLevel = oldlevel ;
    currentClass = oldclass ;
    
    importedTree = new Node (this, SEMICOLON, importedTree, extension) ;
    return NULL ;
}


// travserse down the package chains adding the named ones as siblings to the current block

void Aikido::parseUsing() {
    for (;;) {				// for each identseq, identseq
        if (currentToken != IDENTIFIER) {
            error ("Bad using statement, identifier expected") ;
            nextToken() ;
            continue ;
        }
        string name = spelling ;
        Package *package = NULL ;
        for (;;) {
            Scope::PackageMap::iterator i ;
            // first time in, find the package in the scope chain
            if (package == NULL) {
                package = findPackage (name) ;
                if (package == NULL) {
                    error ("Undefined package %s", name.c_str()) ;
                    goto nextpackage ;
                }
            } else {			// we're inside a package
                int n ;
                Package *p = package->findPackage (name, n) ;
                if (p == NULL) {
                    break ;
                }
                if (n > 1) {
                    error ("Ambiguous use of package identifier %s, I can find %d of them", name.c_str(), n) ;
                }
                package = p ;
            }
            nextToken() ;			// get possible .
            if (!(currentToken == IDENTIFIER && spelling[0] == '.' && spelling[1] == 0)) {
                break ;
            }
            nextToken() ;			// skip .
            if (currentToken == IDENTIFIER) {	// token should be token after dot
                name = spelling ;
            } else {
                error ("Badly formed package identifier") ;
                goto nextpackage ;
            }
        }
        currentScope->siblings.push_back (package) ;

     nextpackage:
        if (!match (COMMA)) {
            break ;
        }
    }
}


//
// extract an operator name
//

string Aikido::getOperatorName() {
    string name = string ("operator") ;
    if (isOverloadableOperator (currentToken)) {
        switch (currentToken) {
        case LSQUARE:
            nextToken() ;
            nextToken() ;
            name += "[]" ;
            break ;
        case LBRACK:
            nextToken() ;
            nextToken() ;
            name += "()" ;
            break ;
        case SIZEOF:
            nextToken() ;
            name += " sizeof" ;
            break ;
        case TYPEOF:
            nextToken() ;
            name += " typeof" ;
            break ;
        case FOREACH:
            nextToken() ;
            name += " foreach" ;
            break ;
        case TIN:
            nextToken() ;
            name += " in" ;
            break ;
        case CAST:
            nextToken() ;
            name += " cast" ;
            break ;
        default:
            name += findOperator(currentToken) ;
            nextToken() ;
            break ;
        }
        return name ;
    } else if (currentToken == IDENTIFIER && spelling[0] == '.' && spelling[1] == '\0') {
        nextToken();
        return name + ".";
    } else {
        error ("Illegal operator overload") ;
        skipToEnd() ;
        return "" ;
    }
}

//
// a package is being declared.  The parameter is the name of the
// package.  This may be followed by a series of package names
// preceeded by dots.  A package is different from a class in
// that it is automatically extended.  So a declaration of
// a package with the same name as one already declared results
// in the declared package being extended.
//
// Package subpackage names (after a dot) are added as a package 
// within the package being created or extended.
//
// NB.  If a package body contains an import statement that includes
//      a file the defines the same package then the original
//      package will not yet have any code for the body.  In this
//      case we need to extend the package but deal with the
//      case where there is no body code
//
// NB. The code to solve the above note doesn't work so I've
//     resorted to giving an error instead (might be lifted in the
//     future)

Node *Aikido::parsePackage (string name, bool isstatic, bool issync) {
    nextToken() ;
    string tagname = "." + name ;
    Tag *tag = (Tag*)findTopScopeVariable (tagname) ;		// check if the package already exists
    Variable *var ;
    bool extend = true ;

    InterpretedBlock *block ;
    if (tag == NULL) {
        block = new Package (this, name, currentScopeLevel/* + 1*/, currentScope) ;

        // add to package map in current scope
        currentScope->packages[name] = static_cast<Package*>(block) ;

        var = new Variable (name, currentMajorScope->stacksize++) ;
        var->setAccess (currentAccessMode) ;
        var->flags |= VAR_CONST ;				// package variables are constant
        tag = new Tag (tagname, block, var) ;
        tag->setAccess (currentAccessMode) ;
        tag->flags |= VAR_CONST ;
        extend = false ;
        debugger->registerPackageBlock (block, currentContext->getFilename(), currentContext->getLineNumber()) ;
    } else {
        var = tag->var ;
        block = (InterpretedBlock*)tag->block ;
    }
    Node *node = NULL ;
    Block *oldscope = currentMajorScope ;
    int oldScopeLevel = currentScopeLevel ;
    Scope *oldCurrentScope = currentScope ;
    Class *oldclass = currentClass ;

    if (currentToken == IDENTIFIER && spelling[0] == '.') {		// any further packages?
        nextToken() ;
        if (currentToken != IDENTIFIER) {
            error ("Identifier for package name expected") ;
            name = "$unknown" ;
        }
        name = spelling ;
        if (!extend) {				// package doesn't exist, create an empty one
            node = getBlockContents (PACKAGE, tag, var, block, isstatic, issync) ;
        }
        //currentScopeLevel = block->level ;			// move to level of block
        //currentMajorScope = block ;		// to this block
        currentScope = block ;
        currentClass = (Class *)block ;
        Node *extension = parsePackage (name, isstatic, issync) ;		// get extension code for this package
        if (numErrors == 0 && extension != NULL) {
            std::vector<Parameter*> newparas ;				// empty
            Extension *x = codegen->generateExtension (extension, newparas) ;
            deleteTree (extension) ;
            block->code->append (block, (properties & DEBUG) ? debugger : NULL , x) ;
            delete x ;
        }
    } else {								// end of list
        if (extend) {
            if (block->code == NULL) {
                 error ("Limitation: Cannot import a package extension inside the same package body") ;
                 node = getBlockContents (PACKAGE, tag, var, block, isstatic, issync) ;
            } else {
                extendBlock (block) ;
            }
        } else {
            //currentScopeLevel++ ;		// move down one level
            node = getBlockContents (PACKAGE, tag, var, block, isstatic, issync) ;
        }
    }
    currentMajorScope = oldscope ;
    currentScopeLevel = oldScopeLevel ;
    currentScope = oldCurrentScope ;
    currentClass = oldclass ;
    return node ;
}

// extend the given block

void Aikido::extendBlock (Block *block) {
    Block *oldscope = currentMajorScope ;
    Class *oldcls = currentClass ;
    Function *oldfunc = currentFunction ;
    
    int oldScopeLevel = currentScopeLevel ;
        
    currentScopeLevel = block->level ;
    
    switch (block->type) {
    case T_THREAD:
        currentFunction = (Function *)block ;
        currentClass = NULL ;
        break ;
    
    case T_FUNCTION:
        currentFunction = (Function *)block ;
        currentClass = NULL ;
        break ;
    
    case T_CLASS:
    case T_INTERFACE:
        currentClass = (Class *)block ;
        break ;
    
    case T_MONITOR:
        currentClass = (Class *)block ;
        break ;
    
    case T_PACKAGE:
        currentClass = (Class *)block ;
        break ;
    }

    Scope *oldCurrentScope = currentScope ;
    if (block->type != T_ENUM) {
        if (block->type != T_PACKAGE) {
            currentMajorScope = block ;
        } else {
        currentMajorScope = block->parentBlock ;	// packages are in the parent of the block
        }
        currentScope = block ;
    } else {
        currentMajorScope = block->parentBlock ;	// enums are in the parent of the block
        currentScope = (Scope*)block->parentBlock ;
    }

    nfuncs++ ;
    int oldsize = block->stacksize ;
    
    std::vector<Parameter*> newparas ;

    if (match (LBRACK)) {				// any parameters?
        if (block->type == T_ENUM || block->type == T_PACKAGE) {
            error ("No extension parameters allowed") ;
        }
        InterpretedBlock *iblock = (InterpretedBlock*)block ;
        while (currentToken != RBRACK) {
            Token access = PRIVATE ;
            switch (currentToken) {
            case PRIVATE:
            case PUBLIC:
            case PROTECTED:
                access = currentToken ;
                nextToken() ;
                break ;
            }
            if (currentToken == TIN) {
               strcpy (spelling, "in") ;
               currentToken = IDENTIFIER ;
            }

            if (currentToken == IDENTIFIER || currentToken == TCONST || currentToken == GENERIC) {
                string pname ;
                bool isconst = false ;
                bool isgeneric = false ;
                if (currentToken == GENERIC || currentToken == TCONST) {
                    if (currentToken == TCONST) {
                        isconst = true ;
                    } else {
                        isgeneric = true ;
                    }
                    nextToken() ;
                    if (currentToken != IDENTIFIER) {
                        error ("Identifier expected after 'const' or 'generic'") ;
                        pname = "$unknown" ;
                    } else {
                        pname = spelling ;
                    }
                } else {
                    pname = spelling ;
                }
                Variable *p = findTopScopeVariable (pname) ;
                Parameter *para = NULL ;
                if (p == NULL) {
                    para = addParameter (pname) ;
                    para->setAccess (access) ;
                    iblock->parameters.push_back (para) ;
                    newparas.push_back (para) ;
                    if (isconst) {       
                        para->flags |= VAR_CONST ;
                    }
                    if (isgeneric) {       
                        para->flags |= VAR_GENERIC ;
                    }
                } else {
                    error ("Multiple definition of extension parameter %s", spelling) ;
                }
                nextToken() ;
                if (match (COLON)) {
                    Node *typevalue = postfixExpression() ;
                    if (para != NULL) {
                        para->setType (typevalue) ;
                    }
                } 
                if (match (ASSIGN)) {
                    Node *defvalue = single_expression() ;
                    if (para != NULL) {
                        para->setDefault (defvalue) ;
                    }
                } else {
                    error ("Must provide default values for extension parameters") ;
                }
            } else if (currentToken == VAR) {			// "variable" parameter
                nextToken() ;
                string pname ;
                if (currentToken == TIN) {
                   strcpy (spelling, "in") ;
                   currentToken = IDENTIFIER ;
                }

                if (currentToken != IDENTIFIER) {
                    error ("Identifier expected for variable parameter name") ;
                    pname = "$unknown" ;
                } else {
                    pname = spelling ;
                }
                Variable *p = findTopScopeVariable (pname) ;
                Reference *para = NULL ;
                if (p == NULL) {
                    para = addReference (pname) ;
                    para->setAccess (access) ;
                    iblock->parameters.push_back (para) ;
                    newparas.push_back (para) ;
                } else {
                    error ("Multiple definition of extension parameter %s", spelling) ;
                }
                nextToken() ;
                if (match (COLON)) {
                    Node *typevalue = postfixExpression() ;
                    if (para != NULL) {
                        para->setType (typevalue) ;
                    }
                } 
                if (match (ASSIGN)) {
                    Node *defvalue = single_expression() ;
                    if (para != NULL) {
                        para->setDefault (defvalue) ;
                    }
                } else {
                    error ("Must provide default values for extension parameters") ;
                }
            } else if (currentToken == ELLIPSIS) {
                nextToken() ;
                if (!(block->flags & BLOCK_VARARGS)) {
                    block->varargs = addVariable ("args") ;
                    block->flags |= BLOCK_VARARGS ;
                    break ;
                }
            } else {
                error ("Extension parameter name expected") ;
            }
            if (!isComma()) {
                break ;
            }
        }
        needbrack (RBRACK) ;
    }
    
    // any body to do?
    if (match (LBRACE)) {
        Node *extension = NULL ;
        if (block->type == T_ENUM) {
            extension = parseEnum ((Enum*)block) ;
        } else if (block->type == T_INTERFACE) {
            parseInterfaceBody ((Interface*)block) ;
            extension = NULL ;
        } else {
            extension = parseStatementSequence (block) ;
            needbrack (RBRACE) ;
        }
    
        if (numErrors == 0 && extension != NULL || newparas.size() != 0) {
            Extension *x = codegen->generateExtension (extension, newparas) ;
            deleteTree (extension) ;
            block->code->append (block, (properties & DEBUG) ? debugger : NULL , x) ;
            delete x ;
        }
    }
 
    nfuncs-- ;
    currentScope = oldCurrentScope ;
    
    int delta = block->stacksize - oldsize ;
    std::list<Tag*>::iterator s ;
    for (s = block->children.begin() ; s != block->children.end() ; s++) {
        (*s)->block->moveMembers (delta) ;
    }
     
    currentMajorScope = oldscope ;
    switch (block->type) {
    case T_CLASS:
    case T_INTERFACE:
    case T_MONITOR:
    case T_PACKAGE: ;
    case T_ENUM: ;
        currentClass = oldcls ;
        break ;
    case T_FUNCTION:
    case T_THREAD:
        currentClass = oldcls ;
        currentFunction = oldfunc ;
        break ;
    }
    currentScopeLevel = oldScopeLevel ;
}


// get the contents of a block
Node *Aikido::getBlockContents (Token command, Tag *tag, Variable *var, InterpretedBlock *block, bool isstatic, bool issync) {
    bool wasPredefined = (tag->flags & VAR_PREDEFINED) ;
    tag->flags &= ~VAR_PREDEFINED ;
    tag->var->flags &= ~VAR_PREDEFINED ;

    Class *oldcls = currentClass ;
    Block *oldscope = currentMajorScope ;
    Function *oldfunc = currentFunction ;
    StackFrame *oldstack = currentStack ;
    Node *body ;
    const char *blockname = blockName (command) ;
    Node *node = NULL ;
    currentStack = NULL ;

    // create the body root node
    switch (command) {
    case THREAD:
        currentFunction = (Function *)block ;
        block->parentClass = currentClass ;
        currentClass = NULL ;
        body = new Node (this,command, Value ((Thread*)block)) ;
        break ;

    case FUNCTION:
    case OPERATOR:
        block->parentClass = currentClass ;
        currentFunction = (Function *)block ;
        currentClass = NULL ;
        body = new Node (this,command, Value ((Function*)block)) ;
        break ;

    case CLASS:
        block->parentClass = currentClass ;
        currentClass = (Class *)block ;
        body = new Node (this,command, Value ((Class*)block)) ;
        break ;

    case TINTERFACE:
        block->parentClass = currentClass ;
        currentClass = (Class *)block ;
        body = new Node (this,command, Value ((Interface*)block)) ;
        break ;

    case ANNOTATE:
        block->parentClass = currentClass ;
        currentClass = (Class *)block ;
        body = new Node (this,TINTERFACE, Value ((Interface*)block)) ;
        break ;

    case PACKAGE:
        block->parentClass = currentClass ;
        currentClass = (Class *)block ;
        body = new Node (this,command, Value ((Package*)block)) ;
        break ;
    
    case ENUM:
        block->parentClass = currentClass ;
        currentClass = NULL ;
        body = new Node (this, command, Value ((Enum*)block)) ;
        break ;

    case MONITOR:
        block->parentClass = currentClass ;
        currentClass = (Class *)block ;
        body = new Node (this,command, Value ((MonitorBlock*)block)) ;
        break ;
    }

    block->setParentBlock (currentMajorScope) ;
    
    // set the current scope
    Scope *oldCurrentScope = currentScope ;
    if (command != ENUM) {			// enum consts are in parent block scope
        if (command != PACKAGE) {		// packages don't change major scope
            currentMajorScope = block ;
        }
        currentScope = block ;
    }

    // add "this" if necessary
    if (command == FUNCTION || command == THREAD || command == OPERATOR) {
        if (!isstatic && block->parentClass != NULL) {		// inside a class: add this as first para
            Parameter *thisvar = addParameter ("this") ;
            thisvar->setAccess (PROTECTED) ;
            block->parameters.push_back (thisvar) ;
        }
    }

    // parse parameterized types
    if (match (LESS)) {
        if (command != CLASS && command != MONITOR) {
            error ("Only classes can have parameterized types");
        }

        while (currentToken != GREATER) {
            if (currentToken == TIN) {
               strcpy (spelling, "in") ;
               currentToken = IDENTIFIER ;
            }

            if (currentToken == IDENTIFIER) {
                string pname = spelling; 
                Variable *p = findTopScopeVariable (pname) ;
                Parameter *para = NULL ;
                if (p == NULL) {
                    para = addParameter (pname) ;
                    para->setAccess (PRIVATE) ;
                    block->types.push_back (para) ;
                } else {
                    error ("Multiple definition of %s parameterized type %s", blockname, spelling) ;
                }
                nextToken() ;
            } else {
                error ("Expected an identifier for a parameterized type name");
            }
            if (!match (COMMA)) {
                break;
            }
        }  
        needbrack (GREATER);

    }

    // parse the arguments
    if (match (LBRACK)) {
        if (command == PACKAGE) {
            error ("Packages can't have parameters, use a class instead") ;
        }
        if (command == ENUM) {
            error ("Enums can't have parameters - it doesn't make sense") ;
        }
        AnnotationList annotations;
        while (currentToken != RBRACK) {
            Token access = PRIVATE ;

            if (currentToken == ANNOTATE) {
                nextToken();
                annotations = parseAnnotationSequence();
            }

            switch (currentToken) {
            case PRIVATE:
            case PUBLIC:
            case PROTECTED:
                access = currentToken ;
                nextToken() ;
                break ;
            }
            if (currentToken == TIN) {
               strcpy (spelling, "in") ;
               currentToken = IDENTIFIER ;
            }

            if (currentToken == IDENTIFIER || currentToken == TCONST || currentToken == GENERIC) {
                string pname ;
                bool isconst = false ;
                bool isgeneric = false ;
                if (currentToken == GENERIC || currentToken == TCONST) {
                    if (currentToken == GENERIC) {
                        isgeneric = true ;
                    } else {
                        isconst = true ;
                    }
                    nextToken() ;
                    if (currentToken != IDENTIFIER) {
                        error ("Identifier expected after 'const' or 'generic'") ;
                        pname = "$unknown" ;
                    } else {
                        pname = spelling ;
                    }
                } else {
                    pname = spelling ;
                }
                Variable *p = findTopScopeVariable (pname) ;
                Parameter *para = NULL ;
                if (p == NULL) {
                    para = addParameter (pname) ;
                    para->setAccess (access) ;
                    block->parameters.push_back (para) ;
                    if (isconst) {       
                        para->flags |= VAR_CONST ;
                    }
                    if (isgeneric) {       
                        para->flags |= VAR_GENERIC ;
                    }
                    para->annotations = annotations;
                    annotations.clear();
                } else {
                    error ("Multiple definition of %s parameter %s", blockname, spelling) ;
                }
                nextToken() ;
                if (match (COLON)) {
                    Node *typevalue = postfixExpression() ;
                    if (para != NULL) {
                        para->setType (typevalue) ;
                    }
                   
                }
                if (match (ASSIGN)) {
                    Node *defvalue = single_expression() ;
                    if (para != NULL) {
                        para->setDefault (defvalue) ;
                    }
                    
                }
            } else if (currentToken == VAR) {           // reference parameter?
                nextToken() ;
                string pname ;
                if (currentToken == TIN) {
                   strcpy (spelling, "in") ;
                   currentToken = IDENTIFIER ;
                }

                if (currentToken != IDENTIFIER) {
                    error ("Identifier expected for variable parameter name") ;
                    pname = "$unknown" ;
                } else {
                    pname = spelling ;
                }
                Variable *p = findTopScopeVariable (pname) ;
                Reference *para = NULL ;
                if (p == NULL) {
                    para = addReference (pname) ;
                    para->setAccess (access) ;
                    para->annotations = annotations;
                    annotations.clear();
                    block->parameters.push_back (para) ;
                } else {
                    error ("Multiple definition of %s parameter %s", blockname, spelling) ;
                }
                nextToken() ;
                if (match (COLON)) {
                    Node *typevalue = postfixExpression() ;
                    if (para != NULL) {
                        para->setType (typevalue) ;
                    }
                   
                }
                if (match (ASSIGN)) {
                    Node *defvalue = single_expression() ;
                    if (para != NULL) {
                        para->setDefault (defvalue) ;
                    }
                    
                }
            } else if (currentToken == ELLIPSIS) {
                nextToken() ;
                block->varargs = addVariable ("args") ;
                block->varargs->setAccess (access) ;
                block->flags |= BLOCK_VARARGS ;
                break ;
            } else {
                error ("%s parameter name expected", blockname) ;
            }
            if (!isComma()) {
                break ;
            }
        }
        needbrack (RBRACK) ;
    }

    // for functions we can have a function return type expression
    if (match (COLON)) {
        if (block->type != T_FUNCTION) {
            error ("Only functions can have a return type") ;
            postfixExpression()	;		// skip expression
        } else {
            Node *t = postfixExpression() ;
            Function *f = (Function*)block ;
            f->returnType = t ;
        }
    }
 

    if (command == MONITOR) {		// for a monitor, add the wait, notify and notifyAll funcs
        Token old = currentAccessMode ;
        currentAccessMode = PUBLIC ;
        block->body = new Node (this,SEMICOLON, block->body, addNativeFunction (string ("wait"), 1, Aikido__monitorWait, currentScopeLevel, currentScope, currentClass)) ;
        block->body = new Node (this,SEMICOLON, block->body, addNativeFunction (string ("timewait"), 2, Aikido__monitorTimewait, currentScopeLevel, currentScope, currentClass)) ;
        block->body = new Node (this,SEMICOLON, block->body, addNativeFunction (string ("notify"), 1, Aikido__monitorNotify, currentScopeLevel, currentScope, currentClass)) ;
        block->body = new Node (this,SEMICOLON, block->body, addNativeFunction (string ("notifyAll"), 1, Aikido__monitorNotifyAll, currentScopeLevel, currentScope, currentClass)) ;
        currentAccessMode = old ;
    } else if (command == THREAD) {			// threads get their own input and output streams
        Token old = currentAccessMode ;
        currentAccessMode = PUBLIC ;
        Thread *thread = static_cast<Thread*>(block) ;
        thread->input = addVariable ("input") ;
        thread->output = addVariable ("output") ;
        currentAccessMode = old ;
    }

    // look for 'extends' and handle it
    Tag *super = NULL ;
    Variable *supervar ;
    std::vector<Node *> superargs ;
    std::vector<Node *> supertypes ;
    bool typesdefined = false;
    int superlevel ;
    if (match (EXTENDS)) {			// any superclass
       if (currentToken == IDENTIFIER) {
           string supername = spelling ;
           nextToken() ;
           if (currentToken == IDENTIFIER && spelling[0] == '.') {
               super = findPackageTag (supername) ;
           } else {
               super = findTag ("." + supername) ;
           }
           if (super == NULL) {
               error ("Undefined super %s %s", blockname, supername.c_str()) ;
           } else {
               if (super->flags & VAR_PREDEFINED) {
                   error ("Can't inherit from %s, it's not fully defined", supername.c_str()) ;
               }
               supervar = super->var ;
               if (supervar->flags & VAR_STATIC) {
                   superlevel = currentScopeLevel ;
               } else {
                   superlevel = currentScopeLevel - super->block->parentScope->level ;
               }
               if (super->block->type == T_INTERFACE && command != TINTERFACE) {
                   error ("Cannot derive from an interface, you must implement it instead") ;
               }
               if (command == TINTERFACE && super->block->type != T_INTERFACE) {
                   error ("Interfaces can only be derived from other interfaces") ;
               }
               Scope::VarMap::iterator var ;
               for (var = block->variables.begin() ; var != block->variables.end() ; var++) {
                   (*var).second->incOffset (super->block->stacksize) ;
               }
               block->stacksize += super->block->stacksize ;

               if (super->block->type == T_FUNCTION || super->block->type == T_THREAD) {
                   if (!(super->var->flags & VAR_STATIC) && super->block->parentClass != NULL) {
                       int level ;
                       string thisname = "this" ;
                       Variable *thisvar = findVariable (thisname, level, VAR_PUBLIC | VAR_PROTECTED) ;
                       if (thisvar != NULL) {
                           Node *n = new Node (this, IDENTIFIER, thisvar) ;
                           n->value.integer = level ;
                           superargs.push_back (n) ;			// add this as first arg to super call
                       }
                   }
               }

               block->superblock = super ;

         
               // parameterized types
               if (match (LESS)) {
                   typesdefined = true;
                   while (currentToken != GREATER) {
                       Node *e = primaryExpression() ;
                       supertypes.push_back (e) ;
                       if (!isComma()) {
                           break ;
                       }
                   }
                   needbrack (GREATER) ;
               }
         
               if (match (LBRACK)) {
                   while (currentToken != RBRACK) {
                       Node *e = single_expression() ;
                       superargs.push_back (e) ;
                       if (!isComma()) {
                           break ;
                       }
                   }
                   needbrack (RBRACK) ;
               }
               super->block->children.push_back (tag) ;		// add as child to the super block
           }
       } else {
           error ("Super-%s name expected", blockname) ;
       }
    } else {				// no super class, add the this pointer
        if (command == CLASS || command == PACKAGE || command == MONITOR) {		 // push up all the variables
            Scope::VarMap::iterator var ;
            for (var = block->variables.begin() ; var != block->variables.end() ; var++) {
                (*var).second->incOffset (1) ;
            }
 
            block->stacksize++ ;
 
            Variable *thisvar = new Variable ("this", 0) ;
            thisvar->setAccess (PROTECTED) ;
            block->variables["this"] = thisvar ;
            thisvar->oldValue.ptr = (void*)block ;
        }
    }

    // add our interfaces
    if (match (IMPLEMENTS)) {
        bool skip = false ;
        if (command == TINTERFACE) {
            error ("Interfaces can't implement interfaces") ;
            skip = true ;
        }
        while (currentToken == IDENTIFIER) {
            if (!skip) {
                string name = spelling ;
                nextToken() ;
                Tag *iface = NULL ;
                if (currentToken == IDENTIFIER && spelling[0] == '.') {
                    iface = findPackageTag (name) ;
                } else {
                    iface = findTag ("." + name) ;
                }
                if (iface == NULL) {
                    error ("Undefined interface %s", name.c_str()) ;
                } else if (iface->block->type != T_INTERFACE) {
                    error ("Can only implement interfaces") ;
                } else {
                    if (iface->flags & VAR_PREDEFINED) {
                        error ("Can't implement %s, it's not fully defined", name.c_str()) ;
                    }
                    block->interfaces.push_back (iface) ;
                }
            }
            if (!match (COMMA)) {
                break ;
            }
        }
    }

    if (!wasPredefined) {
        // insert the var and tag now before the body is parsed
        if (oldstack != NULL ) {
            oldstack->block->insertVariable (var) ;
            oldstack->block->insertVariable (tag) ;
        } else {
            oldCurrentScope->insertVariable (var) ;
            oldCurrentScope->insertVariable (tag) ;
            addVariableName (var->name, oldCurrentScope, var) ;
        }
    }
 
    if (match (LBRACE)) {			// body is optional
        nfuncs++ ;
        if (command == ENUM) {
            block->body = new Node (this, SEMICOLON, block->body, parseEnum ((Enum*)block)) ;
        } else if (command == TINTERFACE) {
            parseInterfaceBody ((Interface*)block) ;
            block->body = NULL ;
        } else {
            block->body = new Node (this,SEMICOLON, block->body, parseStatementSequence(block)) ;
            needbrack (RBRACE) ;

            // now check for @Property annotations
            for (Scope::VarMap::iterator vari = block->variables.begin() ; vari != block->variables.end() ; vari++) {
                string name = vari->first;
                if (name[0] != '.') {
                    // not a tag
                    Variable *var = vari->second;
                    Annotation *propann = var->getAnnotation ("@Property");
                    if (propann != NULL) {
                        generateProperty (var, block, propann);
                    }
                }
            }

        }
        nfuncs-- ;
    } else {
        block->body = NULL ;
    }


 
 //
 // check if the block has a finalize method 
 //
 
      
     Scope *finscope ;
     Tag *ftag = (Tag*)block->findVariable (string (".finalize"), finscope, VAR_ACCESSFULL, NULL, NULL) ;
     if (ftag != NULL) {
         if (ftag->block->type == T_FUNCTION) {
             Function *func = (Function *)ftag->block ;
             if (!func->isNative()) {
                 InterpretedBlock *iblock = (InterpretedBlock*)func ;
                 int np = iblock->parentClass == NULL ? 0 : 1 ;		// if in a class, then count this para
                 if (iblock->parameters.size() == np) {
                     block->finalize = iblock ;
                 }
             }
         }
     }
 
    // restore saved globals
    currentStack = oldstack ;
    currentScope = oldCurrentScope ;
    currentMajorScope = oldscope ;
    switch (command) {
    case CLASS:
    case TINTERFACE:
    case MONITOR:
    case PACKAGE: 
    case ENUM: 
        currentClass = oldcls ;
        break ;
    case FUNCTION:
    case OPERATOR:
    case THREAD:
        currentClass = oldcls ;
        currentFunction = oldfunc ;
        break ;
    }
 
    if (command != ENUM && super != NULL) {
        Node *supernode = new Node (this,IDENTIFIER, supervar) ;
        supernode->value.integer = superlevel ;
        Node *supercall;
        if (typesdefined) { 
            supercall = new Node (this,SUPERCALL, supernode, new Node (this, SEMICOLON, new Node (this, DEFTYPES, supertypes), new Node (this, FUNCPARAS, superargs))) ;
        } else {
            supercall = new Node (this,SUPERCALL, supernode, new Node (this, FUNCPARAS, superargs)) ;
        }
        Node *newbody = new Node (this,SEMICOLON, supercall, block->body) ;
        block->body = newbody ;
    }
 
    if (numErrors == 0) {
        block->code = codegen->generate (block) ;
        if (properties & DEBUG) {
            block->code->registerInstructions (debugger) ;
        }
        // delete the parse tree
        deleteTree (block->body) ;
    }

    block->body = NULL ;

    node = new Node (this,IDENTIFIER, var) ;
    node = new Node (this,CONSTASSIGN, node, body) ;
 
 //
 // if this definition overrides one in a base class, the we overwrite the base class's
 // variable with this one with an assignment at runtime
 //
    if (currentMajorScope->superblock != NULL) {
        Variable *override = currentMajorScope->superblock->block->checkOverride (var) ;
        if (override != NULL) {
            Node *n1 = new Node (this, IDENTIFIER, override) ;
            Node *n2 = new Node (this, IDENTIFIER, var) ;
            Node *n3 = new Node (this, OVERASSIGN, n1, n2) ;
            node = new Node (this, SEMICOLON, node, n3) ;
        }
    }

 
 //
 // for an enum, we copy the body of the enum to the current tree.  It is indirected
 // through an ENUMBLOCK node to prevent it being added to the sequence and therefore
 // rendering it non-extensible
 //

 // this if for packages too as they are in the parent's scope
     
    if (command == ENUM || command == PACKAGE) {
        Node *eid = new Node (this, IDENTIFIER, var) ;
        Node *eb = new Node (this, ENUMBLOCK, eid) ;
        node = new Node (this, SEMICOLON, node, eb) ;
    }

// check that we implement all the public functions of our interfaces

    if (block->interfaces.size() != 0) {
        std::list<Tag*>::iterator i = block->interfaces.begin() ;
        while (i != block->interfaces.end()) {
            Interface *iface = static_cast<Interface*>((*i)->block) ;
            iface->checkBlock (block) ;
            i++ ;
        }
    }

    return node ;
}

// generate getter and setter functions for a property
void Aikido::generateProperty (Variable *var, Block *block, Annotation *propann) {
    std::string name = var->name.str;
    std::string gettername = std::string("get") + (char)toupper(name[0]) + name.substr(1);
    std::string settername = std::string("set") + (char)toupper(name[0]) + name.substr(1);
    std::string isname = std::string("is") + (char)toupper(name[0]) + name.substr(1);

 
    bool get = true;
    bool set = true;
    bool boolean = false;

    Annotation::ActualMap::iterator geti = propann->actuals.find("get");
    Annotation::ActualMap::iterator seti = propann->actuals.find("set");
    Annotation::ActualMap::iterator booli = propann->actuals.find("boolean");

    if (geti != propann->actuals.end()) {
        Node *node = geti->second;
        if (node->op == NUMBER) {
            if (node->value.type == T_BOOL) {
                get = node->value.integer != 0;
            } else {
                error ("Expected boolean value for @Property(get=<value>)");
            }
        } else {
            error ("Expected boolean value for @Property(get=<value>)");
        }
    }

    if (seti != propann->actuals.end()) {
        Node *node = seti->second;
        if (node->op == NUMBER) {
            if (node->value.type == T_BOOL) {
                set = node->value.integer != 0;
            } else {
                error ("Expected boolean value for @Property(set=<value>)");
            }
        } else {
            error ("Expected boolean value for @Property(set=<value>)");
        }
    }

    if (booli != propann->actuals.end()) {
        Node *node = booli->second;
        if (node->op == NUMBER) {
            if (node->value.type == T_BOOL) {
                boolean = node->value.integer != 0;
            } else {
                error ("Expected boolean value for @Property(boolean=<value>)");
            }
        } else {
            error ("Expected boolean value for @Property(boolean=<value>)");
        }
    }

    if (!set && !get && !boolean) {
        error ("There is no point in having a property without getter and setter");
        return;
    }
    std::stringstream codestream;
    if (get) {
        if (boolean) {
            codestream << "@AllowOverride public function " << isname << " { return " << name << " != 0 ? true : false}\n";
        } else {
            codestream << "@AllowOverride public function " << gettername << " { return " << name << "}\n";
        }
    }
    if (set) {
        codestream << "public function " << settername << "(v) { this." << name << " = v}\n";
    }

    // save the whole Aikido object state
    unsigned int size = sizeof(*this);
    void *savedmem = malloc (size);
    if (savedmem == NULL) {
        error ("No memory to save state");
        return;
    }

    memcpy (savedmem, this, size);
    Context ctx (codestream, currentContext) ;
    currentContext = &ctx ;
    Node *node = parseStream (codestream) ;
    currentContext = ctx.getPrevious() ;

    // restore state
    memcpy (this, savedmem, size);
    free (savedmem);

    block->body = new Node (this,SEMICOLON, block->body, node) ;
}



//
// check a block to make sure it honors the interface contract
//
void Interface::checkBlock (Block *block) {
    Scope::VarMap::iterator iv ;
    for (iv = variables.begin() ; iv != variables.end() ; iv++) {
        Variable *var = (*iv).second ;
        if (var->name[0] != '.') {
           continue ;
        }
        Scope *scope ;
        Tag *tag = (Tag*)block->findVariable (var->name, scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (tag == NULL) {
            p->error ("Block %s does not provide implementation for interface member %s.%s", block->name.c_str(), name.c_str(), var->name.c_str() + 1) ;
        } else {
            Tag *itag = static_cast<Tag*>(var) ;
            if (itag->block->type != tag->block->type) {
                p->error ("Member %s.%s does not match interface", block->name.c_str(), var->name.c_str() + 1) ;
                continue ;
            }
            // check function return types
            if (itag->block->type == T_FUNCTION) {
                Function *ifunc = (Function*)itag->block ;
                Function *vfunc = (Function *)tag->block ;
                if (!Aikido::compareTree (ifunc->returnType, vfunc->returnType)) {
                    p->error ("Function %s.%s has a different return type than interface member %s%s", block->name.c_str(), var->name.c_str() + 1, name.c_str(),var->name.c_str()) ;
                }
            }
            InterpretedBlock *iblock = (InterpretedBlock*)itag->block ;
            InterpretedBlock *vblock = (InterpretedBlock*)tag->block ;
            // check that both or neither are variable arg lists
            if (iblock->flags & BLOCK_VARARGS || vblock->flags & BLOCK_VARARGS) {
                if ((iblock->flags & vblock->flags & BLOCK_VARARGS) == 0) {
                    p->error ("Both member %s.%s and interface member %s%s must have variable parameter list", block->name.c_str(), var->name.c_str() + 1, name.c_str(), var->name.c_str()) ;
                }
            }

            // check there are the same number of parameters
            // both will have "this" parameter, but we don't want to report this
            int inp = iblock->parameters.size()  - 1;
            int vnp = vblock->parameters.size()  - 1;
            if (inp != vnp) {
                p->error ("Member %s.%s has %d parameter%s, interface member %s%s has %d", block->name.c_str(), var->name.c_str() + 1, vnp, (vnp == 1 ? "" : "s"), name.c_str(), var->name.c_str(), inp) ;
                continue ;
            }
            // now check the parameter types and default values
            int nparas = inp + 1 ;                // put "this" parameter back
            for (int i = 0 ; i < nparas ; i++) {
                bool ok = Aikido::compareTree (iblock->parameters[i]->type, vblock->parameters[i]->type) ;
                if (!ok) {
                    p->error ("Member %s.%s, type of parameter '%s' does not match interface member %s%s", block->name.c_str(), var->name.c_str() + 1, vblock->parameters[i]->name.c_str(), name.c_str(), var->name.c_str()) ;
                }
                ok = Aikido::compareTree (iblock->parameters[i]->def, vblock->parameters[i]->def) ;
                if (!ok) {
                    p->error ("Member %s.%s, default value of parameter '%s' does not match interface member %s%s", block->name.c_str(), var->name.c_str() + 1, vblock->parameters[i]->name.c_str(), name.c_str(), var->name.c_str()) ;
                }
            }
        }
    }
}

//
// main block parser
//
Node *Aikido::getBlock (Token command, bool isstatic, bool issync, AnnotationList &annotations) {
    bool predefining = false ;
    Node *node = NULL ;
    InterpretedBlock *block ;
    const char *blockname = blockName (command) ;
    nextToken() ;
    Token optoken = BAD ;
    Scope *oldscope = currentScope ;
    int oldlevel = currentScopeLevel ;

    Token oldAccessMode = currentAccessMode ;
       
    // name can be an operator or an identifier
    if (command == OPERATOR || currentToken == IDENTIFIER) {
        Variable *var = NULL ;
        Class *cls ;
        InterpretedBlock *func ;
        string name ;
        if (command == OPERATOR) {
            optoken = currentToken ;
            if (optoken == IDENTIFIER) {
                optoken = DOT;
            }
            name = getOperatorName() ;
            if (name == "") {
                return NULL ;
            }
        } else {
            name = spelling ;
            nextToken() ;
        }
        
        if (match (ASSIGN)) {		// allow things like function x = y
            if ((var = findTopScopeVariable (name)) == NULL) {
                if (currentStack != NULL) {
                    currentStack->checkCapacity() ;
                    var = new Variable (name, currentStack->block->stacksize++) ;
                    currentStack->block->insertVariable (var) ;
                    addVariableName (name, currentStack->block, var) ;
                    var->setAccess (PUBLIC) ;		
                } else {
                    var = addVariable (name) ;
                }
            } else {
                error ("Multiple definition of %s %s", blockname, name.c_str()) ;
                return NULL ;
            }
            var->annotations = annotations;
            var->flags |= VAR_CONST ;
            if (isstatic) {
                var->flags |= VAR_STATIC ;
            }
            Node *varnode = new Node (this,IDENTIFIER, var) ;
            Node *val = single_expression() ;
            node = new Node (this, CONSTASSIGN, varnode, val) ;
            return node ;
        }
        
        predefining = match (ELLIPSIS) ;

        // the first main at the top level that is not in an import file gets called $main__0

        if (command == FUNCTION && name == "main" && currentScopeLevel == 0) {		// main at top level
            char buf[32] ;
            if (mainFound) {
                sprintf (buf, "$main__%d", ++mainNum) ;		// give it a new name
                name = buf ; 
            } else {
                if (currentContext->getPrevious() != NULL) {		// import file?
                    sprintf (buf, "$main__%d", ++mainNum) ;		// give it a new name
                    name = buf ; 
                } else {
                    name = "$main__0" ;
                    mainFound = true ;
                }
             
            }
        }
        bool redefining = !predefining ;
        var = findTopScopeVariable (name) ;
        if (var != NULL) {
            redefining = true ;
            if (!predefining && ((var->flags & VAR_PREDEFINED) == 0)) {
                string tagname = "." + name ;
                Tag *t = (Tag*)findTopScopeVariable (tagname) ;
                if (t == NULL) {
                    goto declerror ;
                } else {
                    if (t->block->type == T_PACKAGE && command == PACKAGE) {
                    }
                }
	    declerror:
                if (!var->allowsOverride()) {
                    error ("%s name %s already has a body defined", blockname, name.c_str()) ;
                    skipToEnd() ;
                }
            }
        } else {
            if (currentStack != NULL) {
                currentStack->checkCapacity() ;
                var = new Variable (name, currentStack->block->stacksize++) ;
            } else {
                var = new Variable (name, currentMajorScope->stacksize++) ;
            }
            var->setAccess (currentAccessMode) ;
        }
        var->annotations = annotations;
        var->flags |= VAR_CONST ;				// function variables are constant
 
        int oldScopeLevel = currentScopeLevel ;

	// static blocks still need a new scope level
        if (isstatic) {
            var->flags |= VAR_STATIC ;
        } 
        if (command != ENUM) {
            ++currentScopeLevel ;
        }

//
// look for a tag for the block and create one if it is not there
//

        string tagname = (command == ANNOTATE ? ".@" : ".") + name ;
        Tag *tag = (Tag*)findTopScopeVariable (tagname) ;
        
        if (tag == NULL) {
            switch (command) {
            case THREAD:
                block = new Thread (this, name, currentScopeLevel, currentScope) ;
                break ;
    
            case FUNCTION:
            case OPERATOR:
                if (issync && currentClass == NULL) {
                    error ("Cannot synchronize a function outside a class") ;
                }
                block = new InterpretedBlock (this, name, currentScopeLevel, currentScope) ;
                if (command == OPERATOR && currentClass != NULL) {			// currentClass checked to trap error
                    currentClass->flags |= BLOCK_HASOPERATORS ;
                    currentClass->addOperator (optoken, block) ;
                }
                break ;
    
            case CLASS:
                if (issync) {
                    block = new MonitorBlock (this, name, currentScopeLevel, currentScope) ;
                } else {
                    block = new Class (this, name, currentScopeLevel, currentScope) ;
                }
                break ;
    
            case ANNOTATE:
                block = new Interface (this, "@" + name, currentScopeLevel, currentScope) ;
                command = TINTERFACE;
                break ;

            case TINTERFACE:
                block = new Interface (this, name, currentScopeLevel, currentScope) ;
                break ;
    
            case MONITOR:
                block = new MonitorBlock (this, name, currentScopeLevel, currentScope) ;
                block->flags |= BLOCK_SYNCHRONIZED ;
                break ;
    
            case PACKAGE:
                block = new Package (this, name, currentScopeLevel, currentScope) ;
                break ;
    
            case ENUM:
                block = new Enum (this, name, currentScopeLevel, currentScope) ;
                break ;
            }
        } else {
            block = (InterpretedBlock*)tag->block ;
        }

        block->annotations = annotations;

        if (issync) {
            block->flags |= BLOCK_SYNCHRONIZED ;
            if (currentClass != NULL) {
                if (currentClass->type == T_CLASS) {
                    currentClass->type = T_MONITOR ;
                }
            }
        }

        if (isstatic) {
            block->flags |= BLOCK_STATIC ;
        }


        if (tag == NULL) {
            tag = new Tag (tagname, block, var) ;
            //insertVariable (tag) ;
            tag->setAccess (currentAccessMode) ;
            tag->flags |= VAR_CONST ;
        }


        if (predefining) {
            // if we are redefining a block with a body using ... we don't do anything
            if (!redefining) {
                tag->flags |= VAR_PREDEFINED ;
                var->flags |= VAR_PREDEFINED ;
                if (currentStack != NULL) {
                    currentStack->checkCapacity() ;
                    currentStack->block->insertVariable (var) ;
                    addVariableName (var->name, currentStack->block, var) ;
                } else {
                     insertVariable (var) ;
                }
                insertVariable (tag) ;
            }
        } else {
            if (command != ENUM) {			// don't change access mode for enum constants
                currentAccessMode = PRIVATE ; 
            }
            node = getBlockContents (command, tag, var, block, isstatic, issync) ;
        }
        if (!predefining && command == OPERATOR) {
            checkOperatorDefinition (static_cast<InterpretedBlock*>(block)) ;
        }
        if (!predefining && command != ENUM && command != TINTERFACE) {
            debugger->registerBlock (block) ;
        }
        currentScopeLevel = oldScopeLevel ;

        // check for predefined blocks and set the isabstract flag
        Scope::VarMap::iterator i = block->variables.begin() ;
        for (; i != block->variables.end() ; i++) {
            if (i->first[0] == '.') {
                Tag *tag = (Tag*)i->second ;
                if (tag->flags & VAR_PREDEFINED) {
                    block->flags |= BLOCK_ISABSTRACT ;
                    break ;
                }
            }
        
        }

    } else {
        error ("%s name expected", blockname) ;
        nextToken() ;
    }
    currentAccessMode = oldAccessMode ;
    return node ;
}

void Aikido::checkOperatorDefinition (InterpretedBlock *block) {
    int nparas = block->parameters.size() ;
    if (block->name == "operator foreach") { 
        if (nparas < 1 || nparas > 2) {
            error ("Operator foreach needs either 0 or 1 parameters") ;
        }
        if (nparas == 2) {
            if (!block->parameters[1]->isReference()) {
                error ("Parameter for operator foreach must be reference parameter (i.e. operator foreach (var %s))", block->parameters[1]->name.c_str()) ;
            }
        }
    } else if (block->name == "operator in") { 
        if (nparas < 2 || nparas > 3) {
            error ("Operator in needs either 1 or 2 parameters") ;
        }
    } else {
        OpInfoMap::iterator i = opinfo.find (block->name) ;
        if (i != opinfo.end()) {
            if (nparas != i->second) {
                int exp = i->second - 1 ;
                error ("Operator %s needs %d parameter%s", block->name.c_str(),
                           exp, exp == 1? "":"s") ;
            }
        }
    }
}

//
// parse the body of an enum.  This consists of a series of lines, each of which contains
// a comma separated enum const and initialisation expression.  The enum consts are added
// as variables in the current scope and a tree is built to assign the expression values
// to the constants.
//


Node *Aikido::parseEnum (Enum *en) {
   Node *tree = NULL ;
   
   EnumConst *prev = NULL ;
   INTEGER envalue = 0 ;
   if (en->consts.size() > 0) {
       std::vector<EnumConst*>::iterator p = en->consts.end() ;
       p-- ;
       prev = *p ;
       envalue = prev->value + 1;
   } else {
       if (en->superblock != NULL) {
           if (en->superblock->block->type != T_ENUM) {
               error ("Illegal superblock for enum") ;
           } else {
               Enum *pen = static_cast<Enum*>(en->superblock->block) ;
               if (pen->consts.size() > 0) {
                   std::vector<EnumConst*>::iterator p = pen->consts.end() ;
                   p-- ;
                   envalue = (*p)->value + 1 ;
               }
           }
       }
   }
   
   while (currentToken != RBRACE) {
        if (currentToken == TIN) {
           strcpy (spelling, "in") ;
           currentToken = IDENTIFIER ;
        }

       if (currentToken == IDENTIFIER) {
           string name = spelling ;
           Variable *var = findTopScopeVariable (name) ;
           if (var != NULL) {
               error ("Duplicate definition of enum constant %s", spelling) ;
               nextToken() ;
           } else {
               var = new EnumConst (en, name, currentMajorScope->stacksize++) ;
               insertVariable (var) ;
               EnumConst *ec = (EnumConst*)var ;
               en->consts.push_back (ec) ;
               if (prev != NULL) {
                   prev->next = ec ;
               }
               ec->prev = prev ;
               prev = ec ;
               var->flags |= VAR_CONST ;
               nextToken() ;
               if (match (ASSIGN)) {
                   ec->value = constExpression() ;
                   envalue = ec->value + 1;
               } else {
                   ec->value = envalue++ ;
               }
               Node *value = new Node (this, NUMBER, Value (ec)) ;
               Node *vnode = new Node (this, IDENTIFIER, var) ;
               value = new Node (this, CONSTASSIGN, vnode, value) ;
               if (tree != NULL) {
                   value = new Node (this, SEMICOLON, tree, value) ;
               }
               tree = value ;
               if (!match (COMMA)) {
                   break ;
               }
           }
       } else {
           break ;
       }
   }
   needbrack (RBRACE) ;
   return tree ;
}

// parse a sequence of annotations and return a list of them
// the first @ has been absorbed and the current token must be an identifier
AnnotationList Aikido::parseAnnotationSequence() {
    AnnotationList result;

    while (currentToken == IDENTIFIER) {
        std::string annotationname = std::string("@") + spelling;
        std::string tagname = std::string(".@") + spelling;
        Tag *tag = findTag (tagname) ;
        if (tag == NULL) {
            error ("Cannot find annotation @%s", spelling);
            nextToken();
        } else {
            nextToken();
            bool singlearg = false;
            Annotation *ann = new Annotation (this, (Interface*)tag->block);
            result.push_back (ann);

            if (match (LBRACK)) {
                for (;;) {
                    std::string paraname ;
                    Node *value = NULL;
                    if (currentToken == IDENTIFIER) {
                        paraname = spelling;
                        nextToken();
                        if (match (ASSIGN)) {
                            value = single_expression();
                        } else {
                            // 'value' argument
                            Variable *var = new Variable (paraname, 0);
                            value = new Node (this, IDENTIFIER, var);
                            paraname = "value";
                            singlearg = true;
                        }
                    } else {
                        // 'value' argument
                        value = single_expression();
                        paraname = "value";
                        singlearg = true;
                    }

                    // validate that the parameter name exists
                    Scope *scope ;
                    Variable *var = ann->iface->findVariable (paraname, scope, aikido::VAR_PUBLIC, NULL, NULL) ;
                    if (var == NULL) {
                        error ("No such annotation parameter %s.%s", ann->iface->name.c_str(), paraname.c_str());
                    } else {
                        // we have the parameter name and the value
                        ann->addActual (paraname, value);
                    }

                    if (!match(COMMA)) {
                        break;
                    }
                    if (singlearg) {
                        error ("Too many parameters for annotation %s", annotationname.c_str());
                    }
                }
                needbrack (RBRACK);
            }
        }

        // another annotation?
        if (!match (ANNOTATE)) {
            break;
        }
    }
    return result;
}

// parse an annotation definition (like an interface only more limited)
void Aikido::parseAnnotationDefinition (Interface *iface) {
    while (!match (RBRACE)) {
        Tag *tag ;
        InterpretedBlock *block ;
        string name ;
        Token tok ;
        const char *blockname = blockName (currentToken) ;
        switch (tok = currentToken) {
        case FUNCTION:
            nextToken() ;
            if (currentToken == TIN) {
               strcpy (spelling, "in") ;
               currentToken = IDENTIFIER ;
            }

            if (currentToken == IDENTIFIER) {
                name = spelling ;
                nextToken() ;
                block = new InterpretedBlock (this, name, currentScopeLevel, currentScope) ;
            } else {
                error ("Identifier for function name within annotation expected") ;
                nextToken() ;
            }
            break ;
        default:
            error ("Illegal member of annotation; only functions supported") ;
            nextToken() ;
        }
        string tagname = "." + name ;
        Scope *scope ;
        Variable *var = iface->Scope::findVariable (name, scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (var != NULL) {
            error ("Annotation %s already contains %s", iface->name.c_str(), name.c_str()) ;
        } else {
            var = new Variable (name, 0) ;
            var->setAccess (PUBLIC) ;
            tag = new Tag (tagname, block, var) ;
            iface->insertVariable (var) ;
            iface->insertVariable (tag) ;
        }

        Block *oldmajor = currentMajorScope ;
        currentMajorScope = block ;

   
        // optional () for function
        if (match (LBRACK)) {
            needbrack (RBRACK);
        }

        if (match (DEFAULT)) {
            // add one parameter for default value
            Parameter *para = block->addParameter ("default") ;
            block->parameters.push_back (para) ;
            Node *defvalue = single_expression() ;
            para->setDefault (defvalue) ;
        }

        // optional semicolon
        match (SEMICOLON);
    }
}


//
// parse the body of an interface
//
void Aikido::parseInterfaceBody (Interface *iface) {
    if (iface->name[0] == '@') {
        parseAnnotationDefinition (iface);
        return;
    }

    while (!match (RBRACE)) {
        Tag *tag ;
        InterpretedBlock *block ;
        string name ;
        Token tok ;
        const char *blockname = blockName (currentToken) ;
        switch (tok = currentToken) {
        case FUNCTION:
        case THREAD:
        case CLASS:
        case MONITOR:
        case TINTERFACE:
            nextToken() ;
            if (currentToken == TIN) {
               strcpy (spelling, "in") ;
               currentToken = IDENTIFIER ;
            }

            if (currentToken == IDENTIFIER) {
                name = spelling ;
                nextToken() ;
                switch (tok) {
                case FUNCTION:
                    block = new InterpretedBlock (this, name, currentScopeLevel, currentScope) ;
                    break ;
                case THREAD:
                    block = new Thread (this, name, currentScopeLevel, currentScope) ;
                    break ;
                case CLASS:
                    block = new Class (this, name, currentScopeLevel, currentScope) ;
                    break ;
                case MONITOR:
                    block = new MonitorBlock (this, name, currentScopeLevel, currentScope) ;
                    break ;
                case TINTERFACE:
                    block = new Interface (this, name, currentScopeLevel, currentScope) ;
                    break ;
                }
            } else {
                error ("Identifier for block name expected") ;
                nextToken() ;
            }
            break ;
        case OPERATOR:
            nextToken() ;
            name = getOperatorName() ;
            if (name == "") {
                continue ;
            }
            
            break ;
        default:
            error ("Illegal member of interface") ;
            nextToken() ;
        }
        string tagname = "." + name ;
        Scope *scope ;
        Variable *var = iface->Scope::findVariable (name, scope, VAR_ACCESSFULL, NULL, NULL) ;
        if (var != NULL) {
            error ("Interface %s already contains %s", iface->name.c_str(), name.c_str()) ;
        } else {
            var = new Variable (name, 0) ;
            var->setAccess (PUBLIC) ;
            tag = new Tag (tagname, block, var) ;
            iface->insertVariable (var) ;
            iface->insertVariable (tag) ;
        }

        Block *oldmajor = currentMajorScope ;
        currentMajorScope = block ;

        if (tok == FUNCTION || tok == THREAD || tok == OPERATOR) {
            Parameter *thisvar = block->addParameter ("this") ;
            thisvar->setAccess (PROTECTED) ;
            block->parameters.push_back (thisvar) ;
        }

        if (match (LBRACK)) {
            while (currentToken != RBRACK) {
                Token access = PRIVATE ;
                switch (currentToken) {
                case PRIVATE:
                case PUBLIC:
                case PROTECTED:
                    access = currentToken ;
                    nextToken() ;
                    break ;
                }
                if (currentToken == TIN) {
                   strcpy (spelling, "in") ;
                   currentToken = IDENTIFIER ;
                }

                if (currentToken == IDENTIFIER || currentToken == TCONST || currentToken == GENERIC) {
                    string pname ;
                    bool isconst = false ;
                    bool isgeneric = false ;
                    if (currentToken == GENERIC || currentToken == TCONST) {
                        if (currentToken == GENERIC) {
                            isgeneric = true ;
                        } else {
                            isconst = true ;
                        }
                        nextToken() ;
                        if (currentToken != IDENTIFIER) {
                            error ("Identifier expected after 'const' or 'generic'") ;
                            pname = "$unknown" ;
                        } else {
                            pname = spelling ;
                        }
                    } else {
                        pname = spelling ;
                    }
                    Parameter *para = block->addParameter (pname) ;
                    block->parameters.push_back (para) ;
                    if (isconst) {       
                        para->flags |= VAR_CONST ;
                    }
                    if (isgeneric) {       
                        para->flags |= VAR_GENERIC ;
                    }
                    nextToken() ;
                    if (match (COLON)) {
                        Node *typevalue = postfixExpression() ;
                        if (para != NULL) {
                            para->setType (typevalue) ;
                        }
                       
                    }
                    if (match (ASSIGN)) {
                        Node *defvalue = single_expression() ;
                        if (para != NULL) {
                            para->setDefault (defvalue) ;
                        }
                        
                    }
                } else if (currentToken == VAR) {
                    nextToken() ;
                    string pname ;
                    if (currentToken == TIN) {
                       strcpy (spelling, "in") ;
                       currentToken = IDENTIFIER ;
                    }

                    if (currentToken != IDENTIFIER) {
                        error ("Identifier expected for variable parameter name") ;
                        pname = "$unknown" ;
                    } else {
                        pname = spelling ;
                    }
                    Variable *p = findTopScopeVariable (pname) ;
                    Reference *para = NULL ;
                    if (p == NULL) {
                        para = addReference (pname) ;
                        para->setAccess (access) ;
                        block->parameters.push_back (para) ;
                    } else {
                        error ("Multiple definition of %s parameter %s", blockname, spelling) ;
                    }
                    nextToken() ;
                    if (match (COLON)) {
                        Node *typevalue = postfixExpression() ;
                        if (para != NULL) {
                            para->setType (typevalue) ;
                        }
                       
                    }
                    if (match (ASSIGN)) {
                        Node *defvalue = single_expression() ;
                        if (para != NULL) {
                            para->setDefault (defvalue) ;
                        }
                        
                    }
                } else if (currentToken == ELLIPSIS) {
                    nextToken() ;
                    block->flags |= BLOCK_VARARGS ;
                    break ;
                } else {
                    error ("%s parameter name expected", blockname) ;
                }
                if (!isComma()) {
                    break ;
                }
            }
            needbrack (RBRACK) ;
        }
        currentMajorScope = oldmajor ;

        if (match (COLON)) {
            if (tok != FUNCTION) {
                error ("Only functions can have a return type") ;
            }
            Node *r = postfixExpression() ;
            Function *f = (Function*)block ;
            f->returnType = r ;
        }

        // optional semicolon
        match (SEMICOLON);
    }
}

//
// collect the actual arguments from the input and put them in the map
//

void Aikido::collectActualArgs (char *s, Context::ParaMap &map, Macro *macro) {
    std::vector<string> &formal = macro->getParameters() ;
    int nargs = formal.size() ;
    char buf[4096] ;
    char *p ;
    int i ;
    int nactuals = 0 ;
    bool instring = false ;
    while (*s != 0 && isspace (*s)) s++ ;
    if (*s != 0) {
        for (i = 0 ; i < nargs ; i++) {
            p = buf ;
            while (*s != 0 && isspace (*s)) s++ ;
            string actual ;
            string &arg = formal[i] ;
            if (*s == ',') {			// missing parameter?
                if (!macro->getParaDef (arg, actual)) {
                    error ("No value has been given to macro arg %s", arg.c_str()) ;
                    continue ;
                }
                goto addtomap ;
            }
            while (*s != 0 && *s != '\n' && *s != '`') {
                if (p >= (buf + sizeof (buf))) {
                    error ("Actual argument too long") ;
                    p-- ;
                    break ;
                }
                if (*s == '\\') {
                    *p++ = *s++ ;
                    *p = *s ;
                     if (*s == 0 || *s == '\n') {
                         break ;
                     }
                     s++ ;
                     p++ ;
                     continue ;
                }
                if (*s == '"') {
                    if (instring) {
                        instring = false ;
                    } else {
                       instring = true ;
                    }
                    *p++ = *s++ ;
                    continue ;
                }
                if (!instring && (*s == ',' || *s == '{' || *s == ';')) {
                    break ;
                }
                *p++ = *s++ ;
            }
            if (p == buf) {
                break ;
            }
            *p = 0 ;
            actual = buf ;
       addtomap:
            const Context::ParaMap::value_type v (arg, actual) ;
            map.insert (v) ;
            nactuals++ ;
            while (*s != 0 && isspace (*s)) s++ ;
            if (*s != ',') {
                break ;
            }
            s++ ;
        }
    }
    if (instring) {
        error ("Non-terminated string literal in macro arguments for macro %s", macro->getName().c_str()) ;
    }
    while (nactuals < nargs) {
        string arg = formal[nactuals] ;
        string actual ;
        if (!macro->getParaDef (arg, actual)) {
            break ;
        }
        const Context::ParaMap::value_type v (arg, actual) ;
        map.insert (v) ;
        nactuals++ ;
    }
    if (nactuals != nargs || (*s != 0 && *s != '\n' && *s != '{')) {
        error ("Incorrect number of actual arguments for macro %s", macro->getName().c_str()) ;
    }
}

// given a line and its starting character position, replace all the macro parameters
// held in the given map

void Aikido::replaceMacroParameters(char *inch, char *out, Context::ParaMap &map) {
    char newline [1024] ;
    char *s = newline ;
    bool instring = false ;

    while (*inch != 0) {
        if (*inch == '"') {
            if (instring) {
                instring = false ;
            } else {
                instring = true ;
            }
        }
        if (*inch == '$') {
            if (inch[1] == '$') {
                *s++ = '$' ;
                inch += 2 ;
                continue ;
            }
            char pname[256] ;
            char *p = pname ;
            char *och = inch ;
            inch++ ;
            if (*inch == '{') {
                inch++ ;
                while (*inch != '}' && (isalnum (*inch) || *inch == '_')) {
                    *p++ = *inch++ ;
                }
                inch++ ;
            } else {
                while (*inch != '$' && (isalnum (*inch) || *inch == '_')) {
                    *p++ = *inch++ ;
                }
            }
            *p = 0 ;
            //printf ("looking for macro parameter %s\n", pname) ;
            Context::ParaMap::iterator result = map.find (pname) ;
            if (result != map.end()) {
                string &text = (*result).second ;
                //printf ("found it as %s\n", text) ;
                for (int i = 0 ; i < text.length() ; i++) {
                    char c = text[i] ;
                    if (instring && c == '"') {		// escape quotes in a string
                        *s++ = '\\' ;
                    }
                    *s++ = c ;
                }
            } else {
                while (och != inch) {
                    *s++ = *och++ ;
                }
            }
        } else {
            *s++ = *inch++ ;
        }
    }
    *s = 0 ;
    strcpy (out, newline) ;
}



//
// check for the next token being a specific one.  Skips to next token
// if match is found
//


bool Aikido::match (Token tok) {
    if (currentToken == tok) {
        nextToken() ;
        return true ;
    }
    return false ;
}

//
// parser error.
//


void Aikido::error (const char *s,...) {
    char str[1024] ;
    va_list arg ;
    va_start (arg, s) ;
    vsprintf (str, s, arg) ;

    // if we are running then this is from an eval or load function.  We want
    // a runtime error to be generated instead of a parse error
    if (mainStack != NULL) {		
#if defined(_CC_GCC) && __GNUC__ == 2
        std::strstream error ;
#else
        std::stringstream error ;
#endif
        if (currentContext != NULL) {
            if (currentContext->getFilename() == "") {
                error <<  str << std::endl ;
            } else if (!currentContext->hasMacro()) {
                error << '\"' << currentContext->getFilename() << "\", line " 
                    << currentContext->getLineNumber() << ": " << str << std::endl ;
            } else {
                string &macroname = currentContext->getMacroName() ;
                error << "Macro: " << macroname << ", \"" << 
                    currentContext->getFilename() << "\", line " << currentContext->getLineNumber() << ": " << str << std::endl ;
                error << '\t' << line << '\n' ;
            }
        } else {
            error << str << '\n' ;
        }
        throw Exception (string (error.str())) ;
    } else {
        if (!currentContext->hasMacro()) {
            std::cerr << '\"' << currentContext->getFilename() << "\", line " 
        	    << currentContext->getLineNumber() << ": " << str << std::endl ;
        } else {
            string &macroname = currentContext->getMacroName() ;
            std::cerr << "Macro: " << macroname << ", \"" << 
        	    currentContext->getFilename() << "\", line " << currentContext->getLineNumber() << ": " << str << std::endl ;
            std::cerr << '\t' << line << '\n' ;
        }
        numErrors++ ;
        if (numErrors > maxErrors) {
            std::cerr << "Too many errors, goodbye\n" ;
            exit (1) ;
        }
    }
}

// error where filename and lineno are not in currentContext
void Aikido::error (string fn, int lineno, const char *s,...) {
    char str[1024] ;
    va_list arg ;
    va_start (arg, s) ;
    vsprintf (str, s, arg) ;
    std::cerr << '\"' << fn << "\", line " << lineno << ": " << str << std::endl ;
    numErrors++ ;
    if (numErrors > maxErrors) {
        std::cerr << "Too many errors, goodbye\n" ;
        exit (1) ;
    }
}

//
// runtime error.
//


void Aikido::runtimeError (SourceLocation *source, const char *s,...) {
    char str[1024] ;
    va_list arg ;
    va_start (arg, s) ;
    vsprintf (str, s, arg) ;
    numErrors++ ;
#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream error ;
#else
    std::stringstream error ;
#endif
    error << '\"' << source->filename << "\", line " << source->lineno << ": " << str << std::endl ;
    throw Exception (string (error.str())) ;
}

void Aikido::warning (const char *s,...) {
    char str[1024] ;
    va_list arg ;
    va_start (arg, s) ;
    vsprintf (str, s, arg) ;

    if (!currentContext->hasMacro()) {
        std::cerr << '\"' << currentContext->getFilename() << "\", line " 
        	<< currentContext->getLineNumber() << ": warning: " << str << std::endl ;
    } else {
        string &macroname = currentContext->getMacroName() ;
        std::cerr << "Macro: " << macroname << ", \"" << 
        	currentContext->getFilename() << "\", line " << currentContext->getLineNumber() << ": warning: " << str << std::endl ;
        std::cerr << '\t' << line << '\n' ;
    }
}


//
// utility function to get a comma and emit an error if it is not there
//

void Aikido::needcomma() {
    if (currentToken != COMMA) {
        error ("Comma expected") ;
    }
    nextToken() ;
}

//
// need a bracket as the next token.  Skip if there, error otherwise
//

void Aikido::needbrack(Token bracket) {
    if (!match (bracket)) {
        error ("%c expected", bracket == LBRACK ? '(' :
                              bracket == RBRACK ? ')' :
                              bracket == LBRACE ? '{' :
                              bracket == RBRACE ? '}' :
                              bracket == LSQUARE ? '[' :
                              bracket == RSQUARE ? ']' : 'x'
                              ) ;
    }
}


void Aikido::needsemi() {
    if (!match (SEMICOLON)) {
        error ("';' expected") ;
    }
}


//
// is the next token a comma?
//

bool Aikido::isComma() {
    if (currentToken == COMMA) {
        nextToken() ;
        return true ;
    }
    return false ;
}

 
//
// macros
//

Macro::Macro (Aikido *p, int l, Scope *parent, Block *major, string nm, string file, int line) : Scope (p, l, parent, major),
    super (NULL) {
    //copylower (name, nm) ;
    name = nm ;
    source = sourceRegistry.newSourceLocation (file.str, line) ;
    enable() ;
    instanceScope = NULL ;
}

void Macro::instantiate (Scope *inst) {
    instanceScope = inst ;
}

Variable *Macro::findVariable (const string &nm, Scope *&scope, int access, SourceLocation *source, Package **pkg) {
    if (instanceScope == NULL) {
        throw "macro has not been instantiated" ;
    }
    Variable *var = NULL ;
    Scope *s = instanceScope ;
    if (pkg != NULL) {
       *pkg = NULL ;
    }
    while (s != NULL) {
        var = s->findVariable (nm, scope, access, source, pkg) ;
        if (var != NULL) {
           return var ;
        }
        s = s->parentScope ;
    }

    // look at ourselves
    var = Scope::findVariable (nm, scope, access, source, pkg) ;
    if (var != NULL) {
        if (pkg != NULL) {
            *pkg = instancePackage ;
        }
        return var ;
    }

    // search up parent list for var.  If found, set the *pkg to our package
    s = parentScope ;
    while (s != NULL) {
        var = s->findVariable (nm, scope, access, source, pkg) ;
        if (var != NULL) {
            if (pkg != NULL) {
                *pkg = instancePackage ;
            }
            return var ;
        }
        s = s->parentScope ;
    }
    return NULL ;
}



void Macro::addParameter (string pn) {
    string para ;
    //copylower (para, p) ;
    para =  pn ;
    parameters.push_back (para) ;
    //printf ("adding parameter %s to macro %s\n", para, name) ;
}

void Macro::putLine (char *l) {
    //printf ("adding line %s to macro %s\n", l, name) ;
    body.write (l, strlen (l)) ;
    body.put ('\n') ;
}


void Macro::copylower (string &to, string &from) {
    for (int i = 0 ; i < from.length() ; i++) {
        to += tolower (from[i]) ;
    }
}

bool Macro::getParaDef (string pa, string &def) {
    ParaDefMap::iterator s = paraDefs.find (pa) ;
    if (s == paraDefs.end()) {
        return false ;
    }
    def = (*s).second ;
    return true ;
}

//
// source location registry
//

SourceRegistry::SourceRegistry() {
    lastfilename = "" ;
    lastline = -1 ;
    lastlocation = NULL ;
}

SourceRegistry::~SourceRegistry() {
}

SourceLocation *SourceRegistry::newSourceLocation (const std::string &filename, int line) {
    if (lastline == line && lastfilename == filename) {
        return lastlocation ;
    }
    LineMap *m = findFile (filename) ;
    if (m == NULL) {
        m = registerFile (filename) ;
    }

    lastfilename = filename ;
    lastline = line ;

    SourceLocation *loc ;
    LineMap::iterator i = m->find (line) ;
    if (i == m->end()) {
        loc = new SourceLocation (filename.c_str(), line) ;
        lastlocation = loc ;
        (*m)[line] = loc ;
        return loc ;
    }
    lastlocation = i->second ;
    return lastlocation ;
}

void SourceRegistry::removeFile (const std::string &filename){
    FileMap::iterator i = files.find (filename) ;
    if (i != files.end()) {
        LineMap::iterator l = i->second->begin() ;
        while (l != i->second->end()) {
            delete l->second ;
            l++ ;
        }
        files.erase (i) ;
    }
    lastfilename = "" ;
}

SourceRegistry::LineMap *SourceRegistry::findFile (const std::string &filename) {
    FileMap::iterator i = files.find (filename) ;
    if (i != files.end()) {
        return i->second ;
    }
    return NULL ;
}

SourceRegistry::LineMap *SourceRegistry::registerFile (const std::string &filename) {
    LineMap *m = new LineMap() ;
    files[filename] = m ;
    return m ;
}

void SourceRegistry::addLine (const std::string &filename, LineMap *map, int line) {
    SourceLocation *loc = new SourceLocation (filename.c_str(), line) ;
    (*map)[line] = loc ;
}


//
// context management
//


Context::Context (std::istream &str, string &file, int line, Context *prev) : stream (str), filename (file), lineno (line), previous (prev), location (NULL) {
    macro = NULL ;
}

Context::Context (std::istream &str, Context *prev) : stream (str), filename ("unknown"), lineno (0), previous (prev), location (NULL) {
    macro = NULL ;
}

Context::Context (Macro *mac, Context *prev) : stream (mac->getBody()), filename (mac->getFilename()), location (NULL) {
    lineno = mac->getLineNumber() ;
    macro = mac ;
    previous = prev ;
}

Context::Context (Node *n, Context *prev) : stream (*n->stream), filename (n->value.foreign->filename), lineno (n->value.foreign->lineno), location (NULL) {
    macro = NULL ;
    previous = prev ;
}


Context::Context (SourceLocation *s, Stream &str, Context *prev) : stream (str.getInStream()), filename (s->filename), lineno (s->lineno) {
    macro = NULL ;
    previous = prev ;
    location = s ;
}

Context::~Context () {
    if (macro != NULL) {
        macro->enable() ;			// reenable the macro
    }

// now delete all the actual parameters from the map
//    for (ParaMap::iterator s = paraMap.begin() ; s != paraMap.end() ; s++) {
//        delete (*s).second ;
//    }
}



//
// block handling
//

bool Aikido::isBlock() {
    switch (currentToken) {
    case IF:
    case FOR:
    case FOREACH:
    case WHILE:
    case SWITCH:
    case MACRO:
    case CATCH:
    case CLASS:
    case TINTERFACE:
    case MONITOR:
    case FUNCTION:
    case THREAD:
    case PACKAGE:
    case EXTEND:
    case ENUM:
        return true ;
    }
    return false ;
}



//
// skip the input until we find an end, skipping nested blocks
//

void Aikido::skipToEnd() {
    int startLevel = blockLevel++ ;
    std::istream &in = currentContext->getStream() ;
    readLine() ;
    while (!in.eof()) {
        char *s = line ;
        while (*s != 0) {
            if (*s == '{') {
                blockLevel++ ;
            } else if (*s == '}') {
                if (--blockLevel == startLevel) {
                    resetInput() ;
                    return ;
                }
            }
            s++ ;
        }
        readLine (false) ;
    }
}


//
// copy the input to the given stream until we find an end, nested blocks are also copied
//

void Aikido::copyToEnd(std::ostream &out) {
    int startLevel = blockLevel++ ;
    std::istream &in = currentContext->getStream() ;
    readLine(false) ;
    while (!in.eof()) {
        char *s = line ;
        while (*s != 0) {
            if (*s == '{') {
                blockLevel++ ;
            } else if (*s == '}') {
                if (--blockLevel == startLevel) {
                    resetInput() ;
                    return ;
                }
            }
            out.put (*s++) ;
        }
        out.put ('\n') ;
        readLine (false) ;
    }
}

Node::Node (Node *n) {
    *this = *n ;
}

Node *Aikido::copyTree (Node *node) {
    if (node == NULL) {
        return NULL ;
    }
    Node *newnode = new Node (node) ;
    newnode->left = copyTree (node->left) ;
    newnode->right = copyTree (node->right) ;
    return newnode ;
}

void Aikido::convertAssign (Node *left) {
    switch (left->op) {
    case DOT:
        left->op = DOTASSIGN ;
        break ;
    case LSQUARE:
        left->op = ARRAYASSIGN ;
        convertAssign (left->left) ;		// left needs to be assigned to also
        break ;
    case IDENTIFIER:
        break ;
    default:
        error ("Illegal assignment") ;
    }
}

//
// expression handling.  This is a simple recursive descent expression parser
//

Node *Aikido::expression() {
    std::vector<Node*> vec ;
    Node *left = single_expression() ;
    while (match (COMMA)) {
        if (left != NULL) {
            vec.push_back (left) ;
            left = NULL ;
        }
        Node *right = single_expression() ;
        vec.push_back (right) ;
    }
    if (left != NULL) {
        return left ;
    }
    return new Node (this, VECTOR, vec) ;
}

Node *Aikido::single_expression() {
    return assignmentExpression() ;
}

Node *Aikido::assignmentExpression() {
    Node *result, *right ;
    result = streamExpression() ;
    Token tok = currentToken ;
    switch (currentToken) {
    case ASSIGN:
        result = new Node (this, ASSIGN, result) ;
        nextToken() ;
        result->right = assignmentExpression() ;
        break ;

    case PLUSEQ:
    case MINUSEQ:
    case STAREQ:
    case SLASHEQ:
    case PERCENTEQ:
    case LSHIFTEQ:
    case RSHIFTEQ:
    case ZRSHIFTEQ:
    case ANDEQ:
    case OREQ:
    case XOREQ: {
        Node *ass = new Node (this,ASSIGN, result, NULL) ;
        nextToken() ;
        right = assignmentExpression() ;
        Node *copy = copyTree (result) ;
        switch (tok) {
        case PLUSEQ:
            tok = PLUS ;
            break ;
        case MINUSEQ:
            tok = MINUS ;
            break ;
        case STAREQ:
            tok = STAR ;
            break ;
        case SLASHEQ:
            tok = SLASH ;
            break ;
        case PERCENTEQ:
            tok = PERCENT ;
            break ;
        case LSHIFTEQ:
            tok = LSHIFT ;
            break ;
        case RSHIFTEQ:
            tok = RSHIFT ;
            break ;
        case ZRSHIFTEQ:
            tok = ZRSHIFT ;
            break ;
        case ANDEQ:
            tok = AMPERSAND ;
            break ;
        case OREQ:
            tok = BITOR ;
            break ;
        case XOREQ: 
            tok = CARET ;
            break ;
        }
        ass->right = new Node (this, tok, copy, right) ;
        result = ass ;
        break ;
        }
    default:
        return result ;
    }

    checkAssignment (result->left) ;

    //convertAssign (result->left) ;
    return result ;
}

void Aikido::checkAssignment (Node *node) {
    Variable *var ;
    switch (node->op) {
    case IDENTIFIER:
        var = node->var ;
        if (var->flags & VAR_CONST) {
            error ("Cannot assign to %s, it is constant", var->name.c_str()) ;
        }
        break ;
    case LSQUARE: {
        Node *n = node->left ;
        while (n->op == LSQUARE) {
            n = n->left ;
        }
        if (n->op == IDENTIFIER) {
            var = n->var ;
            if (var->flags & VAR_CONST) {
                error ("Cannot assign to subscript of %s, it is constant", var->name.c_str()) ;
            }
        }
        break ;
        }
    case DOT:
        break ;
    case VECTOR: {
        for (unsigned int i = 0 ; i < node->vec.size() ; i++) {
            checkAssignment (node->vec[i]) ;
        }
        break ;
        }
    default:
        error ("Illegal left side of assignment") ;
    }

}

Node *Aikido::streamExpression() {
    Node *result , *right ;
    result = conditionalExpression() ;
    while (match (ARROW)) {
        result = new Node (this,ARROW, result, NULL) ;
        result->right = conditionalExpression() ;
    }
    return result ;
}

Node *Aikido::conditionalExpression() {
    Node *right, *result, *left ;
    result = logicalOrExpression() ;
    if (match (QUESTION)) {
        left = expression() ;
        if (match (COLON)) {
            right = conditionalExpression() ;
        } else {
            error (": missing for ?: operator") ;
            return result ;
        }
        right = new Node (this,COLON, left, right) ;
        result = new Node (this,QUESTION, result, right) ;
    }
    return result ;
}

Node *Aikido::logicalOrExpression() {
    Node *right, *result ;
    result = logicalAndExpression() ;
    for (;;) {
        if (match (LOGOR)) {
            right = logicalAndExpression() ;
            result = new Node (this,LOGOR, result, right) ;
        } else {
            return result ;
        }
    }
}

Node *Aikido::logicalAndExpression() {
    Node *right, *result ;
    result = orExpression() ;
    for (;;) {
        if (match (LOGAND)) {
            right = orExpression() ;
            result = new Node (this,LOGAND, result, right) ;
        } else {
            return result ;
        }
    }
}

Node *Aikido::orExpression() {
    Node *right, *result ;
    result = xorExpression() ;
    for (;;) {
        if (match (BITOR)) {
            right = xorExpression() ;
            result = new Node (this,BITOR, result, right) ;
        } else {
            return result ;
        }
    }
}

Node *Aikido::xorExpression() {
    Node *right, *result ;
    result = andExpression() ;
    for (;;) {
        if (match (CARET)) {
            right = andExpression() ;
            result = new Node (this,CARET, result, right) ;
        } else {
            return result ;
        }
    }
}

Node *Aikido::andExpression() {
    Node *right, *result ;
    result = equalityExpression() ;
    for (;;) {
        if (match (AMPERSAND)) {
            right = equalityExpression() ;
            result = new Node (this,AMPERSAND, result, right) ;
        } else {
            return result ;
        }
    }
}

Node *Aikido::equalityExpression() {
    Node *right, *result ;
    result = relationalExpression() ;
    for (;;) {
        if (match (EQUAL)) {
            right = relationalExpression() ;
            result = new Node (this,EQUAL, result, right) ;
        } else if (match (NOTEQ)) {
            right = relationalExpression() ;
            result = new Node (this,NOTEQ, result, right) ;
        } else {
            return result ;
        }
    }
}

Node *Aikido::relationalExpression() {
    Node *right, *result ;
    result = shiftExpression() ;
    for (;;) {
        if (match (LESS)) {
            right = shiftExpression() ;
            result = new Node (this,LESS, result, right) ;
        } else if (match (GREATER)) {
            right = shiftExpression() ;
            result = new Node (this,GREATER, result, right) ;
        } else if (match (LESSEQ)) {
            right = shiftExpression() ;
            result = new Node (this,LESSEQ, result, right) ;
        } else if (match (GREATEREQ)) {
            right = shiftExpression() ;
            result = new Node (this,GREATEREQ, result, right) ;
        } else if (match (INSTANCEOF)) {
            right = shiftExpression() ;
            result = new Node (this,INSTANCEOF, result, right) ;
        } else if (!newLineRead && match (TIN)) {
            right = rangeExpression() ;
            result = new Node (this,TIN, result, right) ;
        } else {
            return result ;
        }
    }
}

// possible range of e1 .. e2

Node *Aikido::rangeExpression() {
    Node *result, *right ;
    result = shiftExpression() ;
    if (match (ELLIPSIS)) {
        right = shiftExpression() ;
        result = new Node (this, RANGE, result, right) ;
    }
    return result ;
}


Node *Aikido::shiftExpression() {
    Node *right, *result ;
    result = additiveExpression() ;
    for (;;) {
        if (match (LSHIFT)) {
            right = additiveExpression() ;
            result = new Node (this,LSHIFT, result, right) ;
        } else if (match (RSHIFT)) {
            right = additiveExpression() ;
            result = new Node (this,RSHIFT, result, right) ;
        } else if (match (ZRSHIFT)) {
            right = additiveExpression() ;
            result = new Node (this,ZRSHIFT, result, right) ;
        } else {
            return result ;
        }
    }
}

Node *Aikido::additiveExpression() {
    Node *right, *result ;
    result = multExpression() ;
    for (;;) {
        if (!newLineRead && match (PLUS)) {
            right = multExpression() ;
            result = new Node (this,PLUS, result, right) ;
        } else if (!newLineRead && match (MINUS)) {
            right = multExpression() ;
            result = new Node (this,MINUS, result, right) ;
        } else {
            return result ;
        }
    }
}

Node *Aikido::multExpression() {
    Node *right, *result ;
    result = unaryExpression() ;
    for (;;) {
        if (match (STAR)) {
            right = unaryExpression() ;
            result = new Node (this,STAR, result, right) ;
        } else if (match (SLASH)) {
            right = unaryExpression() ;
            result = new Node (this,SLASH, result, right) ;
        } else if (match (PERCENT)) {
            right = unaryExpression() ;
            result = new Node (this,PERCENT, result, right) ;
        } else {
            return result ;
        }
    }
}

Node *Aikido::unaryExpression() {
    Node *result ;
    if (match (PLUS)) {
        return unaryExpression() ;
    } else if (match (MINUS)) {
        result = unaryExpression() ;
        if (result->op == NUMBER) {
            result->value.integer = -result->value.integer ;
            return result ;
        } else if (result->op == FNUMBER) {
            result->value.real = -result->value.real ;
            return result ;
        } else {
            return new Node (this,UMINUS, result) ;
        }
    } else if (match (BANG)) {
        result = unaryExpression() ;
        return new Node (this,BANG, result) ;
    } else if (match (TILDE)) {
        result = unaryExpression() ;
        return new Node (this,TILDE, result) ;
    } else if (match (PLUSPLUS)) {		// result = result + 1
        result = unaryExpression() ;
        Node *n1 = copyTree (result) ;
        Node *one = new Node (this, NUMBER, Value (1)) ;
        Node *n2 = new Node (this, PLUS, n1, one) ;
        //convertAssign (result) ;
        return new Node (this,ASSIGN, result, n2) ;
    } else if (match (MINUSMINUS)) {		// result = result - 1
        result = unaryExpression() ;
        Node *n1 = copyTree (result) ;
        Node *one = new Node (this, NUMBER, Value (1)) ;
        Node *n2 = new Node (this, MINUS, n1, one) ;
        //convertAssign (result) ;
        return new Node (this,ASSIGN, result, n2) ;
    } else if (match (SIZEOF)) {
        if (match (LBRACK)) {
           result = unaryExpression() ;
           needbrack (RBRACK);
        } else {
            result = unaryExpression() ;
        }
        return new Node (this,SIZEOF, result) ;
    } else if (match (TYPEOF)) {
        if (match (LBRACK)) {
           result = unaryExpression() ;
           needbrack (RBRACK);
        } else {
            result = unaryExpression() ;
        }
        return new Node (this,TYPEOF, result) ;
    } else if (match (CAST)) {
        if (!match (LESS)) {
            error ("< required for cast<...>") ;
        } else {
            Node *castexpr = postfixExpression() ;
            if (!match (GREATER)) {
                error ("> required for cast<...>") ;
            }
            needbrack (LBRACK) ;
            Node *e = single_expression() ;
            e =  new Node (this, CAST, castexpr, e) ;
            needbrack (RBRACK) ;
            return e ;
        }
    } else {
        return postfixExpression() ;
    }
}

Node *Aikido::postfixExpression(Node *result) {
    if (result == NULL) {
        result = primaryExpression() ;
    }
    for (;;) {
        if (!newLineRead && match (PLUSPLUS)) {			// (result = result + 1) - 1
            Node *n1 = copyTree (result) ;
            Node *one = new Node (this, NUMBER, Value (1)) ;
            Node *n2 = new Node (this, PLUS, n1, one) ;
            //convertAssign (result) ;
            result = new Node (this,ASSIGN, result, n2) ;
            one = new Node (this, NUMBER, Value (1)) ;
            result = new Node (this, MINUS, result, one) ;
        } else if (!newLineRead && match (MINUSMINUS)) {			// (result = result - 1) + 1
            Node *n1 = copyTree (result) ;
            Node *one = new Node (this, NUMBER, Value (1)) ;
            Node *n2 = new Node (this, MINUS, n1, one) ;
            //convertAssign (result) ;
            result = new Node (this,ASSIGN, result, n2) ;
            one = new Node (this, NUMBER, Value (1)) ;
            result = new Node (this, PLUS, result, one) ;
        } else if (!newLineRead && match (LSQUARE)) {
            Node *sub ;
            if (match (COLON)) {
               sub = single_expression() ;
               Node *zero = new Node (this, NUMBER, Value(0)) ;
               sub = new Node (this,COLON, zero, sub) ;
            } else {
                sub = single_expression() ;
                if (match (COLON)) {
                    if (currentToken == RSQUARE) {
                        Node *nres = copyTree (result) ;
                        Node *sz = new Node (this, SIZEOF, nres) ;
                        Node *one = new Node (this, NUMBER, Value(1)) ;
                        Node *minus = new Node (this, MINUS, sz, one) ;
                        sub = new Node (this,COLON, sub, minus) ;
                    } else {
                        Node *hi = single_expression() ;
                        sub = new Node (this,COLON, sub, hi) ;
                    }
                }
            } 
            result = new Node (this,LSQUARE, result, sub) ;
            needbrack (RSQUARE) ;
        } else if (!newLineRead && (currentToken == DOT || (currentToken == IDENTIFIER && spelling[0] == '.'))) {
            nextToken() ;
            if (currentToken == OPERATOR) {
                nextToken() ;
                string name = getOperatorName() ;
                if (name == "") {
                    result = new Node (this, NUMBER, Value(0)) ;
                } else {
                    Node *sub = new Node (this,MEMBER) ;
                    sub->value.ptr = (void*)findVariableName (name) ;
                    result = new Node (this,DOT, result, sub) ;
                }
            } else if (currentToken != TIN && currentToken != IDENTIFIER) {
                error ("operator . requires an identifier") ;
                nextToken() ;
            } else {
                if (currentToken == TIN) {
                    strcpy (spelling, "in") ;
                }
                Node *sub = new Node (this,MEMBER) ;
                sub->value.ptr = (void*)findVariableName (spelling) ;
                result = new Node (this,DOT, result, sub) ;
                nextToken() ;
            }
        } else if (!newLineRead && match (LBRACK)) {
            std::vector<Node *> actuals ;
            bool passbyname = false;
            bool passbyposition = false;
            bool errorgiven = false;
            while (currentToken != RBRACK) {
                Node *actual = NULL;
                if (currentToken == IDENTIFIER) {
                    skipSpaces();
                    if (*ch == ':') {
                        if (!errorgiven && passbyposition) {
                            error ("Cannot mix pass-by-name and pass-by-position in same call");
                            errorgiven = true;
                        }
                        passbyname = true;
                        Node *actualname = new Node (this, STRING,  Value (new string (spelling)));
                        nextToken();            // identifier
                        nextToken();            // skip colon
                        Node *value = single_expression();
                        actual = new Node (this, ACTUAL, actualname, value);
                    } else {
                        if (!errorgiven && passbyname) {
                            error ("Cannot mix pass-by-name and pass-by-position in same call");
                            errorgiven = true;
                        }
                        passbyposition = true;
                        actual = single_expression() ;
                    }
                } else {
                    if (!errorgiven && passbyname) {
                        error ("Cannot mix pass-by-name and pass-by-position in same call");
                        errorgiven = true;
                    }
                    passbyposition = true;
                    actual = single_expression() ;
                }
                actuals.push_back (actual) ;
                if (!isComma()) {
                    break ;
                }
               
            }
            result = new Node (this,LBRACK, result, new Node (this, FUNCPARAS, actuals)) ;
            if (passbyname) {
                result->flags |= NODE_PASSBYNAME;
            }
            needbrack (RBRACK) ;
        } else {
            break ;
        }
    }
    return result ;
}

// parse the possible followers for 'new'

Node *Aikido::newExpression() {
    Node *result ;
    if (currentToken != IDENTIFIER) {
        error ("new requires an identifier") ;
        result = new Node (this, NUMBER, Value(0)) ;
    } else {
        result = identifierExpression(true) ;
    }
    for (;;) {
        if (!newLineRead && (currentToken == DOT || (currentToken == IDENTIFIER && spelling[0] == '.'))) {
            nextToken() ;
            if (currentToken != IDENTIFIER) {
                error ("operator . requires an identifier") ;
                nextToken() ;
            } else {
                Node *sub = new Node (this,MEMBER) ;
                sub->value.ptr = (void*)findVariableName (spelling) ;
                result = new Node (this,DOT, result, sub) ;
                nextToken() ;
            }
        } else {
            break ;
        }
    }
    return result ;
}


//
// find the identifier inside a set of package names separated by .
// returns a variable for the found identifier.  The name
// is modified to the last element
// currentToken is the .
//

Variable *Aikido::findPackageIdentifier (string &name, int &level) {
    Package *package = NULL ;
    level = 0 ;
    for (;;) {
        Scope::PackageMap::iterator i ;
        // first time in, find the package in the scope chain
        if (package == NULL) {
            package = findPackage (name) ;
            if (package == NULL) {
                return NULL ;
            }
        } else {			// we're inside a package
            int n ;
            Package *p = package->findPackage (name, n) ;
            if (p == NULL) {
                break ;
            }
            if (n > 1) {
                error ("Ambiguous use of package identifier %s, I can find %d of them", name.c_str(), n) ;
            }
            package = p ;
        }
        nextToken() ;			// skip . token
        if (currentToken == TIN) {
           strcpy (spelling, "in") ;
           currentToken = IDENTIFIER ;
        }

        if (currentToken == IDENTIFIER) {	// token should be token after dot
            name = spelling ;
            nextToken() ;			// must have a . after
            if (!(currentToken == IDENTIFIER && spelling[0] == '.' && spelling[1] == 0)) {
                break ;
            }
        } else {
            error ("Badly formed package identifier") ;
            break ;
        }
    }
    Scope *scope ;
    int access = VAR_PUBLIC ;
    if (isParentPackage(package)) {
        access = VAR_ACCESSFULL ;
    }
    Variable *var = package->findVariable (name, scope, access, currentContext->getLocation(), NULL) ;
    if (var != NULL) {
        if (var->flags & VAR_STATIC) {
            level = currentScopeLevel ;
        } else {
            level = currentScopeLevel - scope->level ;
        }
    }
    return var ;
}

Tag *Aikido::findPackageTag (string &name) {
    Package *package = NULL ;
    for (;;) {
        Scope::PackageMap::iterator i ;
        // first time in, find the package in the scope chain
        if (package == NULL) {
            package = findPackage (name) ;
            if (package == NULL) {
                return NULL ;
            }
        } else {			// we're inside a package
            int n ;
            Package *p = package->findPackage (name, n) ;
            if (p == NULL) {
                break ;
            }
            if (n > 1) {
                error ("Ambiguous use of package identifier %s, I can find %d of them", name.c_str(), n) ;
            }
            package = p ;
        }
        nextToken() ;			// skip . token
        if (currentToken == TIN) {
           strcpy (spelling, "in") ;
           currentToken = IDENTIFIER ;
        }

        if (currentToken == IDENTIFIER) {	// token should be token after dot
            name = spelling ;
            nextToken() ;			// must have a . after
            if (!(currentToken == IDENTIFIER && spelling[0] == '.' && spelling[1] == 0)) {
                break ;
            }
        } else {
            error ("Badly formed package identifier") ;
            break ;
        }
    }
    Scope *scope ;
    int access = VAR_PUBLIC ;
    if (isParentPackage(package)) {
        access = VAR_ACCESSFULL ;
    }
    Variable *var = package->findVariable ("." + name, scope, access, currentContext->getLocation(), NULL) ;
    return static_cast<Tag*>(var) ;
}

bool Aikido::isParentPackage (Package *package) {
    Scope *scope = currentMajorScope ;
    while (scope != NULL) {
        if (scope == package) {
            return true ;
        }
        scope = scope->parentScope ;
    }
    return false ;
}

Node *Aikido::identifierExpression(bool fromNew) {
    string name (spelling) ;
    nextToken() ;
    int level ;
    Variable *var = NULL ;
    Package *pkg = NULL ;

    // is next token a .?
    if (currentToken == IDENTIFIER && spelling[0] == '.' && spelling[1] == 0) {
        var = findPackageIdentifier (name, level) ;
    }

    if (var == NULL) {
        var = findVariable (name, level, VAR_ACCESSFULL, NULL, &pkg) ;		// look for variable in current scope chain
    }
    if (var == NULL) {
        if (properties & STATIC_ONLY) {
            error ("Undefined variable %s", name.c_str()) ;
            return new Node (this, NUMBER, Value (0)) ;
        }
        var = findPackageVariable (name, level) ;			// not found, try as a package variable
        if (var == NULL) {
            if (fromNew) {
                var = addVariable (name);
                var->setAccess (PUBLIC) ;		// assume it is public (needed for assembler usage)
                var->flags |= VAR_INVENTED ;
                level = 0;
            } else {
                if (currentToken != ASSIGN && !(properties & INVENT_VARS)) {
                    error ("Undefined variable %s", name.c_str()) ;
                }
                if (properties & INVENT_WARNING) {
                    warning ("Inventing variable: %s", name.c_str()) ;
                }
                if (properties & INVENT_IN_MAIN) {		// add the variable to the top level scope?
                    if (mainStack != NULL) {
                        mainStack->checkCapacity() ;
                    }
                    var = new Variable (name, mainPackage->stacksize++) ;
                    mainPackage->insertVariable (var) ;
                    addVariableName (name, mainPackage, var) ;
                    level = currentStack->block->level ;
                } else {
                    if (currentStack != NULL) {
                        currentStack->checkCapacity() ;
                        var = new Variable (name, currentStack->block->stacksize++) ;
                        currentStack->block->insertVariable (var) ;
                        addVariableName (name, currentStack->block, var) ;
                    } else {
                        var = addVariable (name) ;
                    }
                    level = 0 ;
                }
                var->setAccess (PUBLIC) ;		// assume it is public (needed for assembler usage)
                var->flags |= VAR_INVENTED ;
            }
        }
    }
    if (properties & STATIC_ONLY) {
        if (!(var->flags & VAR_STATIC)) {
            error ("Identifier \"%s\" is required to be static in order to get access from here", name.c_str()) ;
            return new Node (this, NUMBER, Value (0)) ;
        }
    }
    Node *node = new Node (this,IDENTIFIER, var) ;
    node->value.integer = level ;
    return node ;
}

Node *Aikido::primaryExpression() {
    if (match (LBRACK)) {
        // check for (ident)... - C style cast
        if (currentToken == IDENTIFIER) {
            skipSpaces();
            if (*ch == ')') {
                // cast
                Node *type = identifierExpression();
                nextToken();        // know this is close paren

                // now need to check for the possible starters for a unary expression.  Only do this if there is
                // no line feed
                if (!newLineRead) {
                    switch (currentToken) {
                    // unary expression starters
                    case BANG:
                    case TILDE:
                    case PLUSPLUS:
                    case MINUSMINUS:
                    case SIZEOF:
                    case TYPEOF:
                    case CAST:
                    // no PLUS or MINUS because these are also binary operators.  This would make (a) + b ambiguous

                    // primary expression starters
                    case LBRACK:
                    case IDENTIFIER:
                    case BACKTICK:
                    case TIN:
                    case NUMBER:
                    case CHAR:
                    case STRING:
                    case TTRUE:
                    case TFALSE:
                    case KNULL:
                    case LSQUARE:
                    case LBRACE:
                    case NEW:
                    case OPERATOR:
                    case FUNCTION:
                    case THREAD:
                    case CLASS:
                    case TINTERFACE:
                    case PACKAGE:
                    case MONITOR:
                        Node *v = unaryExpression();
                        return new Node (this, CAST, type, v) ;
                    }
                }
                return type;
            } else {
                Node *value = expression();
                needbrack (RBRACK) ;
                return value ;
            }
        } else {
            Node *value = expression() ;
            needbrack (RBRACK) ;
            return value ;
        }
    } else if (match (BACKTICK)) {
        nfuncs++ ;
        nbackticks++ ;
        Node *code = NULL ;
        try {
            code = parseStatementSequence (NULL) ;		// statement sequence in a new scope
        } catch (...) {
            nfuncs-- ;
            nbackticks-- ;
            throw ;
        }
        nfuncs-- ;
        nbackticks-- ;
        if (!match (BACKTICK)) {
            error ("Missing close quote for inline block") ;
        }
        code = new Node (this, COMPOUND, code) ;
        if (code->left != NULL) {
            code->scope = code->left->scope ;
        }
        return new Node (this, INLINE, code) ;
    } else {
        switch (currentToken) {
        case IDENTIFIER: 
            return identifierExpression() ;

        case ANNOTATE: {
            // must be followed by an identifier
            nextToken();
            if (currentToken == IDENTIFIER) {
                Node *node = identifierExpression();
                node->op = ANNOTATE;        // change the IDENTIFIER to ANNOTATE
                return node;
            } else {
                error ("Identifier expected after annotation specifier (@)");
            }
            break;
            }

        case TIN:
            strcpy (spelling, "in") ;
            currentToken = IDENTIFIER ;
            return identifierExpression() ;
            
        case NUMBER: {
            Node *node = new Node (this,NUMBER, Value (number)) ;
            nextToken() ;
            return node ;
            }
        case FNUMBER: {
            Node *node = new Node (this,FNUMBER, Value (fnumber)) ;
            nextToken() ;
            return node ;
            }
         case CHAR: {
             Node *node = new Node (this,CHAR, Value ((char)number)) ;
             nextToken() ;
             return node ;
             }
         case STRING: {
            std::string s = getSpelling();
            Node *node = new Node (this,STRING, Value (new string (s))) ;
            if (longString) {
                // if there is a # sign in the long string, tell interpreter to replace the variables
                if (s.find ("#") != std::string::npos || s.find ("<%") != std::string::npos) {
                    node->flags |= NODE_REPLACEVARS;
                }
            }
            nextToken() ;
            return node ;
            }
        case TTRUE: {
            Node *node = new Node (this,NUMBER, Value (true)) ;
            nextToken() ;
            return node ;
            }
        case TFALSE: {
            Node *node = new Node (this,NUMBER, Value(false)) ;
            nextToken() ;
            return node ;
            }
        case KNULL: {
            Node *node = new Node (this,NUMBER, Value((Object*)NULL)) ;
            nextToken() ;
            return node ;
        }
        case LSQUARE: {			// vector literal
            nextToken() ;
            std::vector<Node*> vec(3) ;
            vec.clear() ;
            while (currentToken != RSQUARE) {
                Node *node = single_expression() ;
                vec.push_back (node) ;
                if (!isComma()) {
                    break ;
                }
            }
            Node *node = new Node (this,VECTOR, vec) ;
            needbrack (RSQUARE) ;
            return node ;
            }
            
        case LBRACE: {			// map literal
            nextToken() ;
            std::vector<Node*> vec ;
            while (currentToken != RBRACE) {
                Node *node = NULL ;
                if (currentToken == IDENTIFIER) {
                    // allow javascript-like identifier as string value for undefined identifiers
                    Package *pkg = NULL ;
                    int level ;
                    string name (spelling) ;
                    Variable *var = findVariable (name, level, VAR_ACCESSFULL, NULL, &pkg) ;
                    if (var == NULL) {
                        node = new Node (this,STRING, Value (new string (spelling))) ;
                        // must be an identifer on its own, not part of an expression
                        nextToken();
                        if (currentToken != ASSIGN && currentToken != COLON) {
                            error ("Undefined identifier %s", name.c_str()) ;
                        }
                    }
                }
                if (node == NULL) {
                    node = conditionalExpression() ;
                }
                // allow both = and : to separate (COLON is more compatible with JavaScript)
                if (match (ASSIGN) || match (COLON)) {
                    Node *expr = conditionalExpression() ;
                    node = new Node (this,ASSIGN, node, expr) ;
                    vec.push_back (node) ;
                } else {
                    error ("Badly formed map literal, no = or :") ;
                } 
                if (!isComma()) {
                    break ;
                }
            }
            Node *node = new Node (this,MAP, vec) ;
            needbrack (RBRACE) ;
            return node ;
            break ;
            }
            
        case NEW: {
            nextToken() ;
            if (currentToken == LSQUARE) {		// array?
                return parseNew() ;
            } else {
                Node *vec = NULL ;
                Node *node = newExpression() ;
                if (currentToken == LSQUARE) {		// new <type> [...]...
                    vec = parseNew() ;		// returns NEWVECTOR tree
                }
                std::vector<Node *> actuals ;
                std::vector<Node *> actualtypes ;
                bool paras = false ;
                bool passbyname = false;
                bool passbyposition = false;
                bool typesdefined = false;

                if (match (LESS)) {     // new xxx <foo,bar>...
                    typesdefined = true;
                    while (currentToken != GREATER) {
                        Node *ex = primaryExpression();
                        actualtypes.push_back (ex);
                        if (!match (COMMA)) {
                            break;
                        }
                    }
                    needbrack (GREATER);
                }

                if (match (LBRACK)) {
                    bool errorgiven = false;
                    while (currentToken != RBRACK) {
                        Node *actual = NULL;
                        if (currentToken == IDENTIFIER) {
                            skipSpaces();
                            if (*ch == ':') {
                                if (!errorgiven && passbyposition) {
                                    error ("Cannot mix pass-by-name and pass-by-position in same call");
                                    errorgiven = true;
                                }
                                passbyname = true;
                                Node *actualname = new Node (this, STRING,  Value (new string (spelling)));
                                nextToken();            // identifier
                                nextToken();            // skip colon
                                Node *value = single_expression();
                                actual = new Node (this, ACTUAL, actualname, value);
                            } else {
                                if (!errorgiven && passbyname) {
                                    error ("Cannot mix pass-by-name and pass-by-position in same call");
                                    errorgiven = true;
                                }
                                passbyposition = true;
                                actual = single_expression() ;
                            }
                        } else {
                            if (!errorgiven && passbyname) {
                                error ("Cannot mix pass-by-name and pass-by-position in same call");
                                errorgiven = true;
                            }
                            passbyposition = true;
                            actual = single_expression() ;
                        }
                        actuals.push_back (actual) ;
                        if (!isComma()) {
                            break ;
                        }
                    }
                    paras = true ;
                }
                if (typesdefined) {
                    node = new Node (this,NEW, node, new Node (this,SEMICOLON, new Node (this,DEFTYPES, actualtypes), new Node (this, FUNCPARAS, actuals))) ;
                } else {
                    node = new Node (this,NEW, node, new Node (this, FUNCPARAS, actuals)) ;
                }
                if (passbyname) {
                    node->flags |= NODE_PASSBYNAME;
                }
                if (vec != NULL) {		// was there a vector?
                    Node *v = vec ;
                    while (v->right != NULL) {	// go to end of tree
                        v = v->right ;
                    }
                    v->right = node ;		// assign right node to NEW expression
                    node = vec ;
                }
                if (paras) {
                    needbrack (RBRACK) ;
                }
                return node ;
            }
            break ;
            }
        
        case OPERATOR: {
            nextToken() ;
            string name = getOperatorName() ;
            if (name == "") {
                return new Node (this,NUMBER, Value (0)) ;
            }
            int level ;
            Variable *var = findVariable (name, level, VAR_ACCESSFULL) ;
            if (var == NULL) {
                var = findPackageVariable (name, level) ;
                if (var == NULL) {
                    error ("Undefined operator function %s", name.c_str()) ;
                }
            }
            Node *node = new Node (this,IDENTIFIER, var) ;
            node->value.integer = level ;
            return node ;
            break ;
            }

        // anonymous blocks
        case FUNCTION:
        case THREAD:
        case CLASS:
        case TINTERFACE:
        case PACKAGE:
        case MONITOR:
            return anonBlock (currentToken) ;

        default:
            do {
                nextToken(); 
            } while (currentToken == BAD) ;		// skip until a valid token
            error ("Expression syntax error") ;
            return new Node (this,NUMBER, Value (0)) ;
        }
    }
}

//
// parse a new array definition.  Returns the NEWVECTOR node at the top level
//

Node *Aikido::parseNew(bool emptyok) {
    if (match (LSQUARE)) {
       Node *expr = NULL ;
       if (emptyok && match (RSQUARE)) {
           expr = new Node (this, NUMBER, Value ((INTEGER)0)) ;
       } else {
           expr = single_expression() ;
           needbrack (RSQUARE) ;
       }
       Node *element = parseNew(false) ;
       return new Node (this, NEWVECTOR, expr, element) ;
    }
    return NULL ;
}

//
// symbol table management
//

Variable *Aikido::addVariable (string name) {
    Variable *var = currentScope->addVariable (name) ;
    addVariableName (name, currentScope, var) ;
    var->setAccess (currentAccessMode) ;
    return var ;
}

Parameter *Aikido::addParameter (string name) {
    Parameter *p = currentScope->addParameter (name) ;
    addVariableName (name, currentScope, p) ;
    return p ;
}

Reference *Aikido::addReference (string name) {
    Reference *p = currentScope->addReference (name) ;
    addVariableName (name, currentScope, p) ;
    return p ;
}

void Aikido::insertVariable (Variable *var) {
    currentScope->insertVariable (var) ;
    addVariableName (var->name, currentScope, var) ;
    var->setAccess (currentAccessMode) ;
}

void Aikido::insertMacro (Macro *macro) {
    currentScope->insertMacro (macro) ;
}

//
// find a variable in the stack of scopes.  Each local variable has an offset into a
// stack frame.  The stack frame is determined by the major scope (function, class,...)
// to which the variable belongs.  On the traversal up the scope chain we count the
// number of major scopes we pass through and return this number to the caller
// through the level ref parameter
//

Variable *Aikido::findVariable (const string &name, int &level, int access, SourceLocation *source, Package **p) {
    Variable *var ;
    level = 0 ;
    Scope *scope = currentScope ;
    Scope *foundScope ;
    while (scope != NULL) {
        if ((var = scope->findVariable (name, foundScope, access, source, p)) != NULL) {
            if (var->flags & VAR_STATIC) {
                level = currentScopeLevel ;   // statics are in main
            } else {
                level = currentScopeLevel - foundScope->level ;
            }
            return var ;
        }
        scope = scope->parentScope ;
    }

    return NULL ;
}

// this is used when a worker wants to find a variable at execute time.  The findVariable routine
// uses the currentScope and currentScopeLevel members of Aikido, so these need to be set

Variable *Aikido::findVariable (const string &name, int &level, int access, SourceLocation *source, Scope *scope, int scopelevel) {
    this->currentScope = scope ;
    this->currentScopeLevel = scopelevel ;
    return findVariable (name, level, access, source) ;
}


Package *Aikido::findPackage (const string &name) {
    Package *pkg ;
    int npkgs ;
    Scope *scope = currentScope ;
    while (scope != NULL) {
        if ((pkg = scope->findPackage (name, npkgs)) != NULL) {
            if (npkgs != 1) {
                error ("Ambiguous use of %s, I can find %d of them", name.c_str(), npkgs) ;
            }
            return pkg ;
        }
        scope = scope->parentScope ;
    }
    return NULL ;
}

Variable *Aikido::findPackageVariable (const string &name, int &level) {
    Variable *var ;
    int npkgs ;
    if (name == "this") {
        return NULL ;
    }
    Scope *scope = currentScope ;
    while (scope != NULL) {
        if ((var = scope->findPackageVariable (name, npkgs)) != NULL) {
            if (npkgs != 1) {
                error ("Ambiguous use of %s, I can find %d of them", name.c_str(), npkgs) ;
            }
            if (var->flags & VAR_STATIC) {
                level = currentScopeLevel ;   // statics are in main
            } else {
                level = currentScopeLevel - scope->level ;
            }
            return var ;
        }
        scope = scope->parentScope ;
    }
    return NULL ;
}


Macro *Aikido::findMacro (const string &name, Package *&pkg) {
    Macro *macro, *mac = NULL ;
    Scope *scope = currentScope ;
    int nmacros = 0 ;
    pkg = NULL ;
    while (scope != NULL) {
        if ((macro = scope->findMacro (name, pkg)) != NULL) {
            mac = macro ;
            nmacros++ ;
            break ;
        }
        scope = scope->parentScope ;
    }
    if (nmacros == 0) {
        scope = currentScope ;
        while (scope != NULL) {
            if ((macro = scope->findPackageMacro (name, nmacros, pkg)) != NULL) {
                mac = macro ;
            }
            scope = scope->parentScope ;
        }
        if (nmacros > 1) {
            error ("Ambiguous use of macro %s, I can find %d of them", name.c_str(), nmacros) ;
        }
    }
    return mac ;
}



Tag *Aikido::findTag (const string &name, Scope *topscope) {
    Variable *var ;
    Scope *s ;
    Scope *scope = topscope == NULL ? currentScope : topscope;
    while (scope != NULL) {
        if ((var = scope->findVariable (name, s, VAR_ACCESSFULL, NULL, NULL)) != NULL) {
            return (Tag*)var ;
        }
        scope = scope->parentScope ;
    }
    int level ;
    var = findPackageVariable (name, level) ;
    return (Tag*)var ;
}

Variable *Aikido::findTopScopeVariable (const string &name) {
    Scope *scope ;
    return currentScope->Scope::findVariable (name, scope, VAR_ACCESSFULL, NULL, NULL) ;
}

Macro *Aikido::findTopScopeMacro (const string &name) {
    Package *p ;
    return currentScope->Scope::findMacro (name, p) ;
}




void Scope::insertVariable (Variable *var) {
    variables[var->name] = var ;
    var->oldValue.ptr = (void*)this ;
}

void Scope::insertMacro (Macro *macro) {
    macros[macro->getName()] = macro ;
}

Variable *Scope::addVariable (string name) {
    Variable *var = new Variable (name, p->currentMajorScope->stacksize++) ;
    variables[var->name] = var ;
    var->oldValue.ptr = (void*)this ;
    return var ;

}

Parameter *Scope::addParameter (const string name) {
    Parameter *para = new Parameter (name, p->currentMajorScope->stacksize++) ;
    variables[para->name] = para ;
    para->oldValue.ptr = (void*)this ;
    nparas++ ;
    return para ;
}

Reference *Scope::addReference (const string name) {
    Reference *para = new Reference (name, p->currentMajorScope->stacksize++) ;
    variables[para->name] = para ;
    para->oldValue.ptr = (void*)this ;
    nparas++ ;
    return para ;
}

Variable *Scope::findVariable (const string &name, Scope *&scope, int access, SourceLocation *source, Package **pkg) {
    if (pkg != NULL) {
        *pkg = NULL ;
    }
    VarMap::iterator entry = variables.find (name) ;
    if (entry == variables.end()) {
        return NULL ;
    }
    scope = this ;
    return (*entry).second ;
}

Variable *Scope::findLocalVariable (const string &name) {
    VarMap::iterator entry = variables.find (name) ;
    if (entry == variables.end()) {
        return NULL ;
    }
    return (*entry).second ;
}



void Block::makefullname() {
    Block *b = parentBlock ;
    fullname = name ;
    while (b != NULL) {
        fullname.insert (0, b->name + ".") ;
        b = b->parentBlock ;
    }
}

Variable *Block::findVariable (const string &nm, Scope *&scope, int access, SourceLocation *source, Package **pkg) {
    Variable *var = Scope::findVariable (nm, scope, access, source, pkg) ;
    if (var != NULL) {
        var->checkAccess (p, this, source, access) ;
        return var ;
    }
    if (superblock != NULL) {
        var = superblock->block->findVariable (nm, scope, access & ~VAR_PRIVATE, source, pkg) ;
        if (var != NULL) {
            scope = this ;
            return var ;
        }
    }
    return NULL ;
}

Macro *Scope::findMacro (const string &name, Package *&pkg) {
    pkg = NULL ;
    MacroMap::iterator entry = macros.find (name) ;
    if (entry == macros.end()) {
        return NULL ;
    }
    return (*entry).second ;
}

Macro *Block::findMacro (const string &nm, Package *&pkg) {
    Macro *macro = Scope::findMacro (nm, pkg) ;
    if (macro != NULL) {
        return macro ;
    }
    if (superblock != NULL) {
        return superblock->block->findMacro (nm, pkg) ;
    }
    return NULL ;
}

Macro *Macro::findMacro (const string &nm, Package *&pkg) {
    if (instanceScope == NULL) {
        throw "macro has not been instantiated" ;
    }
    Macro *mac = NULL ;
    Scope *s = instanceScope ;
    while (s != NULL) {
        mac = s->findMacro (nm, pkg) ;
        if (mac != NULL) {
           return mac ;
        }
        s = s->parentScope ;
    }
    return Scope::findMacro (nm, pkg) ;
}


//
// move the members of a block by delta.  This simply sets
// the offsets
//

void Block::moveMembers (int delta) {
    stacksize += delta ;
    VarMap::iterator var ;
    for (var = variables.begin() ; var != variables.end() ; var++) {
        (*var).second->incOffset (delta) ;
    }
    
    std::list<Tag*>::iterator child ;
    for (child = children.begin() ; child != children.end() ; child++) {
        (*child)->block->moveMembers (delta) ;
    }
}

//
// is block b a super block of this one?
//

bool Block::isSuperBlock (Block *b) {
    if (this == b) {
        return true ;
    }
    if (superblock != NULL) {
        return superblock->block->isSuperBlock (b) ;
    }
    return false ;
}


Variable *Scope::findPackageVariable (const string &nm, int &n) {
    std::list<Package*>::iterator s ;
    Variable *var = NULL ;
    n = 0 ;
    Scope *scope ;
    var = findVariable (nm, scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (var != NULL) {
        n = 1 ;
        return var ;
    }
    for (s = siblings.begin() ; s != siblings.end() ; s++){
        Variable *v = (*s)->findVariable (nm, scope, VAR_PUBLIC, NULL, NULL) ;
        if (v != NULL && v != var) {
            n++ ;
            var = v ;
        }
    }
    return var ;
}

Package *Scope::findPackage (const string &nm, int &n) {
    std::list<Package*>::iterator s ;
    Package *pkg = NULL ;
    n = 0 ;
    Scope *scope ;
    PackageMap::iterator i = packages.find (nm) ;
    if (i != packages.end()) {
        pkg = (*i).second ;
        n = 1 ;
        return pkg ;
    }
    for (s = siblings.begin() ; s != siblings.end() ; s++){
        int n1 ;
        Package *p = (*s)->findPackage (nm, n1) ;
        if (p != NULL) {
            if (p != pkg) {
                n += n1 ;
                pkg = p ;
            }
            
        }
    }
    return pkg ;
}

Macro *Scope::findPackageMacro (const string &nm, int &n, Package *&pkg) {
    std::list<Package*>::iterator s ;
    Macro *mac = NULL ;
    Package *rpkg = NULL ;
    for (s = siblings.begin() ; s != siblings.end() ; s++){
        Macro *macro = (*s)->findMacro (nm, pkg) ;
        if (macro != NULL) {
            mac = macro ;
            rpkg = (Package*)(*s) ;
            n++ ;
        }
    }
    pkg = rpkg ;
    return mac ;
}

Node::~Node() {
    if (op == FOREIGN) {
        delete value.foreign ;
    }
}


void Node::propagateFlags() {
    // to have it
    if (left != NULL && left->flags & NODE_TYPEFIXED) {
        flags |= NODE_TYPEFIXED ;
    }
    if (right != NULL) {
        if (!(right->flags & NODE_TYPEFIXED)) {
            flags &= ~NODE_TYPEFIXED ;
        }
    }
    // types on left and right must match
    if (flags & NODE_TYPEFIXED && left != NULL && right != NULL) {
        if (left->type != right->type) {
            flags &= ~NODE_TYPEFIXED ;
        }
    } 
    if (flags & NODE_TYPEFIXED) {
        switch (op) {
        case EQUAL:
        case NOTEQ:
        case LESS:
        case LESSEQ:
        case GREATER:
        case GREATEREQ:
        case BANG:
             type = T_BOOL ;
             break ;
        case NEWVECTOR:
             type = T_VECTOR ;
             break ;
        case SIZEOF:
             type = T_INTEGER ;
             break ;
        case TYPEOF:
             flags &= ~NODE_TYPEFIXED ;
             break ;
        default:
             if (left != NULL) {
                type = left->type ;
            } else if (right != NULL) {
                type = right->type ;
            }
        }
    }
}

void Node::init (Aikido *d) {
    if (d->currentContext != NULL) {
        source = sourceRegistry.newSourceLocation (d->currentContext->getFilename().str, d->currentContext->getLineNumber()) ;
    } else {
        source = sourceRegistry.newSourceLocation ("unknown", -1) ;
    }
    flags = 0 ;
    scope = NULL ;
    pad = 0 ;			// just for RTC
    type = T_NONE ;
  
    // leaf nodes with certain opcodes have fixed types
    switch (op) {
    case NUMBER:
    case FNUMBER:
    case STRING:
    case CHAR:
    case NEW:			// objects are fixed type
    case VECTOR:
        flags |= NODE_TYPEFIXED ;
        type = value.type ;
        break ;
    case IDENTIFIER:
        if (var->flags & VAR_TYPEFIXED) {
            flags |= NODE_TYPEFIXED ;
            type = var->type ;
        }
        break ;
    }
    propagateFlags() ;
}

//
// delete a whole tree
//

void Aikido::deleteTree (Node *tree) {
    if (tree == NULL) {
        return ;
    }
    if (tree->left != NULL) {
        deleteTree (tree->left) ;
    }
    if (tree->right != NULL) {
        deleteTree (tree->right) ;
    }
    switch (tree->op) {
    case VECTOR:
    case FUNCPARAS:
    case MAP:
    case SWITCH:
        for (int i = 0 ; i < tree->vec.size() ; i++) {
            deleteTree (tree->vec[i]) ;
        }
        break ;
    }
    delete tree ;
}

bool Aikido::compareTree (Node *t1, Node *t2) {
    if (t1 == NULL && t2 != NULL) {
        return false ;
    }
    if (t1 != NULL && t2 == NULL) {
        return false ;
    }
    if (t1 == t2) {
        return true ;
    }
    if (!compareTree (t1->left, t2->left)) {
       return false ;
    }
    if (!compareTree (t1->right, t2->right)) {
       return false ;
    }
    if (t1->op != t2->op) {
        return false ;
    }
    switch (t1->op) {
    case NUMBER:
    case STRING:
    case VECTOR:
    case MAP:
    case FNUMBER:
    case CHAR:
        return t1->value == t2->value ;
    case IDENTIFIER:
        return t1->var == t2->var ;
    }
    return true ;
}

//
// does the block being defined override any public or protected block in
// a base class (ie. is this a virtual block override?).  
//
// return NULL if no override, otherwise return the variable being overridden
//


Variable *Block::checkOverride (Variable *var) {
    if (var->flags & VAR_PRIVATE) {			// defining a private block?  No override
        return NULL ;
    }
    Scope *scope ;
    Variable *v = NULL ;
    string tagname = "." + var->name ;

    Tag *tag =  (Tag*)Scope::findVariable (tagname, scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (tag != NULL) {
        v = tag->var ;
        if (v->flags & VAR_PRIVATE) {			// private blocks are not overridden
            return NULL ;
        }
    }
    if (v == NULL && superblock != NULL) {
        v = superblock->block->checkOverride (var) ;
    }
    return v ;
}


InterpretedBlock *Aikido::findMain() {
    Scope *mainscope ;
    Tag *tag = (Tag*)mainPackage->findVariable (string (".$main__0"), mainscope, VAR_ACCESSFULL, NULL, NULL) ;
    if (tag != NULL) {
        if (tag->block->type == T_FUNCTION) {
            Function *func = (Function *)tag->block ;
            if (!func->isNative()) {
                InterpretedBlock *iblock = (InterpretedBlock*)func ;
                if (iblock->parameters.size() == 0) {
                    return iblock ;
                }
            }
        }
    }
    return NULL ;
}


void Worker::error (const char *s, ...) {
    char str[1024] ;
    va_list arg ;
    va_start (arg, s) ;
    vsprintf (str, s, arg) ;
    aikido->error (str) ;
}

void Worker::error (string filename, int lineno, const char *s, ...) {
    char str[1024] ;
    va_list arg ;
    va_start (arg, s) ;
    vsprintf (str, s, arg) ;
    aikido->error (filename, lineno, str) ;
}


StackFrame *Aikido::getStack (StackFrame *frame, StaticLink *sl, int level) {
    if (level == 0) {
        return  frame ;
    }
    if (level == 1) {   
        return sl->frame ;
    }
    while (sl != NULL && --level > 0) {
        sl = sl->next ;
    }
    if (sl == NULL) {
        std::cerr << "Static link sequence too short - aborting\n" ;
        ::abort() ;
        //throw "Static link sequence too short" ;
    }
    return sl->frame ;
}

// threads

void Aikido::deleteThread (OSThread_t id) {
    threadLock.lock() ;
    ThreadMap::iterator t = threads.find (id) ;
    if (t != threads.end()) {
        delete t->second ;
    }
    threads.erase (id) ;
    numThreads-- ;
    threadLock.unlock() ;
}


void Aikido::insertThread (OSThread_t id, OSThread *t) {
    threadLock.lock() ;
    threads[id] = t ;
    numThreads++ ;
    threadLock.unlock() ;
}

OSThread *Aikido::findThread (OSThread_t id) {
    threadLock.lock() ;
    ThreadMap::iterator r = threads.find (id) ;
    threadLock.unlock() ;
    if (r == threads.end()) {
        return NULL ;
    }
    return (*r).second ;
}

// cannot be in aikido.h because CodeSequence is not complete there

Instruction *InstructionPtr::getInst() const {
    return static_cast<Instruction*>(&(*(code->code.begin() + offset))) ;
}

//
// anonymous blocks
//

Node *Aikido::anonBlock(Token command) {
    static int blocknum ;
    Node *node = NULL ;
    InterpretedBlock *block ;

    nextToken() ;
    Variable *var = NULL ;
    Tag *tag = NULL ;

    char buf[32] ;
    sprintf (buf, "$anon__%d", blocknum++) ;
    string name = buf ;

    Token oldAccessMode = currentAccessMode ;

    if (currentStack != NULL) {
        currentStack->checkCapacity() ;
        var = new Variable (name, currentStack->block->stacksize++) ;
        currentStack->block->insertVariable (var) ;
    } else {
        var = new Variable (name, currentMajorScope->stacksize++) ;
    }
    var->setAccess (currentAccessMode) ;
    var->flags |= VAR_CONST ;  
    int oldScopeLevel = currentScopeLevel++ ;
    switch (command) {
    case THREAD:
        block = new Thread (this, name, currentScopeLevel, currentScope) ;
        break ;

    case FUNCTION:
        block = new InterpretedBlock (this, name, currentScopeLevel, currentScope) ;
        break ;

    case CLASS:
        block = new Class (this, name, currentScopeLevel, currentScope) ;
        break ;

    case TINTERFACE:
        block = new Interface (this, name, currentScopeLevel, currentScope) ;
        break ;

    case MONITOR:
        block = new MonitorBlock (this, name, currentScopeLevel, currentScope) ;
        break ;

    case PACKAGE:
        block = new Package (this, name, currentScopeLevel, currentScope) ;
        break ;

    }

    tag = new Tag (string ("." + name), block, var) ;
    tag->setAccess (currentAccessMode) ;
    tag->flags |= VAR_CONST ;
    currentAccessMode = PRIVATE ;
    node = getBlockContents (command, tag, var, block, false, false) ;
    if (currentStack == NULL && command != TINTERFACE) {
        debugger->registerBlock (block) ;
    }
    currentScopeLevel = oldScopeLevel ;
    currentAccessMode = oldAccessMode ;

    Node *id = new Node (this, IDENTIFIER, var) ;
    id->value.integer = 0 ;  			// level 0

    return new Node (this, SEMICOLON, node, id) ;
}

// constant expressions

INTEGER Aikido::constExpression() {
    return constOrExpression() ;
}

INTEGER Aikido::constOrExpression() {
    INTEGER right, result ;
    result = constXorExpression() ;
    for (;;) {
        if (match (BITOR)) {
            right = constXorExpression() ;
            result |= right ;
        } else {
            return result ;
        }
    }
}

INTEGER Aikido::constXorExpression() {
    INTEGER right, result ;
    result = constAndExpression() ;
    for (;;) {
        if (match (CARET)) {
            right = constAndExpression() ;
            result ^= right ;
        } else {
            return result ;
        }
    }
}

INTEGER Aikido::constAndExpression() {
    INTEGER right, result ;
    result = constEqualityExpression() ;
    for (;;) {
        if (match (AMPERSAND)) {
            right = constEqualityExpression() ;
            result &= right ;
        } else {
            return result ;
        }
    }
}

INTEGER Aikido::constEqualityExpression() {
    INTEGER right, result ;
    result = constRelationalExpression() ;
    for (;;) {
        if (match (EQUAL)) {
            right = constRelationalExpression() ;
            result = result == right ;
        } else if (match (NOTEQ)) {
            right = constRelationalExpression() ;
            result = result != right ;
        } else {
            return result ;
        }
    }
}

INTEGER Aikido::constRelationalExpression() {
    INTEGER right, result ;
    result = constShiftExpression() ;
    for (;;) {
        if (match (LESS)) {
            right = constShiftExpression() ;
            result = result < right ;
        } else if (match (GREATER)) {
            right = constShiftExpression() ;
            result = result > right ;
        } else if (match (LESSEQ)) {
            right = constShiftExpression() ;
            result = result <= right ;
        } else if (match (GREATEREQ)) {
            right = constShiftExpression() ;
            result = result >= right ;
        } else {
            return result ;
        }
    }
}

INTEGER Aikido::constShiftExpression() {
    INTEGER right, result ;
    result = constAdditiveExpression() ;
    for (;;) {
        if (match (LSHIFT)) {
            right = constAdditiveExpression() ;
            result <<= right ;
        } else if (match (RSHIFT)) {
            right = constAdditiveExpression() ;
            result >>= right ;
        } else if (match (ZRSHIFT)) {
            right = constAdditiveExpression() ;
            result = (UINTEGER)result >> right ;
        } else {
            return result ;
        }
    }
}

INTEGER Aikido::constAdditiveExpression() {
    INTEGER right, result ;
    result = constMultExpression() ;
    for (;;) {
        if (!newLineRead && match (PLUS)) {
            right = constMultExpression() ;
            result += right ;
        } else if (!newLineRead && match (MINUS)) {
            right = constMultExpression() ;
            result -= right ;
        } else {
            return result ;
        }
    }
}

INTEGER Aikido::constMultExpression() {
    INTEGER right, result ;
    result = constUnaryExpression() ;
    for (;;) {
        if (match (STAR)) {
            right = constUnaryExpression() ;
            result *= right ;
        } else if (match (SLASH)) {
            right = constUnaryExpression() ;
            result /= right ;
        } else if (match (PERCENT)) {
            right = constUnaryExpression() ;
            result %= right ;
        } else {
            return result ;
        }
    }
}

INTEGER Aikido::constUnaryExpression() {
    INTEGER result ;
    if (match (PLUS)) {
        return constUnaryExpression() ;
    } else if (match (MINUS)) {
        result = constUnaryExpression() ;
        return -result ;
    } else if (match (BANG)) {
        result = constUnaryExpression() ;
        return !result ;
    } else if (match (TILDE)) {
        result = constUnaryExpression() ;
        return ~result ;
    } else {
        return constPrimaryExpression() ;
    }
}

INTEGER Aikido::constPrimaryExpression() {
    INTEGER value ;
    if (match (LBRACK)) {
        value = constExpression() ;
        needbrack (RBRACK) ;
        return value ;
    } else {
        switch (currentToken) {
        case TIN:
        case IDENTIFIER: {
            if (currentToken == TIN) {
               strcpy (spelling, "in") ;
               currentToken = IDENTIFIER ;
            }

            Package *pkg = NULL ;
            int level ;
            string name (spelling) ;
            nextToken() ;
            Variable *var = findVariable (name, level, VAR_ACCESSFULL, NULL, &pkg) ;
            if (var != NULL) {
                if (var->isEnumConst()) {
                    EnumConst *ec = static_cast<EnumConst*>(var) ;
                    return ec->value ;
                }
            }
            error ("Illegal identifier %s in constant expression", name.c_str()) ;
            return 0 ;
            break ;
            }
        case NUMBER: 
            value = number ;
            nextToken() ;
            return value ;
         case CHAR: 
             value = number ;
             nextToken() ;
             return value ;
        case TTRUE: 
            nextToken() ;
            return true ;
        case TFALSE: 
            nextToken() ;
            return false ;
        case FNUMBER:
            nextToken() ;
            error ("Illegal real constant in constant expression") ;
            return 0 ;
        default:
            do {
                nextToken(); 
            } while (currentToken == BAD) ;		// skip until a valid token
            error ("Constant expression syntax error") ;
            return 0 ;
        }
    }
}

// tidyup destructors for dynamic loading.

Scope::~Scope() {
    for (VarMap::iterator v = variables.begin() ; v != variables.end() ; v++) {
        delete v->second ;
    }
    for (MacroMap::iterator m = macros.begin() ; m != macros.end() ; m++) {
        delete m->second ;
    }
    for (PackageMap::iterator p = packages.begin() ; p != packages.end() ; p++) {
        delete p->second ;
    }
    for (Children::iterator child = children.begin() ; child != children.end() ; child++) {
        delete *child ;
    }
}

// destruct a block

Block::~Block() {
#ifdef GCDEBUG
    unregisterObject (this) ;
#endif
    if (code != NULL) {
        delete code ;
    }
    Aikido::deleteTree (body) ;
}


Annotation *Variable::getAnnotation (string name) {
    for (AnnotationList::iterator i = annotations.begin() ; i != annotations.end() ; i++) {
        Annotation *a = *i;
        if (a->iface->name == name) {
            return a;
        }
    }
    return NULL;
}

bool Variable::allowsOverride() {
    for (AnnotationList::iterator i = annotations.begin() ; i != annotations.end() ; i++) {
        Annotation *a = *i;
        if (a->iface->name == "@AllowOverride") {
            return true;
        }
    }
    return false;
}


Variable::~Variable() {
}

Parameter::~Parameter() {
    Aikido::deleteTree (def) ;
    Aikido::deleteTree (type) ;
}


void Aikido::setTraceLevel (int level) {
    tracer->setTraceLevel (level) ;
}

//
// variable names mapping
//

VariableName *Aikido::addVariableName (const string &name, Scope *s, Variable *v) {
    if (currentStack != NULL || !cacheVariables) {
        return NULL ;				// only do this when we are not running
    }
    VariableName *var = NULL ;
    //std::cout << "adding variable name " << name << '\n' ;
    VariableNameMap::iterator r = variableNames.find (name) ;
    if (r == variableNames.end()) {
        //std::cout << "    not found, inserting\n" ;
        var = new VariableName (name) ;
        variableNames[name] = var ;
    } else {
        var = (*r).second ;
    }
    var->insert (s, v) ;
    return var ;
}

VariableName *Aikido::findVariableName (const string &name) {
    VariableNameMap::iterator r = variableNames.find (name) ;
    if (r == variableNames.end()) {
        VariableName *var = new VariableName (name) ;
        variableNames[name] = var ;
        return var ;
    }
    return (*r).second ;
}


Variable *VariableName::lookup (Scope *s, SourceLocation *source, int access) {
    if (lastLookupScope == s) {
        return lastvar ;
    }
    //std::cout << "looking up variable " << name << '\n' ;
    LocationMap::iterator r = locations.find (s) ;
    if (r == locations.end()) {
        //std::cout << "   not found\n" ;
        return NULL ;
    }
    //std::cout << "   *** found\n" ;
    Variable *var = (*r).second ;
    var->checkAccess (s->p, static_cast<Block*>(s), source, access) ;
    lastLookupScope = s ;
    lastvar = (*r).second ;
    return lastvar ;
}

void VariableName::insert (Scope *s, Variable *v) {
    locations[s] = v ;
}


//
// return true if:
//   a. the block b is the same block as this
//   b. the block b is a superblock of this
//   c. if b is an interface, this block implements it
//

bool Block::isSubclassOrImplements (Block *b) {
    if (this == b) {
        return true ;
    }
    if (isSuperBlock (b)) {
        return true ;
    }

    if (b->type == T_INTERFACE) {
        Block *blk = this ;
        while (blk != NULL) {
            if (blk->implementsInterface (b)) {
                return true ;
            }
            if (blk->superblock == NULL) {
                break ;
            }
            blk = blk->superblock->block ;
        }
    }
    return false ;
}


bool Block::implementsInterface (Block *iface) {
    if (interfaces.size() != 0) {
        std::list<Tag*>::iterator i = interfaces.begin() ;
        while (i != interfaces.end()) {
            Interface *iface = static_cast<Interface*>((*i)->block) ;
            if (iface == iface) {
                return true ;
            }
            i++ ;
        }
    }
    return false ;
}

bool Block::isAbstract() {
    return (flags & BLOCK_ISABSTRACT) != 0 ;
}

void Block::printUndefinedMembers (std::ostream &os) {
    os << "\tundefined members:" ;
    Scope::VarMap::iterator i = variables.begin() ;
    for (; i != variables.end() ; i++) {
        if (i->first[0] == '.') {
            Tag *tag = (Tag*)i->second ;
            if (tag->flags & VAR_PREDEFINED) {
                os << "\n\t\t" << tag->block->name ;
            }
        }

    }
}


//
// monitor cache
//


Monitor *Aikido::findMonitor (Object *obj) {
    monitorCacheLock.lock() ;
    Monitor *mon = NULL ;
    MonitorMap::iterator m = monitorCache.find (obj) ;
    if (m != monitorCache.end()) {
        mon = (*m).second ;
    } else {
        if (!monitorFreeList.empty()) {
            MonitorFreeList::iterator f = monitorFreeList.begin() ;
            mon = *f ;
            monitorFreeList.erase (f) ;
        } else {
            mon = new (1) Monitor (mainPackage, obj->slink, mainStack, NULL) ;
        }
        monitorCache[obj] = mon ;
    }
    monitorCacheLock.unlock() ;
    return mon ;
}

void Aikido::freeMonitor (Object *obj) {
    monitorCacheLock.lock() ;
    MonitorMap::iterator m = monitorCache.find (obj) ;
    if (m != monitorCache.end()) {
        Monitor *mon = m->second ;
        monitorFreeList.push_back (mon) ;
        monitorCache.erase (m) ;
    }
    monitorCacheLock.unlock() ;
}

//
// object code dumping
//

Scope::Scope (Aikido *b, int l, Scope *parent, Block *major) : p (b), level(l), parentScope (parent), majorScope (major), nparas(0) {
    if (afterinit && b->cacheVariables) {
       index = b->registerScope (this) ;
    }
}

int Aikido::registerScope (Scope *s) {
    scopevec.push_back (s) ;
    return ++nscopes ;
}

void Aikido::dumpByte (int b, std::ostream &s) {
    s.put ((char)b) ;
}

void Aikido::dumpWord (int w, std::ostream &s) {
    s.put ((char)(w >> 24)) ;
    s.put ((char)(w >> 16)) ;
    s.put ((char)(w >> 8)) ;
    s.put ((char)(w >> 0)) ;
}

void Aikido::dumpDirectString (std::string &str, std::ostream &s) {
    dumpWord (str.size(), s) ;
    for (int i = 0 ; i < str.size() ; i++) {
        dumpByte (str[i], s) ;
    }
}

void Aikido::dumpString (std::string &str, std::ostream &s) {
    dumpWord (poolString (str), s) ;
}

void Aikido::dumpDirectString (const string &str, std::ostream &s) {
    dumpWord (str.str.size(), s) ;
    for (int i = 0 ; i < str.str.size() ; i++) {
        dumpByte (str[i], s) ;
    }
}

void Aikido::dumpString (const string &str, std::ostream &s) {
    dumpWord (poolString (const_cast<std::string&>(str.str)), s) ;
}

int Aikido::readByte (std::istream &s) {
    return s.get() & 0xff ;
}

int Aikido::peekByte (std::istream &s) {
    int b = s.get() & 0xff ;
    s.unget() ;
    return b; 
}


int Aikido::readWord (std::istream &s) {
    int w = readByte(s) ;
    w = (w << 8) | readByte(s) ;
    w = (w << 8) | readByte(s) ;
    w = (w << 8) | readByte(s) ;
    return w ;
}

std::string Aikido::readDirectString (std::istream &s) {
    int len = readWord(s) ;
    std::string r = "" ;
    while (len-- > 0) {
        r += (char)readByte(s) ;
    }
    return r ;
}

std::string Aikido::readString (std::istream &s) {
    int index = readWord (s) ;
    if (loadpass == 1) {
        return "" ;		// during pass 1 we have no string table
    }
    if (index < 0 || index >= stringvec.size()) {
        std::cerr << "Invalid string index " << index << "\n" ;
        abort() ;
    }
    return lookupString (index) ;
}

void Aikido::dumpVariableRef (Variable *var, std::ostream &os) {
    if (var == NULL) {
       dumpWord (-1, os) ;
    } else {
        //std::cout << "dumping variable reference, scope: " << ((Scope*)var->oldValue.ptr)->index << ", offset: " << var->offset << "\n" ;
        dumpWord (((Scope*)var->oldValue.ptr)->index, os) ;
        dumpWord (var->offset, os) ;
    }
}

void Aikido::dumpTagRef (Tag *tag, std::ostream &os) {
    if (tag == NULL) {
        dumpWord (-1, os) ;
    } else {
        dumpWord (((Scope*)tag->oldValue.ptr)->index, os) ;
        dumpString (tag->name, os) ;
    }
}


void Aikido::dumpTagList (std::list<Tag*> &tlist, std::ostream &os) {
    dumpWord (tlist.size(), os) ;
    for (std::list<Tag*>::iterator i = tlist.begin() ; i != tlist.end() ; i++) {
        dumpTagRef (*i, os) ;
    }
}

void Aikido::dumpValue (const Value &v, std::ostream &os) {
    dumpByte (v.type, os) ;
    switch (v.type) {
    case T_REAL :
    case T_INTEGER :
    case T_BOOL :
        dumpWord ((v.integer >> 32), os) ;
        dumpWord (v.integer & 0xffffffff, os) ;
        break ;
    case T_BYTE :
    case T_CHAR :
        dumpByte (v.integer & 0xff, os) ;
        break ;
    case T_ADDRESS :
        break ;
    case T_ENUMCONST :
        dumpVariableRef (v.ec, os) ;
        break ;
    case T_STRING :
        dumpDirectString (v.str->str, os) ;
        break ;
    case T_VECTOR :		// XXX: ??
        break ;
    case T_MAP :		// XXX: ??
        break ;
    case T_BYTEVECTOR :		// XXX: ??
        break ;
    case T_STREAM :		
        // need to write these out as a simple number that can be recreated
        // when the object code is read back in again
        if (v.stream == mystdin) {
            dumpByte (0, os) ;
        } else if (v.stream == mystdout) {
            dumpByte (1, os) ;
        } else if (v.stream == mystderr) {
            dumpByte (2, os) ;
        } else {
            std::cerr << "Unknown stream\n" ;
        }
        break ;
    case T_OBJECT :
        break ;
    case T_FUNCTION :
    case T_THREAD :
    case T_CLASS :
    case T_PACKAGE :
    case T_ENUM :
    case T_MONITOR :
    case T_INTERFACE :
        dumpWord (v.block->index, os) ;
        break ;
    case T_MEMORY :
        break ;
    case T_POINTER :
        break ;
    case T_NONE :
        break ;
    }
}

Value Aikido::readValue (std::istream &is) {
    Value v ;
    Type t = (Type)readByte (is) ;
    if (loadpass == 1) {		// can't set type to a block type on pass 1 as it might be NULL
        v.type = T_NONE ;
    } else {
        v.type = t ;
    }
    switch (t) {
    case T_REAL :
    case T_INTEGER :
    case T_BOOL :
        v.integer = readWord (is) ;
        v.integer = (v.integer << 32) | readWord (is) ;
        break ;
    case T_BYTE :
    case T_CHAR :
        v.integer = readByte (is) ;
        break ;
    case T_ADDRESS :
        break ;
    case T_ENUMCONST :
        v.ec = (EnumConst *)readVariableRef (is) ;
        break ;
    case T_STRING :
        v.str = new string(readDirectString (is)) ;
        incRef (v.str, string) ;
        break ;
    case T_VECTOR :		// XXX: ??
        break ;
    case T_MAP :		// XXX: ??
        break ;
    case T_BYTEVECTOR :		// XXX: ??
        break ;
    case T_STREAM :{
        int b = readByte (is) ;
        switch (b) {
        case 0:
            v.stream = mystdin ;
            incRef (v.stream, stream) ;
            break ;
        case 1:
            v.stream = mystdout ;
            incRef (v.stream, stream) ;
            break ;
        case 2:
            v.stream = mystderr ;
            incRef (v.stream, stream) ;
            break ;
        default:
            std::cerr << "Unknown stream: " << b << "\n" ;
        }
        break ;
        }
    case T_OBJECT :
        break ;
    case T_FUNCTION :
        v.func = (Function*)readScope (is) ;
        if (v.func != NULL) {
           incRef (v.func, block); 
        }
        break ;
    case T_THREAD :
        v.thread = (Thread*)readScope (is) ;
        if (v.thread != NULL) {
           incRef (v.thread, block); 
        }
        break ;
    case T_CLASS :
        v.cls = (Class*)readScope (is) ;
        if (v.cls != NULL) {
           incRef (v.cls, block); 
        }
        break ;
    case T_PACKAGE :
        v.package = (Package*)readScope (is) ;
        if (v.package != NULL) {
           incRef (v.package, block); 
        }
        break ;
    case T_ENUM :
        v.en = (Enum*)readScope (is) ;
        if (v.en != NULL) {
           incRef (v.en, block); 
        }
        break ;
    case T_MONITOR :
        v.block = (Block*)readScope (is) ;
        if (v.block != NULL) {
           incRef (v.block, block); 
        }
        break ;
    case T_INTERFACE :
        v.iface = (Interface*)readScope (is) ;
        if (v.iface != NULL) {
           incRef (v.block, block); 
        }
        break ;
    case T_MEMORY :
        break ;
    case T_POINTER :
        break ;
    case T_NONE :
        break ;
    }
    return v ;
}

enum {
    DUMP_SCOPE_TYPE = 1,
    DUMP_BLOCK_TYPE,
    DUMP_VARIABLE_TYPE,
    DUMP_TAG_TYPE,
    DUMP_NF_TYPE,
    DUMP_THREAD_TYPE,
    DUMP_IBLOCK_TYPE,
    DUMP_RNF_TYPE,
    DUMP_ENUM_TYPE,
    DUMP_PARAMETER_TYPE,
    DUMP_REFERENCE_TYPE,
    DUMP_ENUMCONST_TYPE,
    DUMP_CLASS_TYPE,
    DUMP_MONITOR_TYPE,
    DUMP_INTERFACE_TYPE,
    DUMP_FUNCTION_TYPE,
    DUMP_PACKAGE_TYPE
} ;

void Variable::dump (Aikido *aikido, std::ostream &os) {
    aikido->dumpByte (DUMP_VARIABLE_TYPE, os) ;
    aikido->dumpDirectString (name.str, os) ;
    aikido->dumpWord (flags, os) ;
    aikido->dumpWord (offset, os) ;
    aikido->dumpWord (type, os) ;
    //std::cout << "dumping var " << name << " offset: " << offset << "\n" ;
}

void Tag::dump (Aikido *aikido, std::ostream &os) {
    aikido->dumpByte (DUMP_TAG_TYPE, os) ;
    Variable::dump (aikido, os) ;
    aikido->dumpWord (block->index, os) ;
    aikido->dumpVariableRef (var, os) ;
}

void Parameter::dump (Aikido *aikido, std::ostream &os) {
    aikido->dumpByte (DUMP_PARAMETER_TYPE, os) ;
    Variable::dump (aikido, os) ;
    aikido->dumpByte (def != NULL, os) ;		// only a fla
}

void Reference::dump (Aikido *aikido, std::ostream &os) {
    aikido->dumpByte (DUMP_REFERENCE_TYPE, os) ;
    Parameter::dump (aikido, os) ;
}

void EnumConst::dump (Aikido *aikido, std::ostream &os) {
    aikido->dumpByte (DUMP_ENUMCONST_TYPE, os) ;
    Variable::dump (aikido, os) ;
    aikido->dumpWord (en->index,os) ;
    aikido->dumpVariableRef (next, os) ;
    aikido->dumpVariableRef (prev, os) ;
    aikido->dumpWord (value >> 32, os) ;
    aikido->dumpWord (value & 0xffffffff, os) ;
}

void Scope::dump (std::ostream &os) {
    p->dumpByte (DUMP_SCOPE_TYPE, os) ;
    // first dump all the variables
    p->dumpWord (variables.size(), os) ;
    //std::cout << "dumping scope\n" ;
    for (VarMap::iterator v = variables.begin() ; v != variables.end() ; v++) {
        Variable *var = v->second ;
        var->dump (p, os) ;
    }
 

    // now the scope information
    p->dumpWord (level, os) ;
    if (parentScope == NULL) {
       p->dumpWord (0, os) ;
    } else {
        p->dumpWord (parentScope->index, os) ;
    }
    if (majorScope == NULL) {
       p->dumpWord (0, os) ;
    } else {
        p->dumpWord (majorScope->index, os) ;
    }
    //p->dumpWord (children.size(), os) ;
    //for (Children::iterator c = children.begin() ; c != children.end() ; c++) {
        //p->dumpWord ((*c)->index, os) ;
    //}
    p->dumpWord (nparas, os) ;
}

void Block::dump (std::ostream &os) {
    //std::cout << "dumping block " << name << "\n" ;
    p->dumpByte (DUMP_BLOCK_TYPE, os) ;
    p->dumpString (name.str, os) ;
    p->dumpString (fullname.str, os) ;
    p->dumpWord (stacksize, os) ;
    p->dumpWord (flags, os) ;
    if (parentClass != NULL) {
        p->dumpWord (parentClass->index, os) ;
    } else {
        p->dumpWord (0, os) ;
    }
    if (parentBlock != NULL) {
        p->dumpWord (parentBlock->index, os) ;
    } else {
        p->dumpWord (0, os) ;
    }
    //if (superblock != NULL) {
        //std::cout << "dumping superblock " << superblock->var->name << "\n" ;
    //}
    p->dumpTagRef (superblock, os) ;

    Scope::dump (os) ;

    p->dumpTagList (children, os) ;
    p->dumpTagList (interfaces, os) ;
    p->dumpWord (type, os) ;
    p->dumpVariableRef (varargs, os) ;
    if (finalize != NULL) {
        p->dumpWord (finalize->index, os) ;
    } else {
        p->dumpWord (0, os) ;
    }
    if (code != NULL) {
       p->dumpWord (1, os) ;
       code->dump (p, os) ;
    } else {
        p->dumpWord (0, os) ;
    }
}

void Function::dump (std::ostream &os) {
    p->dumpByte (DUMP_FUNCTION_TYPE, os) ;
    Block::dump (os) ;
}

void InterpretedBlock::dump (std::ostream &os) {
    p->dumpByte (DUMP_IBLOCK_TYPE, os) ;
    Function::dump (os) ;
    p->dumpWord (parameters.size(), os) ;
    for (int i = 0 ; i < parameters.size() ; i++) {
        p->dumpVariableRef (parameters[i], os) ;
    }
}

void NativeFunction::dump (std::ostream &os) {
    p->dumpByte (DUMP_NF_TYPE, os) ;
    Function::dump (os) ;
    p->dumpWord (paramask, os) ;
}

void RawNativeFunction::dump (std::ostream &os) {
    p->dumpByte (DUMP_RNF_TYPE, os) ;
    NativeFunction::dump (os) ;
}

void Thread::dump (std::ostream &os) {
    p->dumpByte (DUMP_THREAD_TYPE, os) ;
    InterpretedBlock::dump (os) ;
    p->dumpVariableRef (input, os) ;
    p->dumpVariableRef (output, os) ;
}

void Class::dump (std::ostream &os) {
    p->dumpByte (DUMP_CLASS_TYPE, os) ;
    InterpretedBlock::dump (os) ;
    p->dumpWord (operators.size(), os) ;
    for (OperatorMap::iterator i = operators.begin() ; i != operators.end() ; i++) {
        p->dumpWord (i->first, os) ;
        p->dumpWord (i->second->index, os) ;
    }
}

void MonitorBlock::dump (std::ostream &os) {
    p->dumpByte (DUMP_MONITOR_TYPE, os) ;
    Class::dump (os) ;
}

void Package::dump (std::ostream &os) {
    p->dumpByte (DUMP_PACKAGE_TYPE, os) ;
    Class::dump (os) ;
}

void Interface::dump (std::ostream &os) {
    p->dumpByte (DUMP_INTERFACE_TYPE, os) ;
    Class::dump (os) ;
}

void Enum::dump (std::ostream &os) {
    p->dumpByte (DUMP_ENUM_TYPE, os) ;
    InterpretedBlock::dump (os) ;
    p->dumpWord (consts.size(), os) ;
    for (int i = 0 ; i < consts.size() ; i++) {
        p->dumpVariableRef (consts[i], os) ;
    }
}

void Aikido::dumpObjectCode (std::string filename) {
    if (numErrors != 0) {
        return ;
    }
    std::ofstream of (filename.c_str()) ;
    if (!of) {
        std::cerr << "Unable to open object file " << filename << " for output\n" ;
        return ;
    }
    dumpWord (0x416b646f, of) ;		// magic
    dumpWord (100, of) ;			// version
    dumpWord (libraries.size(), of) ;
    // dump open libraries
    for (int i = 0 ; i < libraries.size() ; i++) {
        string &path = libraries[i] ;
        int j = path.size() - 1 ;
        while (j >= 0) {		// find last / in name
            if (path[j] == '/') {
                j++ ;
                break ;
            }
            j-- ;
        }
        std::string p = path.str.substr(j) ;
        dumpDirectString (p, of) ;
    }

    // dump the scopes
    dumpWord (scopevec.size(), of) ;			// number of scopes
    for (int i = 0 ; i < scopevec.size() ; i++) {
        scopevec[i]->dump (of) ;
    }
    // dump the preamble code
    preamble->dump (this, of) ;

    // dump the string pool

    dumpWord (stringvec.size(), of) ;
    for (int i = 0 ; i < stringvec.size() ; i++) {
        std::string &s = stringvec[i] ;
        dumpWord (s.size(), of) ;
        for (int i = 0 ; i < s.size() ; i++) {
            dumpByte (s[i], of) ;
        }
    }
    of.close() ;
}

//----------------------------------------------------------------------------------------------------
// reloading
//----------------------------------------------------------------------------------------------------

void Aikido::checkByte (int b, std::istream &is) {
    int c = readByte (is) ;
    if (c != b) {
        std::cerr << "bad byte, expected " << b << ", got " << c << "\n" ;
        numErrors++ ;
    }
}

Scope *Aikido::readScope (std::istream &is) {
    int index = readWord (is) ;
    if (index == 0) {
        return NULL ;
    }
    if (loadpass == 1) {
        return NULL ;
    }
    return scopevec[index-1] ;
}

Variable *Scope::findVarAtIndex (int index) {
    for (VarMap::iterator i = variables.begin() ; i != variables.end() ; i++) {
        Variable *var = i->second ;
        if (var->offset == index) {
            return var ;
        }
    }
    std::cerr << "Cant find variable " << index << " in scope\n" ;
    abort() ;
    p->numErrors++ ;
    return NULL ;
}

Variable *Aikido::readVariableRef (std::istream &is) {
    int scopeindex = readWord(is) ;
    if (scopeindex == -1) {
        return NULL ;
    }
    if (loadpass == 1) {
        readWord (is) ;		// read offset
        return NULL ;
    }
    Scope *scope = scopevec[scopeindex-1] ;
    return scope->findVarAtIndex (readWord (is)) ;
}

Tag *Aikido::readTagRef (std::istream &is) {
    Tag *tag = NULL ;
    int t = readWord (is) ;
    if (t != -1) {
        std::string name = readString (is) ;
        if (loadpass == 2) {
            Scope *scope = scopevec[t-1] ;
            Scope *finscope ;
            tag = (Tag*)scope->findVariable (string (name), finscope, VAR_ACCESSFULL, NULL, NULL) ;
            if (tag == NULL) {
                std::cerr << "Cant find tag " << name << "\n" ;
                abort() ;
            }
        }
    }
    return tag ;
}

void Aikido::readTagList (std::list<Tag*> &list, std::istream &is) {
    int size = readWord (is) ;
    while (size-- > 0) {
        Tag *tag = readTagRef (is) ;
        list.push_back (tag) ;
    }
}


void Variable::load (Aikido *aikido, std::istream &is) {
    aikido->checkByte (DUMP_VARIABLE_TYPE, is) ;
    name = aikido->readDirectString (is); 
    flags = aikido->readWord (is) ;
    offset = aikido->readWord (is) ;
    type = (Type)aikido->readWord (is) ;
    //if (aikido->loadpass == 2) {
        //std::cout << "loaded variable " << name << " offset: " << offset << "\n" ;
    //}
}

void Tag::load (Aikido *aikido, std::istream &is) {
    aikido->checkByte (DUMP_TAG_TYPE, is) ;
    Variable::load (aikido, is) ;
    block = (Block*)aikido->readScope (is) ;
    var = aikido->readVariableRef (is) ;
}

void Parameter::load (Aikido *aikido, std::istream &is) {
    aikido->checkByte (DUMP_PARAMETER_TYPE, is) ;
    Variable::load (aikido, is) ;
    def = aikido->readByte (is) ? new Node(aikido, NUMBER, Value((INTEGER)0)) : NULL ;
}

void Reference::load (Aikido *aikido, std::istream &is) {
    aikido->checkByte (DUMP_REFERENCE_TYPE, is) ;
    Parameter::load (aikido, is) ;
}

void EnumConst::load (Aikido *aikido, std::istream &is) {
    aikido->checkByte (DUMP_ENUMCONST_TYPE, is) ;
    Variable::load (aikido, is) ;
    en = (Enum*)aikido->readScope (is) ;
    next = (EnumConst*)aikido->readVariableRef (is) ;
    prev = (EnumConst*)aikido->readVariableRef (is) ;
    value = aikido->readWord (is) ;
    value = (value << 32) | aikido->readWord (is) ;
}

void Scope::load (std::istream &is) {
    p->checkByte (DUMP_SCOPE_TYPE, is) ;

    //std::cout << "loading scope\n" ;
    int nvars = p->readWord (is) ;
    for (int i = 0 ; i < nvars ; i++) {
        int type = p->peekByte (is) ;
        Variable *var = NULL ;
        if (p->loadpass == 1) {
            switch (type) {
            case DUMP_VARIABLE_TYPE:
               var = new Variable() ;
               break ;
            case DUMP_TAG_TYPE:
               var = new Tag() ;
               break ;
            case DUMP_PARAMETER_TYPE:
               var = new Parameter() ;
               break ;
            case DUMP_REFERENCE_TYPE:
               var = new Reference() ;
               break ;
            case DUMP_ENUMCONST_TYPE:
               var = new EnumConst() ;
               break ;
            }
            varlist.push_back (var) ;
            var->load (p, is) ;
            variables[var->name] = var ;		// add to scope
            p->addVariableName (var->name, this, var) ;
        } else {
            var = varlist[i] ;
            var->load (p, is) ;
        }
    }

    // now the scope information
    level = p->readWord (is) ;
    parentScope = p->readScope (is) ;
    majorScope = (Block*)p->readScope (is) ;

    //p->dumpWord (children.size(), is) ;
    //for (Children::iterator c = children.begin() ; c != children.end() ; c++) {
        //p->dumpWord ((*c)->index, is) ;
    //}
    nparas = p->readWord (is) ;
    if (p->loadpass == 1) {
        p->registerScope (this) ;
    }
}

void Block::load (std::istream &is) {
    p->checkByte (DUMP_BLOCK_TYPE, is) ;
    name = p->readString (is) ;
    fullname = p->readString (is) ;
    stacksize = p->readWord (is) ;
    flags = p->readWord (is) ;
    parentClass = (Class*)p->readScope (is) ;
    parentBlock = (Block*)p->readScope (is) ;
    superblock = p->readTagRef (is) ;

    //if (p->loadpass == 2) {
        //std::cout << "loaded block " << name << "\n" ;
    //}
    Scope::load (is) ;		// need hierarchy for variable searches

    p->readTagList (children, is) ;
    p->readTagList (interfaces, is) ;
    type = (Type)p->readWord (is) ;
    varargs = p->readVariableRef (is) ;
    finalize = (InterpretedBlock*)p->readScope (is) ;
    int w = p->readWord (is) ;
    if (w == 0) {
        code = NULL ;
    } else {
        if (p->loadpass == 1) {
            code = new CodeSequence() ;
        }
        code->load (p, is) ;
    }
}

void Function::load (std::istream &is) {
    p->checkByte (DUMP_FUNCTION_TYPE, is) ;
    Block::load (is) ;
}

void InterpretedBlock::load (std::istream &is) {
    p->checkByte (DUMP_IBLOCK_TYPE, is) ;
    Function::load (is) ;
    int np = p->readWord (is) ;
    for (int i = 0 ; i < np ; i++) {
        Parameter *para = (Parameter*)p->readVariableRef (is) ;
        if (p->loadpass == 1) {
            parameters.push_back (para) ;
        } else {
            parameters[i] = para ;
        }
    }
}

void NativeFunction::load (std::istream &is) {
    p->checkByte (DUMP_NF_TYPE, is) ;
    Function::load (is) ;
    paramask =  p->readWord (is) ;

    if (p->loadpass == 1) {
        return ;
    }
    // special cases:
    if (name == "sleep") {
        ptr = Aikido__threadSleep ;
    } else if (name == "setPriority") {
        ptr = Aikido__setThreadPriority ;
    } else if (name == "getPriority") {
        ptr = Aikido__getThreadPriority ;
    } else if (name == "getID") {
        ptr = Aikido__getThreadID ;
    } else if (name == "join") {
        ptr = Aikido__threadJoin ;
    } else if (name == "wait") {
        ptr = Aikido__monitorWait ;
    } else if (name == "timewait") {
        ptr = Aikido__monitorTimewait ;
    } else if (name == "notify") {
        ptr = Aikido__monitorNotify ;
    } else if (name == "notifyAll") {
        ptr = Aikido__monitorNotifyAll ;
    } else {
        string nativename = "Aikido__" + name ;
        const char *symname = nativename.c_str() ;
        void *sym = OS::findSymbol (symname) ;
        if (sym != NULL) {
            ptr = (NativeFunction::FuncPtr)sym ;
        } else {
            std::cerr << "Unable to find native function " << name << " in any shared library\n" ;
            p->numErrors++ ;
        }
    }
}

void RawNativeFunction::load (std::istream &is) {
    p->checkByte (DUMP_RNF_TYPE, is) ;

    p->checkByte (DUMP_NF_TYPE, is) ;		// inline this because the NativeFunction::load shouldn't search
    Function::load (is) ;
    paramask =  p->readWord (is) ;

    if (p->loadpass == 1) {
        return ;
    }
    // look for symbol as a raw name
    void *sym = OS::findSymbol (name.c_str()) ;
    if (sym != NULL) {
        ptr = (NativeFunction::FuncPtr)sym ;
    } else {
        std::cerr << "Unable to find native function " << name << " in any shared library\n" ;
        p->numErrors++ ;
    }
}

void Thread::load (std::istream &is) {
    p->checkByte (DUMP_THREAD_TYPE, is) ;
    InterpretedBlock::load (is) ;
    input = p->readVariableRef (is) ;
    output = p->readVariableRef (is) ;
}

void Class::load (std::istream &is) {
    p->checkByte (DUMP_CLASS_TYPE, is) ;
    InterpretedBlock::load (is) ;
    int no = p->readWord (is) ;			// number of operators
    for (int i = 0 ; i < no ; i++) {
        Token tok = (Token)p->readWord (is) ;
        Scope *s = p->readScope (is) ;
        if (p->loadpass == 2) {
            operators[tok] = (InterpretedBlock*)s ;
        }
    }
}

void MonitorBlock::load (std::istream &is) {
    p->checkByte (DUMP_MONITOR_TYPE, is) ;
    Class::load (is) ;
}

void Package::load (std::istream &is) {
    p->checkByte (DUMP_PACKAGE_TYPE, is) ;
    Class::load (is) ;
}

void Interface::load (std::istream &is) {
    p->checkByte (DUMP_INTERFACE_TYPE, is) ;
    Class::load (is) ;
}

void Enum::load (std::istream &is) {
    p->checkByte (DUMP_ENUM_TYPE, is) ;
    InterpretedBlock::load (is) ;
    int nc = p->readWord (is) ;
    for (int i = 0 ; i < nc ; i++) {
        if (p->loadpass == 1) {
            consts.push_back ((EnumConst*)p->readVariableRef (is)) ;
        } else {
            consts[i] = (EnumConst*)p->readVariableRef (is) ;
        }
    }
}

// load the object code from the file and get the parser and interpreter into a state
// that looks just like the source code has been parsed.

void Aikido::loadObjectCode (std::string filename) {
    std::ifstream is (filename.c_str()) ;
    if (!is) {
        std::cerr << "Unable to open file: " << filename << "\n" ;
        exit (2) ;
    }
    int magic = readWord (is) ;
    if (magic != 0x416b646f) {
        std::cerr << "Not an Aikido object file: " << filename << "\n" ;
        exit (2) ;
    }
    int version = readWord (is) ;
    if (version != 100) {		// check version
        std::cerr << "Aikido object file has incorrect version: " << filename << "\n" ;
        exit (2) ;
    }

    // shared libraries are a hint as it is possible that the library has
    // already been linked into the code and there is no way to know
    // that.  So, if the library can't be found, we are silent about it as
    // the symbol lookups will fail and errors will be given then
    int numlibs = readWord (is) ;
    int err = 0 ;
    for (int i = 0 ; i < numlibs ; i++) {
        std::string name = readDirectString (is) ;
        for (int j = 0 ; j < importpaths.size() ; j++) {
            std::string path = importpaths[j].str + "/" + name ;
            void *handle = OS::openLibrary (path.c_str()) ;
            if (handle != NULL) {
                break ;
            }
        }
    }

    if (err > 0) {
       exit (2) ;
    }

    int resetpoint = is.tellg() ;

    loadpass = 1 ;
    int numscopes = readWord (is) ;

    for (int i = 0 ; i < numscopes ; i++) {
        int start = is.tellg() ;
        //std::cout << "scope " << i << "(" << numscopes << ") at " << start << "\n" ;
        int scopetype = peekByte (is) ;
        Scope *scope = NULL ;
        switch (scopetype) {
        case DUMP_SCOPE_TYPE:
            scope = new Scope(this) ;
            break ;
        case DUMP_BLOCK_TYPE:
            throw "Trying to create an abstract object" ;
            break ;
        case DUMP_NF_TYPE:
            scope = new NativeFunction(this) ;
            break ;
        case DUMP_THREAD_TYPE:
            scope = new Thread(this) ;
            break ;
        case DUMP_IBLOCK_TYPE:
            scope = new InterpretedBlock(this) ;
            break ;
        case DUMP_RNF_TYPE:
            scope = new RawNativeFunction(this) ;
            break ;
        case DUMP_ENUM_TYPE:
            scope = new Enum(this) ;
            break ;
        case DUMP_CLASS_TYPE:
            scope = new Class(this) ;
            break ;
        case DUMP_MONITOR_TYPE:
            scope = new MonitorBlock(this) ;
            break ;
        case DUMP_INTERFACE_TYPE:
            scope = new Interface(this) ;
            break ;
        case DUMP_FUNCTION_TYPE:
            throw "Trying to create an abstract object" ;
            break ;
        case DUMP_PACKAGE_TYPE:
            scope = new Package(this) ;
            break ;
        default:
            std::cerr << "Bad scope type byte: " << scopetype << " at offset " << is.tellg() << "\n" ;
            abort() ;
        }
        scope->load (is) ;
    }

    // allocate code for preamble
    preamble = new CodeSequence() ;
    preamble->load (this, is) ;

    // load the string pool

    stringvec.clear() ;			// just in case
    int nstrings = readWord (is) ;
    for (int i = 0 ; i < nstrings ; i++) {
        int len = readWord (is) ;
        std::string s = "" ;
        while (len-- > 0) {
            s += (char)readByte(is) ;
        }
        stringvec.push_back (s) ;
    }

    // second pass, reset stream and go for it
#if !defined(_CC_GCC)
    is.rdbuf()->pubseekoff (resetpoint, std::ios_base::beg) ;
#else
    is.rdbuf()->pubseekoff (resetpoint, std::ios::beg) ;
#endif

    loadpass = 2 ;
    numscopes = readWord (is) ;
    for (int i = 0 ; i < numscopes ; i++) {
       Scope *scope = scopevec[i] ;
       scope->load (is) ;
    }
    preamble->load (this, is) ;

    is.close() ;
}

//
// string pool
//

int Aikido::poolString (std::string &s) {
    StringMap::iterator i = stringmap.find (s) ;
    int index = -1 ;
    if (i == stringmap.end()) {
        index = stringvec.size() ;
        stringmap[s] = index ;		// index into vec
        stringvec.push_back (s) ;
    } else {
        index = i->second ;
    }
    return index ;
}

std::string &Aikido::lookupString (int index) {
    return stringvec[index] ;
}


void Aikido::set_alias(std::string name, std::string value, std::ostream &os) {
    AliasMap::iterator i = aliases.find (name) ;
    if (i != aliases.end()) {
        i->second = value ;
    } else {
        aliases[name] = value ;
    }
}

void Aikido::show_alias (std::string name, std::ostream &os) {
    if (name == "") {
        for (AliasMap::iterator i = aliases.begin() ; i != aliases.end() ; i++) {
            os << i->first << " = " << i->second << "\n" ;
        }
    } else {
        AliasMap::iterator i = aliases.find (name) ;
        if (i != aliases.end()) {
            os << name << " = " << i->second << "\n" ;
        } else {
            error ("No such alias %s", name.c_str()) ;
        }
    }
    
}

void Aikido::unset_alias (std::string name, std::ostream &os) {
    AliasMap::iterator i = aliases.find (name) ;
    if (i != aliases.end()) {
        aliases.erase (i) ;
    } else {
        error ("No such alias %s", name.c_str()) ;
    }
}

bool Aikido::find_alias (std::string name, std::string &value) {
    AliasMap::iterator i = aliases.find (name) ;
    if (i != aliases.end()) {
        value = i->second ;
        return true ;
    } else {
        return false ;
    }
}

// expand the alias value given the tail of the command.  Args are $1...

void Aikido::expand_alias (std::string aliasvalue, std::string &cmd) {
    std::vector<std::string> args ;
    int i = 0 ;
    std::string incmd = cmd ;
    int size = incmd.size() ;
    
    // collect the args into the args vector
    while (i < size) {
        std::string arg ;
        while (i < size && isspace (incmd[i])) {
            i++ ;
        }
        if (i == incmd.size()) {
            break ;
        }
        if (incmd[i] == '"') {          // quoted command
            i++ ;
            arg += '"' ;
            while (i < size && incmd[i] != '"') {
                if (incmd[i] == '\\') {
                    i++ ;
                    if (i < size) {
                        arg += incmd[i++] ;
                    }
                } else {
                    arg += incmd[i++] ;
                }
            }
            if (i < size) {
                i++ ;
            }
            arg += '"' ;
        } else {
            while (i < size && !isspace (incmd[i])) {
                arg += incmd[i++] ;
            }
        }
        args.push_back (arg) ;
    }
    
    // now that we have the args, insert them into the aliasvalue
    
    i = 0 ;
    cmd = "" ;
    size = aliasvalue.size() ;
    while (i < size) {
        if (aliasvalue[i] == '\\') {
            i++ ;
            if (i < size) {
                cmd += aliasvalue[i++] ; 
            }
        } else if (aliasvalue[i] == '$') {
            i++ ;
            if (i < size && aliasvalue[i] == '[') {
                // $[range] of args
                int n1 = 0 ;
                int n2 = 0;
                i++ ;
                if (i < size && isdigit (aliasvalue[i])) {
                    while (i < size && isdigit(aliasvalue[i])) {
                        n1 = n1 * 10 + aliasvalue[i++] - '0' ;
                    }
                    
                } else {
                    n1 = 1 ;
                }
                if (i < size && aliasvalue[i] == ':') { 
                    i++ ;
                    if (i < size && isdigit(aliasvalue[i])) {
                        while (i < size && isdigit(aliasvalue[i])) {
                            n2 = n2 * 10 + aliasvalue[i++] - '0' ;
                        }
                    } else {
                        n2 = 1000 ;
                    }
                }
                if (i < size && aliasvalue[i] == ']') {
                    i++ ;
                }
                int n = n1 ;
                while (n <= n2 && n <= args.size()) {
                    if (n == 0) {      // $0 means all args
                        cmd += incmd ;
                    } else {
                        cmd += args[n-1] ;
                    }
                    cmd += ' ' ;        // space between args
                    n++ ;
                }
            } else {
                int n = 0 ;
                while (i < size && isdigit(aliasvalue[i])) {
                    n = n * 10 + aliasvalue[i++] - '0' ;
                }
                if (n == 0) {      // $0 means all args
                    cmd += incmd ;
                } else {
                    if (n <= args.size()) {
                        cmd += args[n-1] ;
                    }
                }
            }
        } else {
            cmd += aliasvalue[i++] ;
        }
    }
}

//
// loopback worker
//
void LoopbackWorker::initialize() {
    if (parserDelegate == NULL) {
        return;
    }
    std::vector<Value> args;
    aikido->call (parserDelegate, "initialize", args);
}

void LoopbackWorker::parse(std::istream &in, int scopelevel, int pass, std::ostream &out, std::istream &input, WorkerContext *ctx)  {
    if (parserDelegate == NULL) {
        return;
    }
    Scope *scope ;
    Tag *tag = (Tag*)parserDelegate->block->findVariable (".parse", scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (tag != NULL) {
        Operand tmp ;
        // build arguments for function
        Value arg1 (new StdInStream (&in));
        Value arg2 (new StdOutStream (&out));
        Value arg3 (new StdInStream (&input));
        aikido->vm->callFunction (&tmp, tag->block, parserDelegate, arg1, arg2, arg3) ;
    }
}

void LoopbackWorker::execute()  {
    if (parserDelegate == NULL) {
        return;
    }
    std::vector<Value> args;
    aikido->call (parserDelegate, "execute", args);
}

void LoopbackWorker::finalize()  {
}

void LoopbackWorker::print (std::ostream &os)  {
}

bool LoopbackWorker::isClaimed (std::string s)  {
    if (parserDelegate == NULL) {
        return false;
    }
    Context *saved = aikido->currentContext;

    std::vector<Value> args;
    args.push_back (Value(s));
    try {
        Value v = aikido->call (parserDelegate, "isClaimed", args);
        aikido->currentContext = saved;
        return v.integer;
    } catch (...) {
        aikido->currentContext = saved;
        throw;
    }
}

void LoopbackWorker::preparePass (int pass)  {
}




}


