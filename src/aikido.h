/*
 * aikido.h
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
 * Version:  1.78
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */

// CHANGE LOG
// 1/21/2004	David Allison of PathScale Inc.
//              Added insignalhandler 
//		Added object code dumping and reloading (extensive changes)
// 10/21/2004   David Allison           Added closures



//
// Aikido interpreter
// Author : David Allison
// Version: 1.78 07/29/03
//

// this is the main header file for the aikido interpreter and parser
// The classes defined here are:


// string					- reference counted string

// Stream					- base class for a stream
//	StdStream				- standard stream
//	StdOutStream				- standard output stream
//	StdInStream				- standard input stream
//	PipeStream				- bidirectional stream used for networks
//	ThreadStream				- stream use to communicate with threads

// ForeignContext				- holds information on a context for a tree node

// Value					- holds one value in a program
// 	Value::map				- a map of value/value pairs
//	Value::vector				- a vector of values

// Variable					- information about a variable
// 	Tag					- information about a block tag
//	Parameter				- information on a block parameters
//		Reference			- reference parameter
//	EnumConst				- a constant in an enumeration

// MemberCache					- dynamic lookup cache for blocks

// StackFrame					- holds variable values
//	Object					- an instance of a class
//		Monitor				- an instance of a monitor

// Scope					- base class for blocks - holds variables
//	Block					- named block in program
//		Function			- a function
//			InterpretedBlock	- an interpreted block (code generated for this)
//				Thread		- a thread block
//				Class		- a class block
//					Package - a package block
//					MonitorBlock	- a monitor block
//					Interface	- an interface
//				Enum		- enumeration block
//			NativeFunction		- a native function
//	Macro					- a statement level macro definition

// Exception					- interpreter exception

// Node						- parse tree node

// Context					- where we are in a program

// TokenNode					- one token for the lexical analyzer

// Aikido					- main class, holds everything

// Worker					- handles things that are not part of the language

#ifndef __AIKIDO_H
#define __AIKIDO_H

#include "aikidotoken.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stack>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string.h>

#include "aikidozip.h"

#if defined(_CC_GCC) && __GNUC__ == 2
#include <strstream>
#else
#include <sstream>
#endif

#include <map>
#include <set>
#include <string>
#include <vector>
#include <list>
#include <setjmp.h>
#include <signal.h>
#include <sys/types.h>

//
// Operating system dependent
//

// default the macros to Sun CC on Solaris
#if !defined(_OS_SOLARIS) && !defined(_OS_LINUX) && !defined(_OS_WINDOWS) && !defined(_OS_MACOSX)
#define _OS_SOLARIS
#endif

#if !defined(_CC_SUN) && !defined(_CC_GCC) && !defined(_CC_BORLAND)
#define _CC_SUN
#endif

#if defined(_OS_SOLARIS)
#include "unix/os.h"
#define AIKIDO_EXPORT 
#endif

#if defined(_OS_LINUX)
#include "unix/os.h"
#define AIKIDO_EXPORT 
#endif

#if defined(_OS_MACOSX)
#include "unix/os.h"
#define AIKIDO_EXPORT 
#endif

#if defined(_OS_WINDOWS)
#include "win32/os.h"
#define AIKIDO_EXPORT __declspec(dllexport)
#endif

#ifndef AIKIDO_EXPORT
#define AIKIDO_EXPORT
#endif

namespace aikido {

extern std::string programname ;		// name of aikido executable
extern std::string current_directory ;          // current directory name

extern bool interrupted;
#ifdef _32BIT
typedef int INTEGER ;
typedef unsigned int UINTEGER ;
#else
#if defined(_OS_WINDOWS)
typedef __int64 INTEGER ;
typedef unsigned __int64 UINTEGER ;
#else
typedef long long INTEGER ;
typedef unsigned long long UINTEGER ;
#endif
#endif

#define VERSION 302
const int version_number = VERSION ;

// set of properties

const int MULTITHREADED = 1 ;		// application allows threads
const int INVENT_VARS = 2 ;		// undeclared vars must be invented and no errors
const int NOCPP = 4 ;			// cpp is not being run, treat # as comment
const int DEBUG = 8 ;			// run debugger
const int INVENT_WARNING = 16 ;		// warning for invented variables
const int TRACE = 32 ;			// trace execution
const int VERBOSE = 64 ;		// verbose mode
const int INVENT_IN_MAIN = 128 ;        // invent vars in top level scope only
const int STATIC_ONLY = 256 ;		// static variables only allowed in expression
const int ALLOW_OMITTED_PARENS = 512 ;  // allow parens to be omitted for if, while etc
const int TRACEBACK = 1024 ;		// trace stack on exception
const int COMPILE = 2048 ;		// compile code
const int LOCKDOWN = 4096 ;             // security lockdown
const int CLI = 8192 ;                  // running in cli mode

extern int verbosegc ;			// verbose garbage collection
extern int nogc ;			// garbage collection off
extern bool interrupt ;			// ^c hit in debugger
extern bool noaccesscheck ;		// don't perform access check


#define Expression Node

// forward declations for classes used
class Aikido ;
class Worker ;
class LoopbackWorker ;
struct Function ;
struct Node ;
struct Stream ;
struct Thread ;
struct Value ;
struct Class ;
struct Interface ;
struct Object ;
struct Package ;
struct Block ;
struct Enum ;
struct EnumConst ;
struct MonitorBlock ;
class Macro ;
class Scope ;
struct InterpretedBlock ;
struct StackFrame ;
struct StaticLink ;
struct Variable ;
struct Closure ;
struct map ;
struct Annotation;
typedef std::list<Annotation*> AnnotationList;

// in aikidocodegen.h
namespace codegen {
    class CodeGenerator ;
    struct CodeSequence ;
    struct Instruction ;
}

class Debugger ;		// in aikidodebug.h
class Tracer ;		// in aikidodebug.h

using codegen::Instruction ;
using codegen::CodeSequence ;

class VirtualMachine ;		// in aikidointerpret.h

// the type that can be taken by a value
// if the bottom bit is set then it is an integral type
enum Type {
     T_INTEGER = 1,		// regular 64 bit integer
     T_BYTE = 3,		// 8 bit unsigned integer
     T_CHAR = 5,		// character
     T_BOOL = 7,		// boolean
     T_REAL = 6,		// floating point
     T_ADDRESS = 8,		// address of another value
     T_ENUMCONST = 10,	// enumerated constant

     // ref counted but not blocks
     T_STRING = 12,		// string
     T_VECTOR = 14,		// vector of Values
     T_MAP = 16,		// map of Value:Value
     T_BYTEVECTOR = 18, 	// vector of bytes
     T_STREAM = 20,		// stream
     T_OBJECT = 22,		// user defined object

     // ref counted blocks
     T_FUNCTION = 24,	// function
     T_THREAD = 26,		// thread
     T_CLASS = 28,		// user defined type
     T_PACKAGE = 30,		// package enclosure
     T_ENUM = 32,		// enumerated type
     T_MONITOR = 34,		// monitor block
     T_INTERFACE = 36,		// interface
     T_MEMORY = 38, 		// raw memory
     T_POINTER = 40,		// pointer to raw memory
     T_CLOSURE = 42,            // closure

     T_NONE = 0		// no value
} ;

// a location in the source code (filename and linenumber)
class SourceLocation {
public:
    SourceLocation (const char *fn, int l) : filename(fn), lineno(l) {}
    ~SourceLocation() {}

    const char *filename ;
    int lineno ;
} ;

//
// registry of all source locations (pooled)
//

class SourceRegistry {
public:
    SourceRegistry() ;
    ~SourceRegistry() ;

    SourceLocation *newSourceLocation (const std::string &filename, int lineno) ;
    void removeFile (const std::string &filename) ;

private:
    typedef std::map<int, SourceLocation*> LineMap ;
    typedef std::map<std::string, LineMap*> FileMap ;

    FileMap files ;

    LineMap *findFile (const std::string &filename) ;
    LineMap *registerFile (const std::string &filename) ;
    
    void addLine (const std::string &filename, LineMap *map, int line) ;

    std::string lastfilename ;
    int lastline ;
    SourceLocation *lastlocation ;
} ;

extern SourceRegistry sourceRegistry ;


#define isRefCounted(t) (t >= T_STRING && t <= T_CLOSURE)

#ifdef GCDEBUG
//
// object registry for tracking leaks
//

struct ObjReg {
    ObjReg() : type (T_NONE), obj(NULL)  {}
    ObjReg (Type t, void *o) : type (t), obj(o) {}
    Type type ;
    void *obj ;
} ;

typedef std::list<ObjReg> ObjectRegistry ;
extern ObjectRegistry objectRegistry ;
extern bool registryon ;

inline void registerObject (Type type, void *obj) {
    if (registryon) {
        objectRegistry.push_back (ObjReg (type, obj)) ;
    }
}

inline void unregisterObject (void *obj) {
    if (!registryon) {
        return ;
    }
    ObjectRegistry::iterator i = objectRegistry.begin() ;
    for ( ; i != objectRegistry.end() ; i++) {
        if (i->obj == obj) {
            objectRegistry.erase (i) ;
            return ;
        }
    }
    std::cerr << "Unknown object " << obj << "\n" ;
}

#endif

//
// a garbage-collected object.  This is a thread-safe mutex locked reference
// counter
//

extern OSMutex AIKIDO_EXPORT heapLock ;
extern int AIKIDO_EXPORT numThreads ;

struct GCObject {
    GCObject() : ref (0), pad(0) {}		
    virtual ~GCObject() {
#if 0
        //std::cout << "deleting GCObject " << this << '\n' ;
        if (ref == 0xdeadbeef) {
            throw "double deletion of object" ;
        }
        ref = 0xdeadbeef ;
#endif
    }
    int ref ;
    int pad ;				// to align to 8 byte size and stop RTC complaining
    void incRef(const char *type = NULL) {
#if defined(_OS_WINDOWS)
		InterlockedIncrement ((volatile LONG *)&ref);
#if 0
        if (numThreads > 0) {
            heapLock.lock() ;
            ref++ ;
           // std::cout << "incRef for " << this << " to " << ref << '\n' ;
            heapLock.unlock() ;
        } else {
            ref++ ;
			//std::cout << "incRef for " << this << " to " << ref << '\n' ;
        }
#endif
#else
#if (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4))
                ::__gnu_cxx::__atomic_add (&ref, 1) ;       // one more ref
#else
                __atomic_add (&ref, 1) ;       // one more ref
#endif

#endif
    }
    int decRef(const char *type = NULL) {
#if defined(_OS_WINDOWS)
		return InterlockedDecrement ((volatile LONG *)&ref);
#if 0
        if (numThreads > 0) {
            heapLock.lock() ;
            int x = --ref ;
           // std::cout << "decRef for " << this << " to " << ref << '\n' ;
            heapLock.unlock() ;
            return x ;
        } else {
            int x = --ref ;
			//std::cout << "decRef for " << this << " to " << ref << '\n' ;
			return x;

        }
#endif
#else
#if (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4))
                return ::__gnu_cxx::__exchange_and_add (&ref, -1) - 1;       // one less ref
#else
                return __exchange_and_add (&ref, -1) - 1;       // one less ref
#endif

#endif
    }
} ;

// GC macros
#define incRef(o,type) o->incRef()
#define decRef(o,type) o->decRef()
#define GCDELETE(o,type) if (!nogc) { delete (o) ; }

void incValueRef (Value &v);
void decValueRef (Value &v);

//
// extension of std::string to provide for reference counting
//

struct string : public GCObject {
    string (char *s, int len): str (s, len) { init () ; }
    string (const char *s, int len): str (s, len) { init() ; }
    string (char *s): str (s) { init () ; }
    string (const char *s): str (s) { init() ; }
    string (string &s) : str (s.str){ init() ; }
    string() { init() ; }
    string (const string &s) : str (s.str){ init() ; }
    string (const std::string &s) : str (s){ init() ; }

    void init() {
//        char buf[1024] ;
//        sprintf (buf, "new string \"%s\" (%x)\n", str.c_str(), this) ;
//        std::cout << "new string \"" << str << "\" (" << (void*)this << ")\n" ;
//        std::cout << buf ;
#ifdef GCDEBUG
        registerObject (T_STRING, this) ;
#endif
    }

    ~string() {
#ifdef GCDEBUG
        unregisterObject (this) ;
#endif
    }
    
    void push_back (char c ) {
#if !defined(_CC_GCC)
        str.push_back (c) ;
#else
        str += c ;
#endif
    }

    bool operator== (const string &s) const {
		return str == s.str ;
    }
    bool operator!= (string &s) const {
		return str != s.str ;
    }
    bool operator== (const char *s) const {
		return str == s ;
    }
    bool operator!= (const char *s) const {
		return str != s ;
    }
    bool operator== (char *s) const {
		return str == s ;
    }
    bool operator!= (char *s) const {
		return str != s ;
    }
    bool operator< (const string &s) const {
		return str < s.str ;
    }
    bool operator> (const string &s) const {
		return str > s.str ;
    }
    bool operator<= (const string &s) const {
		return str <= s.str ;
    }
    bool operator>= (const string &s) const {
		return str >= s.str ;
    }

    std::string::size_type find (const char *s) {
        return str.find (s) ;
    }

    std::string::size_type find (char c) {
        return str.find (c) ;
    }

    string operator+ (const string &s) const {
	string r (str + s.str) ;
	return r ;
    }

    string operator+ (const char *s) const {
	   string r (str + s) ;
	return r ;
    }
    string operator+ (char *s) const {
	   string r (str + s) ;
	return r ;
    }

    string &operator += (const string &s) {
		str += s.str ;
	return *this ;
    }

    string &operator += (const char *s) {
	str += s ;
	return *this ;
    }

    string &operator += (char *s) {
	str += s ;
	return *this ;
    }

    string &operator+= (char c) {
		str += c ;
	return *this ;
    }

    char operator[] (int i) const {
        return str[i] ;
    }

    void setchar (int i, char c) {
        str[i] = c ;
    }

    int size() { return str.size() ; }
    const char *data() { return str.data() ; }
    const char *c_str() const { return str.c_str() ; }
    string erase (int s, int e) {
	std::string r = str.erase (s, e) ;
	return string (r) ;
    }

    string replace (int i, int j, string s) {
        return string (str.replace (i, j, s.str)) ;
    }

    string substr (int i, int l) {
	    std::string r = str.substr (i, l) ;
	    return string (r) ;
    }

    string substr (int i) {
	    std::string r = str.substr (i) ;
	    return string (r) ;
    }

    string &append (const string &s) {
        str.append (s.str) ;
        return *this ;
    }

    string &insert (int p, const string &s) {
        str.insert (p, s.str) ;
        return *this ;
    }

    string &insert (int p, const std::string &s) {
        str.insert (p, s) ;
        return *this ;
    }

    int length() { return str.length() ; }
    std::string str ;

    
} ;

inline string operator+ (const char *s1, const string &s2) {
    return string (s1 + s2.str) ;
}

inline bool operator== (const std::string &s1, const string &s2) {
    return s1 == s2.str ;
}


//
// raw memory, reference counted
//

class RawMemory : public GCObject {
public:
    RawMemory (int sz) : size (sz) {
        mem = new char [sz] ;
    }
    ~RawMemory() {
        delete [] mem ;
    }

    int size ;
    char *mem ;
} ;

// 
// a raw pointer is a reference to raw memory and is generated when
// a raw memory is added to or subtracted from
//
class RawPointer : public GCObject {
public:
    RawPointer (RawMemory *m, int off) : mem (m), offset (off) {
        incRef (mem, memory) ;
    }
    ~RawPointer() {
        if (decRef (mem, memory) == 0) {
             if (verbosegc) {
                 std::cerr << "gc: deleting raw memory " << (void*)mem << '\n' ;
             }
             GCDELETE(mem,memory) ;
        }
    }
    RawMemory *mem ;		// memory referred to
    int offset ;		// offset into memory
} ;

//
// simple encapsulation for reference counted streams
//

AIKIDO_EXPORT struct Stream: public GCObject  {
    static const int LINEMODE = 1 ;		// stream operating in line mode
    static const int CHARMODE = 2 ;		// stream operating in character mode
    Stream (): mode (LINEMODE), autoflush (false) {
#ifdef GCDEBUG
        registerObject (T_STREAM, this) ;
#endif
    }
    virtual ~Stream() {
#ifdef GCDEBUG
        unregisterObject (this) ;
#endif
    }
    virtual bool eof() = 0 ;
    virtual char get() = 0 ;
    virtual void unget() = 0 ;
    virtual int get (char *buf, int n) = 0 ;
    virtual void put (char c) = 0 ;
    virtual void put (char *buf, int n) = 0 ;
    virtual void close() = 0 ;
    virtual void flush() = 0 ;
    virtual int AIKIDO_EXPORT seek (int offset, int whence) = 0 ;
    virtual void AIKIDO_EXPORT rewind() = 0 ;
    virtual int availableChars() = 0 ;
    virtual void AIKIDO_EXPORT readValue (Value &v) = 0 ;
    virtual void AIKIDO_EXPORT writeValue (Value &v) = 0 ;
    virtual int getInputFD() = 0 ;
    virtual OSThread_t getThreadID() { return (OSThread_t)-1 ; }
    virtual void setBufferSize (int size) {}
    virtual std::istream &getInStream() = 0 ;
    virtual std::ostream &getOutStream() = 0 ;
    void setMode (int m) { if (m == 0) mode = LINEMODE ; else if (m == 1) mode = CHARMODE ; }
    void setAutoFlush (int f) { autoflush = f ; }

    int mode ;
    bool autoflush ;
} ;


struct StdStream : public Stream {
    StdStream (std::iostream *s): stream (s) {
    }
    ~StdStream() {
       delete stream ;
    }
    bool eof() { return stream->eof() ; }
    char get() { return stream->get() ; }
    void unget() { stream->unget() ;}
    int get (char *buf, int n) { stream->read (buf, n) ; return stream->gcount() ; } 
    void put (char c) { stream->put(c) ; }
    void put (char *buf, int n) { stream->write (buf, n) ; }
    void close() { static_cast<std::fstream*>(stream)->close(); }
    void flush() { stream->flush() ; } 
    int AIKIDO_EXPORT seek (int offset, int whence) ;
    void AIKIDO_EXPORT rewind()  ;
    int availableChars() { return (int)stream->rdbuf()->in_avail() ; }
    void AIKIDO_EXPORT readValue (Value &v) ;
    void AIKIDO_EXPORT writeValue (Value &v) ;
#if defined (_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    int getInputFD() { return static_cast<__gnu_cxx::stdio_filebuf<char>*>(stream->rdbuf())->fd() ; }
#elif defined (_OS_WINDOWS)
	int getInputFD() { return 0 ; }
#else
    int getInputFD() { return static_cast<std::filebuf*>(stream->rdbuf())->fd() ; }
#endif
    void setBufferSize (int size) {
#if defined(_CC_GCC) && __GNUC__ == 2
        stream->rdbuf()->setbuf (NULL, size) ;
#else
        stream->rdbuf()->pubsetbuf (NULL, (std::streamsize)size) ;
#endif
    }
    std::istream &getInStream() { return *stream ; }
    std::ostream &getOutStream() { return *stream ; }
    std::iostream *stream ;
} ;

struct StdOutStream : public Stream {
    StdOutStream (std::ostream *s);
    ~StdOutStream() {
    }
    bool eof() { return stream->eof() ; }
    char get() { throw "Cannot get from output stream" ; }
    int get (char *buf, int n) { throw "Cannot get from output stream" ;  } 
    void unget() {  throw "Cannot unget from output stream" ; }
    void put (char c) { stream->put(c) ; }
    void put (char *buf, int n) { stream->write (buf, n) ; }
    void close() { }
    void flush() { stream->flush() ; } 
    int AIKIDO_EXPORT seek (int offset, int whence) ;
    void AIKIDO_EXPORT rewind()  ;
    int availableChars() { return (int)stream->rdbuf()->in_avail() ; }
    void AIKIDO_EXPORT readValue (Value &v) ;
    void AIKIDO_EXPORT writeValue (Value &v) ;
#if defined (_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    int getInputFD() { return static_cast<__gnu_cxx::stdio_filebuf<char>*>(stream->rdbuf())->fd() ; }
#elif defined (_OS_WINDOWS)
	int getInputFD() { return 1 ; }
#else
    int getInputFD() { return static_cast<std::filebuf*>(stream->rdbuf())->fd() ; }
#endif
    void setBufferSize (int size) {
#if defined(_CC_GCC) && __GNUC__ == 2
        stream->rdbuf()->setbuf (NULL, size) ;
#else
        stream->rdbuf()->pubsetbuf (NULL, (std::streamsize)size) ;
#endif
    }
    std::istream &getInStream() { throw "output stream has no input component" ; }
    std::ostream &getOutStream() { return *stream ; }
    std::ostream *stream ;
} ;

struct StdInStream : public Stream {
    StdInStream (std::istream *s) ;
    ~StdInStream() {
    }
    bool eof() { return stream->eof() ; }
    char get() { return stream->get() ; }
    int get (char *buf, int n) { stream->read (buf, n) ; return stream->gcount() ; } 
    void unget() { stream->unget() ; }
    void put (char c) { throw "Cannot put to input stream" ; }
    void put (char *buf, int n) { throw "Cannot put to input stream" ; }
    void close() { }
    void flush() { throw "Cannot flush input stream" ; } 
    int AIKIDO_EXPORT seek (int offset, int whence) ;
    void AIKIDO_EXPORT rewind()  ;
    int availableChars() { return (int)stream->rdbuf()->in_avail() ; }
    void AIKIDO_EXPORT readValue (Value &v) ;
    void AIKIDO_EXPORT writeValue (Value &v) ;
#if defined (_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    int getInputFD() { return static_cast<__gnu_cxx::stdio_filebuf<char>*>(stream->rdbuf())->fd() ; }
#elif defined (_OS_WINDOWS)
	int getInputFD() { return 1 ; }
#else
    int getInputFD() { return static_cast<std::filebuf*>(stream->rdbuf())->fd() ; }
#endif
    void setBufferSize (int size) {
#if defined(_CC_GCC) && __GNUC__ == 2
        stream->rdbuf()->setbuf (NULL, size) ;
#else
        stream->rdbuf()->pubsetbuf (NULL, (std::streamsize)size) ;
#endif
    }
    std::istream &getInStream() { return *stream ; }
    std::ostream &getOutStream() { throw "output stream has no output component" ; }
    std::istream *stream ;
} ;



struct PipeStream : public Stream {
    PipeStream (int in, int out): input (in), output (out), threadID ((OSThread_t)-1) {
    }
    ~PipeStream() {
    }
    void setThreadID (OSThread_t id) { threadID = id ; }

    bool eof() { return input.eof() ; }
    char get() { return input.get() ; }
    int get (char *buf, int n) { input.read (buf, n) ; return input.gcount() ; } 
    void unget() { input.unget() ; }
    void put (char c) { output.put(c) ; }
    void put (char *buf, int n) { output.write (buf, n) ; }
#if defined (_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    void close() { }            // fd_istreams close on destruction and closing twice is bad
#else
    void close() { input.close() ; output.close() ; }
#endif
    void flush() { output.flush() ; }
    int AIKIDO_EXPORT seek (int offset, int whence) ;
    void AIKIDO_EXPORT rewind()  ;
    int availableChars() { return (int)input.rdbuf()->in_avail() ; } 
    void AIKIDO_EXPORT readValue (Value &v) ;
    void AIKIDO_EXPORT writeValue (Value &v) ;
#if defined (_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    int getInputFD() { return static_cast<__gnu_cxx::stdio_filebuf<char>*>(input.rdbuf())->fd() ; }
#elif defined (_OS_WINDOWS)
	int getInputFD() { return input.fd() ; }
#else
    int getInputFD() { return static_cast<std::filebuf*>(input.rdbuf())->fd() ; }
#endif
    OSThread_t getThreadID() { return threadID ; }
    void setBufferSize (int size) {
#if defined(_CC_GCC) && __GNUC__ == 2
        input.rdbuf()->setbuf (NULL, size) ;
        output.rdbuf()->setbuf (NULL, size) ;
#else
        input.rdbuf()->pubsetbuf (NULL, (std::streamsize)size) ;
        output.rdbuf()->pubsetbuf (NULL, (std::streamsize)size) ;
#endif
    }
    std::istream &getInStream() { return input ; }
    std::ostream &getOutStream() { return output ; }

    fd_ifstream input ;
    fd_ofstream output ;
    OSThread_t threadID ;
} ;


// a thread has a stream associated with it.  This stream is implemented by
// an operating system file descriptor and is therefore expensive in terms
// of operating system resources.  If we create a pipe for every thread
// we will limit the number of threads that may be created.  So, instead,
// we defer the creation of the pipe until the thread actually uses its
// stream.  Since most threads don't use the streams this will save resources.

struct ThreadStream : public Stream {
    ThreadStream (Aikido *aikido, ThreadStream *peer, Thread *thread, StackFrame *frame) ;
    ~ThreadStream() ;
    void setThreadID (OSThread_t id) { threadID = id ; }

    bool eof() { checkstreams() ; return input->eof() ; }
    char get() { checkstreams() ;return input->get() ; }
    int get (char *buf, int n) { checkstreams() ; input->read (buf, n) ; return input->gcount() ; } 
    void unget() { checkstreams() ; input->unget() ; }
    void put (char c) { checkstreams() ;output->put(c) ; }
    void put (char *buf, int n) { checkstreams() ; output->write (buf, n) ; }
#if defined (_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    void close() { }
#else
    void close() { checkstreams() ;input->close() ; output->close() ; }
#endif
    void flush() { checkstreams() ;output->flush() ; }
    int AIKIDO_EXPORT seek (int offset, int whence) ;
    void AIKIDO_EXPORT rewind()  ;
    int availableChars() { checkstreams() ;return (int)input->rdbuf()->in_avail() ; } 
    void AIKIDO_EXPORT readValue (Value &v) ;
    void AIKIDO_EXPORT writeValue (Value &v) ;
#if defined (_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    int getInputFD() { return static_cast<__gnu_cxx::stdio_filebuf<char>*>(input->rdbuf())->fd() ; }
#elif defined (_OS_WINDOWS)
	int getInputFD() { return input->fd(); }
#else
    int getInputFD() { return static_cast<std::filebuf*>(input->rdbuf())->fd() ; }
#endif
    OSThread_t getThreadID() { return threadID ; }
    void setBufferSize (int size) {
        checkstreams() ;
#if defined(_CC_GCC) && __GNUC__ == 2
        input->rdbuf()->setbuf (NULL, size) ;
        output->rdbuf()->setbuf (NULL, size) ;
#else
        input->rdbuf()->pubsetbuf (NULL, (std::streamsize)size) ;
        output->rdbuf()->pubsetbuf (NULL, (std::streamsize)size) ;
#endif
    }
    std::istream &getInStream() { return *input ; }
    std::ostream &getOutStream() { return *output ; }

    fd_ifstream *input ;
    fd_ofstream *output ;
    OSThread_t threadID ;
    OSMutex lock ;
    bool connected ;
    ThreadStream *peer ;
    Aikido *aikido ;
    Thread *thread ;
    StackFrame *stack ;

    void connect() ;

    // check if the stream is connected
    void checkstreams() {
        lock.lock() ;
        if (!connected) {
            connect() ;
        }
        lock.unlock() ;
    }

} ;

//
// context for code that is not handled by aikido
//

struct ForeignContext {
    ForeignContext (string f, int l) : filename (f), lineno (l) {}
    string filename ;
    int lineno ;
} ;

//
// a Value.  This is the basic value of anything in a aikido program.
//
AIKIDO_EXPORT struct Value {
    typedef std::vector<Value> ValueVec ;
    typedef std::vector<unsigned char> ByteVec ;

    // a vector of values
    struct vector : public GCObject {
        vector() { ref = 0 ; init() ;}
        vector(int size): vec (size) { ref = 0 ; init() ;}
        void init() {
#ifdef GCDEBUG
             registerObject (T_VECTOR, this) ;
#endif
        }
        ~vector() {
#ifdef GCDEBUG
             unregisterObject ( this) ;
#endif
        }
        typedef ValueVec::iterator iterator ;
        int size() { return vec.size() ; }
        iterator begin() { return vec.begin() ; }
        iterator end() { return vec.end() ; }
        void clear() { vec.clear() ; }
        void push_back (const Value &v) { vec.push_back (v) ; }
        void push_front (const Value &v) { vec.insert (vec.begin(), v) ; }
        Value &operator[] (int i) { return vec[i] ; }
        vector *erase (ValueVec::iterator s, ValueVec::iterator e) { vec.erase (s, e) ; return this ; }
        ValueVec vec ;
        //Value elementtype;          // type of elements (none means don't check)
    } ;

    // a vector of bytes
    struct bytevector : public GCObject {
        bytevector() { ref = 0 ; init() ;}
        bytevector(int size): vec (size) { ref = 0 ; init() ;}
        void init() {
#ifdef GCDEBUG
             registerObject (T_BYTEVECTOR, this) ;
#endif
        }
        ~bytevector() {
#ifdef GCDEBUG
             unregisterObject ( this) ;
#endif
        }
        typedef ByteVec::iterator iterator ;
        int size() { return vec.size() ; }
        iterator begin() { return vec.begin() ; }
        iterator end() { return vec.end() ; }
        void clear() { vec.clear() ; }
        void push_back (unsigned char v) { vec.push_back (v) ; }
        void push_front (unsigned char v) { vec.insert (vec.begin(), v) ; }
        unsigned char &operator[] (int i) { return vec[i] ; }
        bytevector *erase (ByteVec::iterator s, ByteVec::iterator e) { vec.erase (s, e) ; return this ; }
        ByteVec vec ;
    } ;

    Value() { 
       integer = 0 ; 
       type = T_NONE ; 
       }
#ifndef _32BIT
    Value (int i) { integer = i ; type = T_INTEGER ; }
    Value (unsigned int i) { integer = i ; type = T_INTEGER ; }
#endif
    Value (unsigned long i) { integer = i ; type = T_INTEGER ; }
    Value (long i) { integer = i ; type = T_INTEGER ; }
    Value (char c) { integer = c ; type = T_CHAR ; }
    Value (unsigned char c) { integer = c ; type = T_BYTE ; }
    Value (INTEGER i) { integer = i ; type = T_INTEGER ; }
    Value (UINTEGER i) { integer = i ; type = T_INTEGER ; }
    Value (string s) { integer = 0 ; str = new string (s) ; type = T_STRING ; incRef(str,string) ; }
    Value (string *s) { integer = 0 ; str = s ; type = T_STRING ; incRef(str,string) ; }
    Value (bool b) { integer = b == true ; type = T_BOOL ; }
    Value (vector *v) { integer = 0 ; vec = v ; type = T_VECTOR ; incRef(v,vector) ; }
    Value (bytevector *v) { integer = 0 ; bytevec = v ; type = T_BYTEVECTOR ; incRef(v,bytevector) ; }
    Value (Function *f) { integer = 0 ; func = f ; type = T_FUNCTION ; incBlockRef () ; }
    Value (Thread *t) { integer = 0 ; thread = t ; type = T_THREAD ; incBlockRef () ; }
    Value (map *mp) ;
    Value (Stream *s) { integer = 0 ; stream = s ; type = T_STREAM ; incRef(s, stream) ;}
    Value (Object *o) ;						// defined below struct Object
    Value (Class *c) { integer = 0 ; cls = c ; type = T_CLASS ; incBlockRef () ; }
    Value (Interface *i) { integer = 0 ; iface = i ; type = T_INTERFACE ; incBlockRef () ; }
    Value (MonitorBlock *mon) { integer = 0 ; block = (Block *)mon ; type = T_MONITOR ; incBlockRef () ; }
    Value (Package *p) { integer = 0 ; package = p ; type = T_PACKAGE ; incBlockRef () ; }
    Value (Enum *e) { integer = 0 ; en = e; type = T_ENUM ; }
    Value (EnumConst *c) { integer = 0 ; ec = c ; type = T_ENUMCONST ; }
    Value (double d) { real = d ; type = T_REAL ; }
    Value (const char *s) {integer = 0 ; str = new string (s) ; type = T_STRING ; incRef (str, string) ; }
    Value (Value *a) { integer = 0 ; addr = a ; type = T_ADDRESS ; }
    Value (const Value *a) { integer = 0 ; caddr = a ; type = T_ADDRESS ; }
    Value (RawMemory *mm) {integer = 0 ; type = T_MEMORY ; mem = mm ; incRef (mem, memory) ; }
    Value (RawPointer *p) { integer = 0 ; type = T_POINTER ; pointer = p ; incRef (pointer, pointer) ; }
    Value (Closure *c) ;    // defined in value.cc
    Value (void *p) { type = T_INTEGER ; integer = reinterpret_cast<INTEGER>(p) ; }
    ~Value() { 
        destruct() ;
     }
     
    void destruct() ;			// defined below struct Object    
    void incBlockRef() ;                // defined in value.cc

    union {
        string *str ;
        INTEGER integer ;			// integer type
        vector *vec ;				// vector type
        bytevector *bytevec ;			// byte vector type
        map *m ;				// map type
        Function *func ;
        Thread *thread ;
        Stream *stream ;			// stream type
        Class *cls ;
        Interface *iface ;
        Package *package ;
        Block *block ;				// synonym for any block type
        Enum *en ;
        EnumConst *ec ;
        ForeignContext *foreign ;
        void *ptr ;				// pointer to anything
        Node *node ;
        double real ;
        Value *addr ;
        const Value *caddr ;
        Object *object ;
        int fourbyte ;
        RawMemory *mem ;
        RawPointer *pointer ;
        Closure *closure ;
    } ;
    Type type ;
    
    int size() ;				// size of the value

    operator bool() const ;		// conversion to boolean
    Value &operator= (const Value &v) ;

#ifndef _32BIT
    Value &operator= (int i) { destruct() ; integer = i ; type = T_INTEGER ; return *this ;}
    Value &operator= (unsigned int i) { destruct() ; integer = i ; type = T_INTEGER ; return *this ;}
#endif
    Value &operator= (unsigned long i) { destruct() ; integer = i ; type = T_INTEGER ; return *this ;}
    Value &operator= (long i) { destruct() ; integer = i ; type = T_INTEGER ; return *this ;}
    Value &operator= (char c) { destruct() ; integer = c ; type = T_CHAR ; return *this ;}
    Value &operator= (unsigned char c) { destruct() ; integer = c ; type = T_BYTE ; return *this ;}
    Value &operator= (INTEGER i) { destruct() ; integer = i ; type = T_INTEGER ; return *this ;}
    Value &operator= (UINTEGER i) { destruct() ; integer = i ; type = T_INTEGER ; return *this ;}
    Value &operator= (string s) { destruct() ; str = new string (s) ; type = T_STRING ; incRef(str,string) ; return *this ;}
    Value &operator= (string *s) { destruct() ; integer = 0 ; str = s ; type = T_STRING ; incRef(str,string) ; return *this ;}
    Value &operator= (bool b) { destruct() ; integer = b == true ; type = T_BOOL ; return *this ;}
    Value &operator= (vector *v) { destruct() ; vec = v ; type = T_VECTOR ; incRef(v,vector) ; return *this ;}
    Value &operator= (bytevector *v) { destruct() ; bytevec = v ; type = T_BYTEVECTOR ; incRef(v,bytevector) ; return *this ;}
    Value &operator= (Function *f) { destruct() ; func = f ; type = T_FUNCTION ; return *this ;}
    Value &operator= (Thread *t) { destruct() ; thread = t ; type = T_THREAD ; return *this ;}

    Value &operator= (map *mp) ;
    Value &operator= (Stream *s) { destruct() ; stream = s ; type = T_STREAM ; incRef(s, stream) ;return *this ;}
    Value &operator= (Object *o) ;						// defined below struct Object
    Value &operator= (Class *c) { destruct() ; cls = c ; type = T_CLASS ; return *this ;}
    Value &operator= (Interface *c) { destruct() ; iface = c ; type = T_INTERFACE ; return *this ;}
    Value &operator= (MonitorBlock *mon) { destruct() ; block = (Block *)mon ; type = T_MONITOR ; return *this ;}
    Value &operator= (Package *p) { destruct() ; package = p ; type = T_PACKAGE ; return *this ;}
    Value &operator= (Closure *c) ; // defined in value.cc
    Value &operator= (Enum *e) { destruct() ; en = e; type = T_ENUM ; return *this ;}
    Value &operator= (EnumConst *c) { destruct() ; ec = c ; type = T_ENUMCONST ; return *this ;}
    Value &operator= (double d) { destruct() ; real = d ; type = T_REAL ; return *this ;}
    Value &operator= (const char *s) {destruct() ; str = new string (s) ; type = T_STRING ; incRef (str, string) ; return *this ;}
    Value &operator= (Value *a) { destruct() ; addr = a ; type = T_ADDRESS ; return *this ;}

    // copy constructor
    Value (const Value &v) ;
    Value operator< (const Value &v) ;		// needed for map
    Value operator== (const Value &v) ;		// needed for vector intersection etc.

#if 0
    Value operator== (Value &v) ;
    Value operator!= (Value &v) ;
    Value operator> (Value &v) ;
    Value operator<= (Value &v) ;
    Value operator>= (Value &v) ;
    Value operator* (Value &v) ;
    Value operator/ (Value &v) ;
    Value operator% (Value &v) ;
    Value operator+ (Value &v) ;
    Value operator- (Value &v) ;
    Value operator>> (Value &v) ;
    Value operator<< (Value &v) ;
    Value operator& (Value &v) ;
    Value operator| (Value &v) ;
    Value operator^ (Value &v) ;
    Value operator- () ;			// unary minus
    Value operator! () ;			// unary not
    Value operator~ () ;			// unary onescomp
#endif
    Value operator[] (int i) const ;
    Value operator[] (const Value &v) const ;

    bool checkInt() { return type == T_INTEGER || type == T_CHAR || type == T_BYTE || type == T_BOOL; }
    bool checkReal() { return type == T_REAL ; }
    bool checkEnum() { return type == T_ENUMCONST ; }

    string toString() const ;
    
} ;

// a closure is a pointer to a block and its associated execution environment.  An
// execution environment is simply the complete set of static links that it
// requires to execute.  The stackframe objects are reference counted.

struct Closure : public GCObject {
    Closure (Type t, Block *blk, StaticLink *sl) ;
    ~Closure() ;                // deleting a closure must traverse the static links
    Type type ;
    Block *block ;
    StaticLink *slink ;
    void *vm_state ;        // vm state
    Value value ;           // value yielded
} ;

// less functor for maps
struct less {
    bool operator () (const Value &v1, const Value &v2) const {
        return Value (v1) < Value (v2) ;
    }
} ;

// a map of value vs value
struct map : public GCObject  {
    map() {ref = 0 ; init() ;}
        void init() {
#ifdef GCDEBUG
             registerObject (T_MAP, this) ;
#endif
        }
        ~map() {
#ifdef GCDEBUG
             unregisterObject ( this) ;
#endif
        }
    typedef std::map<Value, Value, less>ValueMap ;
    typedef ValueMap::iterator iterator ;
    typedef ValueMap::value_type value_type ;
    int size() { return m.size() ; }
    iterator begin() { return m.begin() ; }
    iterator end() { return m.end() ; }

    Value &operator[] (const Value &k) { return m[k] ; }
    bool insert (Value key, Value v) { const ValueMap::value_type t (key, v) ; return (m.insert (t)).second ; }
    void insert (iterator s, iterator e) { m.insert (s, e) ; }
    iterator find (const Value &key) { return m.find (key) ; }
    void erase (iterator s) { m.erase (s) ; }
    ValueMap m ;
} ;


inline Value::Value (map *mp) { integer = 0 ; m = mp ; type = T_MAP, incRef(m,map) ; }
inline Value &Value::operator= (map *mp) { destruct() ; m = mp ; type = T_MAP, incRef(m,map) ; return *this ;}


typedef Value::ValueVec ValueVec ;
typedef Value::ByteVec ByteVec ;


//
//  vector operations
//

bool operator== (Value::vector &v1, Value::vector &v2) ;
bool operator!= (Value::vector &v1, Value::vector &v2) ;
bool operator< (Value::vector &v1, Value::vector &v2) ;
Value::vector operator& (Value::vector &v1, Value::vector &v2) ;
Value::vector operator| (Value::vector &v1, Value::vector &v2) ;
Value::vector operator- (Value::vector &v1, Value::vector &v2) ;
bool operator> (Value::vector &v1, Value::vector &v2) ;
bool operator<= (Value::vector &v1, Value::vector &v2) ;
bool operator>= (Value::vector &v1, Value::vector &v2) ;
Value::vector operator+ (Value::vector &v1, Value::vector &v2) ;
Value::vector operator>> (Value::vector &v1, Value::vector &v2) ;
Value::vector operator<< (Value::vector &v1, Value::vector &v2) ;

//
//  bytevector operations
//

bool operator== (Value::bytevector &v1, Value::bytevector &v2) ;
bool operator!= (Value::bytevector &v1, Value::bytevector &v2) ;
bool operator< (Value::bytevector &v1, Value::bytevector &v2) ;
Value::bytevector operator& (Value::bytevector &v1, Value::bytevector &v2) ;
Value::bytevector operator| (Value::bytevector &v1, Value::bytevector &v2) ;
Value::bytevector operator- (Value::bytevector &v1, Value::bytevector &v2) ;
bool operator> (Value::bytevector &v1, Value::bytevector &v2) ;
bool operator<= (Value::bytevector &v1, Value::bytevector &v2) ;
bool operator>= (Value::bytevector &v1, Value::bytevector &v2) ;
Value::bytevector operator+ (Value::bytevector &v1, Value::bytevector &v2) ;
Value::bytevector operator>> (Value::bytevector &v1, Value::bytevector &v2) ;
Value::bytevector operator<< (Value::bytevector &v1, Value::bytevector &v2) ;

//
//  map operations
//

bool operator== (map &v1, map &v2) ;
bool operator!= (map &v1, map &v2) ;
bool operator< (map &v1, map &v2) ;
bool operator> (map &v1, map &v2) ;
bool operator<= (map &v1, map &v2) ;
bool operator>= (map &v1, map &v2) ;
map operator+ (map &v1, map &v2) ;
map operator>> (map &v1, map &v2) ;
map operator<< (map &v1, map &v2) ;


//
// a stack frame.  This holds all the data for an instantiation of a block.  It can
// be extended as new variables are added to the block at runtime
//
// The StackFrame object (or any object derived from it) is followed immediately
// in memory by the values it contains
//

struct StackFrame : public GCObject {
    StackFrame (StaticLink *sl, StackFrame *dl, Instruction *i, Block *b, int sz, bool doinit = true): block (b), size (sz), slink(sl), dlink (dl), inst (i) { 
        if (doinit) {
            init (sizeof (StackFrame)) ;
        }
        capacity = 0 ;			// note that we have not separately allocated the value array
    }
    
    void init (int objsize) {
        char *addr = (char*)this + objsize ;
        varstack = (Value*)addr ;
        memset ((void*)addr, 0, sizeof (Value) * size) ;
    }

    virtual ~StackFrame() { 
        if (capacity > 0) {
            delete [] varstack ; 
        } else {
            if (size > 0) {
                for (int i = 0 ; i < size ; i++) {
                    varstack[i].~Value() ;		// explicit destructor call
                }
            }
        }
    }

   void *operator new (size_t s, int nelems) {
       return malloc (sizeof (StackFrame) + nelems * sizeof (Value)) ;
   }

   void operator delete (void *p) {
        free (p) ;
   }

    virtual void checkCapacity() {
        size++ ;			// another variable added
        if (size >= (capacity - 1)) {
            bool firsttime = false ;
            if (capacity == 0) {
                capacity = size ;
                firsttime = true ;
            }
//            std::cout << "stack has reached capacity, expanding\n" ;
            int newcap = capacity * 2 ;
            Value *s = new Value [newcap] ;
            for (int i = 0 ; i < size-1 ; i++) {      // size is one greater than the number of elements in the array
                s[i] = varstack[i] ;
            }
            if (!firsttime) {
                delete [] varstack ;
            }
            varstack = s ;
            capacity = newcap ;
        }
    }

    Value *varstack ;		// members of the frame
    Block *block ;              // current block
    StackFrame *dlink ;		// dynamic link
    StaticLink *slink ;         // static link
    int size ;			// size of this frame
    int capacity ;
    Instruction *inst ;		// current instruction
} ;

// a separate chain of static links for each thread is maintained

struct StaticLink {
    StaticLink (StaticLink *n = NULL, StackFrame *f = NULL) : next(n), frame(f) {}
    StaticLink *next ;          // next static link in chain
    StackFrame *frame ;         // frame at this level
} ;

// variable attributes
const int VAR_CONST = 1 ;		// constant
const int VAR_PREDEFINED = 2 ;		// variable is a predefined block
const int VAR_PRIVATE = 4 ;		// private access
const int VAR_PROTECTED = 8 ;		// protected access
const int VAR_PUBLIC = 16 ;		// public access
const int VAR_STATIC = 32 ;		// variable is static
const int VAR_TYPESET = 64 ;		// type of the variable has been set
const int VAR_GENERIC = 128 ;		// variable is generic
const int VAR_INVENTED = 256 ;		// variable has been inventted
const int VAR_OLDVALUE = 512 ;		// variable has an old value (has been overridden)
const int VAR_NEEDINIT = 1024 ;		// need to init var to value none
const int VAR_TYPEFIXED = 2048 ;	// variable has fixed type

const int VAR_ACCESSMASK = (VAR_PRIVATE | VAR_PROTECTED | VAR_PUBLIC) ;
const int VAR_ACCESSFULL = VAR_ACCESSMASK ;

// a variable - holds information on a named thing in the program.  The value
// of the variable is actually held in the stack frame.


struct Variable {
    Variable() {}
    Variable (string n, int off) : name (n), offset (off), flags (0), type(T_NONE) {
        //std::cout << "var " << name << " at offset " << off << '\n' ;
    }
    virtual ~Variable() ;
    virtual void setValue (const Value &v, StackFrame *base) { 
//        std::cout << "setting value of " << name << " to " << v << '\n' ;
        base->varstack[offset] = v ; 
        flags |= VAR_TYPESET ;
        //type = v.type ;
    }
    virtual void setValue (Object *obj, StackFrame *base) { 
//        std::cout << "setting value of " << name << " to " << v << '\n' ;
        base->varstack[offset] = obj ; 
        flags |= VAR_TYPESET ;
    }
    virtual Value &getValue(StackFrame *base) { return base->varstack[offset] ; }
    virtual Value *getAddress(StackFrame *base) { return &base->varstack[offset] ; }

    void destruct(StackFrame *base) {
	base->varstack[offset].destruct() ;
    }

    void incOffset (int off) { 
        offset += off ; 
        //std::cout << "incrementing offset of " << name << " to " << offset << '\n' ;
    }
    
    virtual void commit() {}

    void checkAccess (Aikido *p, Block *block, SourceLocation *source, int required) ;
    void setAccess (Token access) ;
    string name ;
    int flags ;
    int offset ;			// offset from base to value
    Type type ;				// for fixed type variables
    Value oldValue ;			// old value for overridden variables
    AnnotationList annotations;
    virtual bool isEnumConst() { return false ; }
    virtual void dump (Aikido *aikido, std::ostream &os) ;
    virtual void load (Aikido *aikido, std::istream &is) ;

    // find an annotation if it exists
    Annotation *getAnnotation (string name);
    bool allowsOverride();      // does this block allow an override

    // actual subtypes defined for variable (var x:foo<int,string> - subtypes are int,string)
    std::vector<Node*> subtypes;
} ;

//                           
// this is used to hold the value of a block (class, function, thread or package) during
// the parse phase.  The reason for this is that the parser needs to get the scope
// for use during inheritance.  The name of the variable is the same as the real
// variable, but prefixed by a dot.
//

struct Tag : public Variable {
    Tag() {}
    Tag (string name, Block *b, Variable *v) : Variable (name, -1), block (b), var(v) {}
    Block *block ;
    Variable *var ;
    virtual void dump (Aikido *aikido, std::ostream &os) ;
    virtual void load (Aikido *aikido, std::istream &is) ;
} ;

// a parameter to a block.  Just a variable the can be defaulted and have a specified
// type

struct Parameter : public Variable {
    Parameter() {}
    Parameter (string name, int off) : Variable (name, off), def (NULL), type (NULL) {}
    ~Parameter() ;
    void setDefault (Node *d) { def = d ; }
    void setType (Node *t) { type = t ; }
    Node *def ;
    Node *type ;
    virtual bool isReference() { return false ; }
    virtual void dump (Aikido *aikido, std::ostream &os) ;
    virtual void load (Aikido *aikido, std::istream &is) ;
}  ;

//
// a reference parameter is one that is assigned to point to another
// variable.
//

struct Reference : public Parameter {
    Reference() {}
    Reference (string name, int off) : Parameter (name, off) {}
    void setValue (const Value &val, StackFrame *base) {
        Value *ref = static_cast<Value*>(base->varstack[offset].ptr) ;
        *ref = val ;
    }
    Value &getValue(StackFrame *base) { 
        Value *ref = static_cast<Value*>(base->varstack[offset].ptr) ;
        return *ref ;
    }
    Value *getAddress(StackFrame *base) { 
        Value *ref = static_cast<Value*>(base->varstack[offset].ptr) ;
        return ref ;
    }
    void setReference (Value *v, StackFrame *base) {
        base->varstack[offset].ptr = static_cast<void*>(v);
    } 
    virtual bool isReference() { return true ; }
    virtual void dump (Aikido *aikido, std::ostream &os) ;
    virtual void load (Aikido *aikido, std::istream &is) ;
} ;

#if 0
struct FixedVariable : public Variable {
    FixedVariable (string name, int off, int foff) : Variable (name, off), fixedoff (foff) {}
    int fixedoff ;
} ;

struct FixedVariable1 : public FixedVariable {
    FixedVariable1(string name, int off, int foff) : FixedVarible (name, off, foff) {
    }

    void setValue (const Value &val, StackFrame *base) {
        *(char *)&base->memory[fixedoff] = (char)val.integer ;
    }

    Value &getValue(StackFrame *base) { 
        Value &val = base->varstack[offset] ;
        val.integer = (INTEGER)(*(char *)&base->memory[fixedoff]) ;
        val.type = type ;
        return val ;
    }

    virtual void commit() {
        Value &val = base->varstack[offset] ;
        *(char *)&base->memory[fixedoff] = (char)val.integer ;
    }
} ;


struct FixedVariable2 : public FixedVariable {
    FixedVariable2(string name, int off, int foff) : FixedVarible (name, off, foff) {
    }

    void setValue (const Value &val, StackFrame *base) {
        *(short *)&base->memory[fixedoff] = (short)val.integer ;
    }

    Value &getValue(StackFrame *base) { 
        Value &val = base->varstack[offset] ;
        val.integer = (INTEGER)(*(short *)&base->memory[fixedoff]) ;
        val.type = type ;
        return val ;
    }

    virtual void commit() {
        Value &val = base->varstack[offset] ;
        *(short *)&base->memory[fixedoff] = (short)val.integer ;
    }
} ;

struct FixedVariable4 : public FixedVariable {
    FixedVariable4(string name, int off, int foff) : FixedVarible (name, off, foff) {
    }

    void setValue (const Value &val, StackFrame *base) {
        *(int *)&base->memory[fixedoff] = (int)val.integer ;
    }

    Value &getValue(StackFrame *base) { 
        Value &val = base->varstack[offset] ;
        val.integer = (INTEGER)(*(int *)&base->memory[fixedoff]) ;
        val.type = type ;
        return val ;
    }

    virtual void commit() {
        Value &val = base->varstack[offset] ;
        *(int *)&base->memory[fixedoff] = (int)val.integer ;
    }
} ;


struct FixedVariable8 : public FixedVariable {
    FixedVariable8(string name, int off, int foff) : FixedVarible (name, off, foff) {
    }

    void setValue (const Value &val, StackFrame *base) {
        *(long long *)&base->memory[fixedoff] = (long long)val.integer ;
    }

    Value &getValue(StackFrame *base) { 
        Value &val = base->varstack[offset] ;
        val.integer = (INTEGER)(*(long long *)&base->memory[fixedoff]) ;
        val.type = type ;
        return val ;
    }

    virtual void commit() {
        Value &val = base->varstack[offset] ;
        *(long long *)&base->memory[fixedoff] = (long long)val.integer ;
    }
} ;

#endif


//
// runtime variable lookup:
//
// Each variable name in the program resides in a set of blocks (there may
// be the same name in 2 blocks).  When we need to find the member
// of a block at runtime we are told what block it is in.  The simplest
// way of finding it is to search the block's symbol table for the name
// but that would be slow as it involves string comparisons.  So, if
// we build a mapping of a variable name to a set of block/variable pointers
// we can search on the block pointer at runtime to obtain the variable
// pointer.  If this list is sorted we can do it using a binary search.


struct VariableName {
    typedef std::map<Scope*, Variable*> LocationMap ;
    VariableName() : lastLookupScope(NULL), lastvar(NULL) {}
    VariableName (const string &n) : name (n), lastLookupScope(NULL), lastvar(NULL) {}
    const string name ;
    LocationMap locations ;
    Variable *lookup (Scope *s, SourceLocation *source, int access) ;
    void insert (Scope *s, Variable *v) ;
    Scope *lastLookupScope ;
    Variable *lastvar ;
} ;


#if 0
// this is used to cache a member of a block to avoid a search using the . operator

const int MEMCACHE_SIZE = 4 ;

struct MemberCache {
    MemberCache(string name) {
        for (int i = 0 ; i < MEMCACHE_SIZE ; i++) {
            entries[i].block = NULL ;
        }
        memberName = name ;
    }
    struct Entry {
        Block *block ;
        Variable *member ;
    } ;

    void add (Block *b, Variable *mem) {
        //std::cout << "trying to add " << (void*)b << ':' << mem->name.str << " to cache " << (void*)this << '\n' ;
        for (int i = 0 ; i < MEMCACHE_SIZE ; i++) {
            if (entries[i].block == NULL) {
                entries[i].block = b ;
                entries[i].member = mem ;
                //std::cout << "added\n" ;
                return ;
            }
        }
        //std::cout << "cache full\n" ;
    }

    Variable *find (Block *b) {
        //std::cout << "looking for block " << (void*)b << " in cache " << (void*)this << '\n' ;
        for (int i = 0 ; i < MEMCACHE_SIZE ; i++) {
            if (entries[i].block == b) {
                //std::cout << "found\n" ;
                return entries[i].member ;
            }
        }
        //std::cout << "not found\n" ;
        return NULL ;
    }

    Entry entries[MEMCACHE_SIZE] ;
    string memberName ;
    
} ;

#endif

// a scope.  Contains a set of variables and macros

class Scope {
public:
    Scope(Aikido *b):p(b) {}
    Scope (Aikido *b, int l, Scope *parent, Block *major) ;	// in parser.cc
    virtual ~Scope() ;
    virtual Variable *addVariable (string name) ;
    Parameter *addParameter (string name) ;
    //void addParameter (Parameter *p) ;
    Reference *addReference (string name) ;
    void insertVariable (Variable *var) ;
    virtual Variable *findVariable (const string &name, Scope *&scope, int access, SourceLocation *source, Package **p) ;
    virtual Variable *findPackageVariable (const string &name, int &n) ;
    Variable *findLocalVariable (const string &name) ;
    virtual Package *findPackage (const string &name, int &n) ; 
    void insertMacro (Macro *macro) ;
    virtual Macro *findMacro (const string &name, Package *&pkg) ;
    virtual Macro *findPackageMacro (const string &name, int &n, Package *&pkg); 
    typedef std::map <string, Variable *, std::less<string> > VarMap ;
    VarMap variables ;
    std::vector<Variable*> varlist ;		// list of variables reloaded

    typedef std::map<string, Macro *, std::less<string> > MacroMap ;
    MacroMap macros ;

    Variable *findVarAtIndex (int index) ;

    // all the packages defined in this scope
    typedef std::map<string, Package*> PackageMap ;
    PackageMap packages ;

    std::list<Package*> siblings ;			// packages added using using

    Aikido *p ;
    int level ;
    Scope *parentScope ;
    Block *majorScope ;
    typedef std::list<Scope*> Children ;
    Children children ;		// children scopes
    void addChild (Scope *s) { children.push_back (s) ; }
    int index ;		// index of this scope in scopes vector
    int nparas ;

    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
} ;

struct ExceptionFrame {
    ExceptionFrame() : source(NULL), block(NULL) {}
    ExceptionFrame (SourceLocation *src, Block *blk): source (src), block(blk) {}
    SourceLocation *source ;
    Block *block ;
} ;

// an exception
struct Exception {
    Exception (Value v) : value (v), stack (NULL), staticLink (NULL), scope(NULL), scopeLevel (0) {}
    Exception (Value v, StackFrame *s, StaticLink *sl, Scope *sc, int l, SourceLocation *src= NULL, Block *blk=NULL) : value (v), stack (s), staticLink (sl), scope (sc), scopeLevel (l) { fillStackTrace(src, blk) ; }
    Value value ;
    
    StackFrame *stack ;
    StaticLink *staticLink ;
    Scope *scope ;
    int scopeLevel ;
    std::vector<ExceptionFrame> stacktrace ;
    void fillStackTrace(SourceLocation *src, Block *blk) ;
} ;


const int NODE_TYPEFIXED = 1 ;	// node only has fixed types
const int NODE_PASSBYNAME = 2 ;	// call uses pass-by-name semantics
const int NODE_REPLACEVARS = 4; // replace $ vars in string

// the Node class is a parse tree node that is created by the parser.

struct Node {
    Node(Aikido *d, Token t, Node *l, Node *r) : op (t), left(l), right(r), var (NULL) {init (d) ;}
    Node (Aikido *d, Token t, const Value &v) : op(t), left(NULL), right(NULL), var(NULL), value (v) {init (d) ;}
    Node (Aikido *d, Token t, Variable *v) : op(t), left(NULL), right(NULL), var(v) {init (d) ;}
    Node (Aikido *d, Package *p) : op(IMPLICITPACKAGE), left(NULL), right(NULL), pkg(p) {init (d) ;}
    Node (Aikido *d, Token t) : op(t), left(NULL), right(NULL), var(NULL) {init (d) ;}
    Node (Aikido *d, Token t, Node *l) : op(t), left(l), right(NULL), var(NULL) {init (d) ;}
    Node (Aikido *d, Token t, std::vector<Node *> &v): var(NULL), op(t), vec(v), left(NULL), right(NULL) {init (d) ;}
    Node (Aikido *d, std::iostream *str, string *f, int l) : stream(str), op(FOREIGN), left(NULL), 
                right(NULL) { value.type = T_INTEGER ; value.foreign = new ForeignContext (*f, l) ; init(d) ;}
    Node (Node *) ;

    ~Node() ;

    Token op ;
    Node *left, *right ;
    Scope *scope ;				// for scope based ops
    int flags ;
    union {
        Variable *var ;				// op == IDENTIFIER
        std::iostream *stream ;
        Package *pkg ;				// for package scoping
    } ;
    Value value ;                               // op == NUMBER, STRING, FOREIGN
    std::vector<Node*> vec ;			// op == VECTOR literal, function params and SWITCH case list

    Type type ;			// for fixed type nodes
    //Value evaluate(Aikido *p, StackFrame *stack, StackFrame *staticLink) ;
    Node *sequence (Node *n, bool end = false) ;
    void print (Aikido *p, std::ostream &os, int indent) ;

    bool isVar() { return op == IDENTIFIER ; }
    void assign (Value *vstack, Aikido *p, const Value &v, StackFrame *stack, StaticLink *staticLink, Scope *currentScope, int currentScopeLevel) ;
    SourceLocation *source ;
    int pad ;			// just for RTC to shut it up
    void init (Aikido *d) ;
    void propagateFlags() ;
} ;


// a block is a named scope (also known as a major scope)

const int BLOCK_STATIC = 1 ;			// block is static
const int BLOCK_VARARGS = 2 ;			// block has variable args
const int BLOCK_HASOPERATORS = 4 ;		// block has operator overloads
const int BLOCK_SYNCHRONIZED = 8 ;		// block is synchronized
const int BLOCK_BUILTINMETHOD = 16 ;            // block is a builtin method
const int BLOCK_ISABSTRACT = 32 ;            // block is abstract

struct Block : public Scope, public GCObject {
    Block(Aikido *b):Scope(b),body(NULL) {}
    Block (Aikido *b, string n, int l, Scope *parent, Type t) : Scope (b, l, parent, this), name (n), stacksize(0), 
           flags (0), parentClass(NULL), parentBlock (NULL), superblock (NULL), body (NULL), type (t),
           varargs(NULL), finalize(NULL), code(NULL) {
#ifdef GCDEBUG
        registerObject (type, this) ;
#endif
        makefullname() ;
    }
    ~Block() ;

    Variable *findVariable (const string &name, Scope *&scope, int access, SourceLocation *source, Package **p) ;
    Macro *findMacro (const string &name, Package *&pkg) ;
    bool isSuperBlock (Block *blk) ;		// is blk a superblock of this one?
    Variable *checkOverride (Variable *var) ;		// is variable going to override one of ours?
    bool isSubclassOrImplements (Block *b) ;		// is this class a subclass of b or implements b
    bool implementsInterface (Block *iface) ;
    bool isAbstract() ;					// is the block abstract (contains predefined blocks)
    void printUndefinedMembers (std::ostream &os) ;
    string name ;
    string fullname ;
    virtual void call(Value *dest, VirtualMachine *vm, StackFrame *stack, StaticLink *staticLink, Scope *currentScope, int currentScopeLevel, const ValueVec &parameters) {}
    virtual bool isNative() = 0 ;
    void moveMembers (int delta) ;
    int stacksize ;
    int flags ;
    int stackFrameSize() {
        return sizeof (StackFrame) + stacksize * sizeof (Value) ;
    }
    Class *parentClass ;		// member class
    Block *parentBlock ;
    Tag *superblock ;
    void setParentBlock (Block *pa) { parentBlock = pa ; makefullname() ; }


    std::list<Tag *> children ;			// blocks derived from this one
    std::list<Tag*> interfaces ;		// interfaces I implement
#if defined(_OS_WINDOWS)
    std::set<__int64> imports ;
#else
    std::set<long long> imports ;
#endif

    Node *body ;
    codegen::CodeSequence *code ;			// generated code
    Type type ;
    Variable *varargs ;
    InterpretedBlock *finalize ;		// non-null means finalize method
    AnnotationList annotations;
    void makefullname() ;


    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
} ;


struct Function : public Block {
    Function(Aikido *b):Block(b) {}
    Function (Aikido *b, string n, int l, Scope *parent,Type t) : Block (b, n, l, parent, t), returnType(NULL) {}
    Node *returnType ;
    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
    virtual bool isRaw() { return false ; }
} ;

struct InterpretedBlock : public Function {
    InterpretedBlock(Aikido*b):Function(b) {} 
    InterpretedBlock (Aikido *b, string nm, int l, Scope *parent, Type t = T_FUNCTION) : Function (b, nm, l, parent, t) {}
    std::vector<Parameter *> parameters ;
    std::vector<Parameter *> types ;            // parameterized types
    bool isNative() { return false ; }

    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
#ifdef JAVA
    // support for Java
    void *javaClass ;
    void setJavaClass (void *cls) { javaClass = cls ; }
    void *getJavaClass() { return javaClass ; }
#endif
} ;


struct NativeFunction : public Function {
    NativeFunction(Aikido *b) : Function(b){}
    typedef Value (*FuncPtr)(Aikido *b, VirtualMachine *vm, const ValueVec &, StackFrame *stack, StaticLink *staticLink, Scope *currentScope, int currentScopeLevel) ;
    NativeFunction (Aikido *b, string nm, int np, FuncPtr p, int l, Scope *parent) : Function (b, nm, l, parent, T_FUNCTION), ptr (p), paramask(0) {nparas = np ;}
    FuncPtr ptr ;
    virtual void call(Value *dest, VirtualMachine *vm, StackFrame *stack, StaticLink *staticLink, Scope *currentScope, int currentScopeLevel, const ValueVec &parameters) { 
        //std::cout << "calling native function " << name.str << "\n" ;
        *dest = (*ptr)(p, vm, parameters, stack, staticLink, currentScope, currentScopeLevel) ;  
    }
    bool isNative() { return true ; }
    int paramask ;		// 1 bit per para - value 1 means reference 
    virtual bool isRaw() { return false ; }
    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
} ;

struct RawNativeFunction : public NativeFunction {
    RawNativeFunction(Aikido *b):NativeFunction(b) {}
    RawNativeFunction (Aikido *b, string nm, int np, FuncPtr p, int l, Scope *parent) : NativeFunction (b, nm, np, p, l, parent) {}
    virtual void call(Value *dest, VirtualMachine *vm, StackFrame *stack, StaticLink *staticLink, Scope *currentScope, int currentScopeLevel, const ValueVec &parameters) ;
    virtual bool isRaw() { return true ; }
    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
} ;

struct Thread : public InterpretedBlock {
    Thread(Aikido *b):InterpretedBlock(b) {}
    Thread (Aikido *b, string n, int l, Scope *parent) : InterpretedBlock (b, n, l, parent, T_THREAD) {}
    Aikido *aikido ;
    Variable *input ;
    Variable *output ;
    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
} ;


struct Class : public InterpretedBlock {
    Class(Aikido *b) :InterpretedBlock(b){}
    Class (Aikido *b, string n, int l, Scope *parent, Type t = T_CLASS) : InterpretedBlock (b, n, l, parent, t) {}
    typedef std::map<Token, InterpretedBlock*, std::less<Token> > OperatorMap ;
    OperatorMap operators ;

    void addOperator (Token tok, InterpretedBlock *func) {
        OperatorMap::value_type t (tok, func) ;
        operators.insert (t) ;
    }

    InterpretedBlock *findOperator (Token tok) ;
    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;

} ;

struct MonitorBlock : public Class {
    MonitorBlock(Aikido *b):Class (b) {}
    MonitorBlock (Aikido *b, string n, int l, Scope *parent) : Class (b, n, l, parent, T_MONITOR) {}
    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
} ;

struct Package : public Class {
    Package(Aikido*b) :Class(b){}
    Package (Aikido *b, string n, int l, Scope *parent) : Class (b,n, l, parent, T_PACKAGE), object (NULL) {}
    Object *object ;
    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
} ;

struct Interface : public Class {
    Interface(Aikido*b):Class(b) {}
    Interface (Aikido *b, string n, int l, Scope *parent) : Class (b, n, l, parent, T_INTERFACE) {}
    void checkBlock (Block *b) ;		// check that a block implements all members

    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
} ;

// an instance of an annotation.  An annotation refers to an interface (which is defined as an annotation with its name starting with @)
// it also contains a map of function name within the interface versus the value for the function
struct Annotation {
    Annotation (Aikido*b, Interface *i): p(b), iface(i){}
   
    void addActual (std::string name, Node *value) {
        actuals[name] = value;
    }

    Aikido *p;
    Interface *iface; 
    typedef std::map<std::string, Node*> ActualMap;
    ActualMap actuals;
};

// an object is an instance of a class

struct Object : public StackFrame { 
   Object (Block *b, StaticLink *sl, StackFrame *dl, Instruction *i, bool doinit = true) : StackFrame (&objslink, dl, i, b, b->stacksize, false) { 
      //std::cout << "new object at " << this << '\n' ;
      if (sl != NULL) {
          objslink = *sl ;
      }
      incRef (b, block) ;			// an instance of a block is a reference to it
      if (doinit) {
          init (sizeof (Object)) ;
      }
#ifdef GCDEBUG
        registerObject (T_OBJECT, this) ;
#endif
   }
   ~Object() {
        varstack[0].object = NULL ;     // prevent recursive deletion
#ifdef GCDEBUG
        unregisterObject (this) ;
#endif
        
    }

   virtual OSThread_t getOwner() {
	   return 0;
   }

   virtual bool isMonitor() { return false;}

   void *operator new (size_t s, int nelems) {
       return malloc (sizeof (Object) + nelems * sizeof (Value)) ;
   }

   void operator delete (void *p) {
        free (p) ;
   }

   void destruct() ;
   virtual void AIKIDO_EXPORT acquire(bool force = false) ;
   virtual void AIKIDO_EXPORT release(bool force = false) ;

   StaticLink objslink ;
} ;

// an instance of a monitor
struct Monitor : public Object {
   Monitor (Block *b, StaticLink *sl, StackFrame *dl, Instruction *i) ;
   ~Monitor() ;

   void *operator new (size_t s, int nelems) {
       return malloc (sizeof (Monitor) + nelems * sizeof (Value)) ;
   }

   void operator delete (void *p) {
        free (p) ;
   }

   virtual bool isMonitor() { return true;}

   OSThread_t getOwner() {
	   return owner;
   }


private:
   OSMutex mutex ;
   OSCondvar event ;
   OSCondvar queue ;
   OSThread_t owner ;
   int n ;

public:
   virtual void acquire(bool force = false) ;
   virtual void release(bool force = false) ;
   void wait() ;
   bool timewait(int microseconds) ;
   void notify() ;
   void notifyAll() ;
} ;


// enumerated type
struct Enum : public InterpretedBlock {
    Enum(Aikido*b):InterpretedBlock(b) {}
    Enum (Aikido *p, string n, int l, Scope *parent) : InterpretedBlock (p, n, l, parent, T_ENUM) {}
    std::vector<EnumConst*> consts ;
    virtual void dump (std::ostream &os) ;
    virtual void load (std::istream &is) ;
} ;

// enumeration constant
struct EnumConst : public Variable {
    EnumConst() {}
    EnumConst (Enum *e, string n, int off) : en (e), Variable (n, off) {
        next = prev = NULL ;
        value = 0 ;
    }
    Enum *en ;
    EnumConst *next ;
    EnumConst *prev ;
    EnumConst *nextValue (int n) ;
    EnumConst *prevValue (int n) ;
    INTEGER value ;
    virtual bool isEnumConst() { return true ; }
    virtual void dump (Aikido *aikido, std::ostream &os) ;
    virtual void load (Aikido *aikido, std::istream &is) ;
} ;


///////////////////////////////////////////////////////////////////////////////////
// IO operations
///////////////////////////////////////////////////////////////////////////////////

inline std::ostream &operator << (std::ostream &os, string &s) {
    return os << s.str ;
}

inline std::ostream &operator << (std::ostream &os, const string &s) {
    return os << s.str ;
}



std::ostream &operator << (std::ostream &os, Value &v) ;
inline std::ostream &operator << (std::ostream &os, Node *node) {
   os << node->left ;
   os << "OP: " << node->op << '\n' ;
   return os << node->right ;
}

inline std::istream &operator >> (std::istream &is, string &s) {
    //return is >> s.str ;
    return std::getline (is, s.str) ;
}

// in value.cc
void readfile (std::istream &is, Value::vector &v) ;


std::istream &operator >> (std::istream &is, Value &v) ;

// a macro
class Macro : public Scope {
public:
    Macro (Aikido *p, int l, Scope *parent, Block *major, string nm, string file, int line) ;
    void instantiate (Scope *scope) ;
    void uninstantiate() { instanceScope = NULL ; }
    void addParameter (string p) ;
    void putLine (char *l) ;
    string &getName() { return name ;}
    std::iostream &getBody() { return body ;}
    const char *getFilename() { return source->filename ; }
    int getLineNumber() { return source->lineno ;}
    virtual Variable *findVariable (const string &name, Scope *&scope, int access, SourceLocation *source, Package **p) ;
    Scope *getInstanceScope() { return instanceScope ; }
    void derive (Macro *s) { super = s ; }
    void addParaDef (string pa, string def) { paraDefs[pa] = def ; }
    bool getParaDef (string pa, string &def) ;
    std::vector<string> &getParameters() { return parameters ; }
    Macro *findMacro (const string &name, Package *&pkg) ;
    
    void disable() { disabled = true ; }
    void enable() { disabled = false ; }
    bool isDisabled() { return disabled ; }
    Macro *super ;
    string superargs ;
    Package *instancePackage ;
    
private:
    typedef std::map<string, string> ParaDefMap ;
    string name ;
    std::vector<string> parameters ;
#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream body ;
#else
    std::stringstream body ;
#endif
    SourceLocation *source ;
    bool disabled ;
    void copylower (string &to, string &from) ;
    Scope *instanceScope ;
    ParaDefMap paraDefs ;
} ;


// a context holds the current location of the parser in the input stream
class Context {
public:
#if __SUNPRO_CC == 0x500
    typedef std::map<const string , string , std::less<string> > ParaMap ;
#else
    typedef std::map<string , string , std::less<string> > ParaMap ;
#endif

    Context (std::istream &str, string &file, int line, Context *prev = NULL) ;
    Context (std::istream &str, Context *prev = NULL) ;
    Context (Macro *mac, Context *prev) ;
    Context (Node *n, Context *prev) ;
    Context (SourceLocation *s, Stream &str, Context *prev) ;
    ~Context() ;
    std::istream &getStream() { return stream ; }
//    void rewind() { stream->seekg (0) ; }
    void rewind() { stream.rdbuf()->pubseekpos (0) ; stream.clear() ; }
    string &getFilename() { return filename ; }
    int getLineNumber() { return lineno ; }
    void setFilename(string f) { filename = f ; }
    void setLineNumber(int l) { lineno = l ; }
    void incLineNumber() { lineno++ ; }
    SourceLocation *getLocation() { return location ; }

    string &getMacroName() { return macro->getName() ; }
    bool hasMacro() { return macro != NULL ; }
    Context *getPrevious() { return previous ; }

    ParaMap &getParaMap() { return paraMap ; }
    
private:
    std::istream &stream ;
    string filename ;
    Macro *macro ;
    Node *node ;
    int lineno ;
    ParaMap paraMap ;
    Context *previous ;
    SourceLocation *location ;
} ;

//
// lexical analyser token tree
//

struct TokenNode {
    TokenNode (char c, Token val, TokenNode *par) { ch = c ; value = val ; parent = par ; }
    bool add (string &s, int index, Token tok) ;
    Token find (char *&s) ;
    void print (std::ostream &os, Token tok) ;
    void print (std::ostream &os) ;
    
    
    std::vector <TokenNode*> children ;
    TokenNode *parent ;
    char ch ;
    Token value ;
} ;

inline std::ostream &operator << (std::ostream &os, TokenNode &n) {
    return os << '(' << n.ch << ',' << n.value << ')' ;
}

inline std::ostream &operator << (std::ostream &os, std::vector<TokenNode *> &v) {
    for (int i = 0 ; i < v.size() ; i++) {
        os << *v[i] << ' ' ;
    }
    return os << '\n' ;
}

#if defined(_OS_WINDOWS)
struct ImportFile {
    ImportFile (const string &n, __int64 i, bool z = false) : name(n), id(i), inzip(z) {}
    ImportFile (const ImportFile &f) : name (f.name), id (f.id), inzip (f.inzip){}
    string name ;		// name of file
    __int64 id ;		// unique identifier for file
    bool inzip ;
} ;
#else
struct ImportFile {
    ImportFile (const string &n, long long i, bool z = false) : name(n), id(i), inzip(z) {}
    string name ;		// name of file
    long long id ;		// unique identifier for file
    bool inzip ;
} ;
#endif


#if defined(_OS_WINDOWS) || defined (_OS_CYGWIN)
#define MAXSIGS 100
#else
#if defined(_OS_LINUX)
#define MAXSIGS __SIGRTMAX
#else
#if defined(_OS_MACOSX)
#define MAXSIGS 33
#else
#define MAXSIGS _SIGRTMAX
#endif
#endif
#endif

// signal handling
extern int signalnum ;		// set to +ve when signal received
extern bool insignalhandler ;
extern Closure *signals[MAXSIGS] ;		// signal routines, one per signal

#define pushScope(s) {if ((s) == NULL) throw "NULL scope" ; } Scope *oldCurrentScope = currentScope ; currentScope = s 
#define popScope() currentScope = oldCurrentScope

// main Aikido class
class Aikido {
    friend class Worker ;

public:
    Aikido(std::string dir, std::string program, int argc, char **argv, std::istream &cin, std::ostream &cout, std::ostream &cerr, int props = MULTITHREADED, int nincs = 0, char **incs = NULL) ;
    Aikido(std::string objfile, std::string dir, std::string program, std::istream &cin, std::ostream &cout, std::ostream &cerr, int props = MULTITHREADED, int nincs = 0, char **incs = NULL) ;
    ~Aikido() ;
    void init() ;
    void setWorker (Worker *w) ;
    void parse(string file, std::istream &in) ;
    void generate() ;
    void execute(int passes = 1, int argc=-1, char **argv=NULL) ;
    Value call (Object *object, std::string func, std::vector<Value> &args) ;        // call a member function
    Value call (Object *object, Function *func, std::vector<Value> &args) ;        // call a member function
    Value call (Object *object, std::string func, std::vector<Value> &args, std::ostream *out, std::istream *in) ;        // call a member function
    Value call (Object *object, Function *func, std::vector<Value> &args, std::ostream *out, std::istream *in) ;        // call a member function
    Value call (Closure *closure, std::vector<Value> &args) ;        // call a function in a closure
    Value execute (std::istream &in, Object *root, std::ostream *output = NULL, std::istream *input=NULL, bool setCurrentClass=false, bool add_return=false) ;

    Value loadFile (std::vector<std::string> &filenames, std::vector<Value> &args, StackFrame *stack, StaticLink *slink, Scope *scope, int sl, std::string outfile, std::ostream *output = NULL, std::istream *input=NULL) ;
    Value loadFile (std::string filename, std::vector<Value> &args, StackFrame *stack, StaticLink *slink, Scope *scope, int sl, std::string outfile, std::ostream *output = NULL, std::istream *input=NULL) ;

    void readFile (std::vector<std::string> &filenames, StackFrame *stack, StaticLink *slink, Scope *scope, int sl, std::string outfile, std::ostream *output = NULL, std::istream *input=NULL) ;
    void readFile (std::string filename, StackFrame *stack, StaticLink *slink, Scope *scope, int sl, std::string outfile, std::ostream *output = NULL, std::istream *input=NULL) ;

    Value preload (std::string filename, StackFrame *stack, StaticLink *slink, Scope *scope, int sl) ;
    Value preload (std::vector<std::string> &filenames, StackFrame *stack, StaticLink *slink, Scope *scope, int sl) ;

    Value executePreload (Block *block, std::vector<Value> &args, StackFrame *stack, StaticLink *slink, Scope *scope, int sl, std::string outfile, std::ostream *output = NULL, std::istream *input=NULL) ;

    Value newObject (std::string classname, std::vector<Value> &args);
    Value newObject (std::string classname, StackFrame *frame, StaticLink *slink, std::vector<Value> &args);

    Value call (Value func, StackFrame *frame, StaticLink *slink, int nargs, Value *args) ;
    void finalize() ;
    void print(std::ostream &os) ;
    void error (const char *error,...) ;
    void error (string filename, int lineno, const char *error,...) ;
    void warning (const char *w,...) ;

    void createVariable (StackFrame *stack, std::string name, Value &value, Token access=PUBLIC, bool isconst=false, bool isgeneric=false) ;

    InterpretedBlock *findMain() ;		// find a function called 'main' at top level

    CodeSequence *preamble ;
    Debugger *debugger ;
    std::ostream &mainos ;
    StdOutStream *mystdout ;		
    StdInStream *mystdin ;	
    StdOutStream *mystderr ;		// standard error stream for reporting exceptions
    codegen::CodeGenerator *codegen ;
    VirtualMachine *vm ;
#ifdef JAVA
    bool javaEnabled ;
#endif

    static void deleteTree (Node *n) ;
    static bool compareTree (Node *t1, Node *t2) ;
    Value getExpression(StackFrame *stack, StaticLink *staticLink, Scope *currentScope, int currentScopeLevel) ;
    Value getExpression (std::string expr, StackFrame *stack, StaticLink *staticLink, Scope *currentScope, int currentScopeLevel) ;
    void runtimeError (SourceLocation *source, const char *format,...) ;
    StackFrame *getStack (StackFrame *frame, StaticLink *sl, int level) ;

    string directory ;			// directory from which executable is run
    string filename ;
    Node *program ;
    Worker *worker ;

    int properties ;

    // the monitor cache is a cache of monitors attached to a value
    OSMutex monitorCacheLock ;
    typedef std::map<Object *, Monitor*> MonitorMap ;
    typedef std::list<Monitor*> MonitorFreeList ;
    MonitorMap monitorCache ;
    MonitorFreeList monitorFreeList ;

    Monitor *findMonitor (Object *obj) ;
    void freeMonitor (Object *obj) ;

//
// lexical analyser
//

    void reportException (Exception &e) ;
    void reportException (Exception &e, std::ostream &os) ;

    void setTraceLevel (int level) ;

    std::vector<TokenNode *> tokens ;
    void addToken (string s, Token tok) ;
    void printToken (std::ostream &os, Token tok) ;
    
    static const int MAXLINE = 4096 ;
    static const int MAXSPELLING = 1024 ;
    char line[MAXLINE] ;			// input line
    char *ch ;	                        // current position
    char *oldch ;			// old current position
    char spelling[MAXSPELLING] ;		// identifier spelling
    std::string longspelling;
    bool longString;
    INTEGER number ;			// number value
    double fnumber ;			// floating point number value
    Token currentToken ;			// current token
    int numErrors ;
    int numRuntimeErrors ;
    int maxErrors ;
    static const int MAXERRORS = 50 ;

    zip::ZipFile *systemZip ;
    void findSystemZip() ;
    
    void readLongString (char terminator);
    std::string getSpelling();

    void readLine(bool skipblank = true) ;
    bool newLineRead ;

    void resetInput() ;
    void skipSpaces() ;
    void nextToken() ;

    void setLineBased(bool v) { linebased = v ; }

    bool linebased ;
    Context *currentContext ;
    void needcomma() ;			// absorb a comma
    bool isComma() ;			// absorb comma if there
    void needbrack (Token tok) ;	// need a bracket
    void needsemi() ;
    
    bool match (Token tok) ;

    string getOperatorName() ;

    // thread support
    typedef std::map <OSThread_t, OSThread*, std::less<OSThread_t> > ThreadMap ;
    ThreadMap threads ;
    OSMutex threadLock ;
    void insertThread(OSThread_t id, OSThread *t) ;
    void deleteThread(OSThread_t id) ;
    OSThread *findThread (OSThread_t id) ;

    // map of every unique variable in the system
    typedef std::map<string, VariableName*> VariableNameMap ;
    VariableNameMap variableNames ;

    VariableName *findVariableName (const string &name) ;
    VariableName *addVariableName (const string &name, Scope *s, Variable *v) ;

    Variable *addVariable (const string name) ;
    Parameter *addParameter (const string name) ;
    //void addParameter (Parameter *p) ;
    Reference *addReference (const string name) ;
    Variable *findVariable (const string &name, int &level, int access, SourceLocation *source = NULL, Package **p = NULL) ;
    Variable *findVariable (const string &name, int &level, int access, SourceLocation *source, Scope *scope, int scopelevel) ;
    Macro *findMacro (const string &name, Package *&pkg) ;
    Tag *findTag (const string &name, Scope *topscope=NULL) ;
    Variable *findPackageIdentifier (string &name, int &level) ;
    Tag *findPackageTag (string &name) ;
    Variable *findPackageVariable (const string &name, int &level) ;
    Package *findPackage (const string &name) ;
    Variable *findTopScopeVariable (const string &name) ;
    Macro *findTopScopeMacro (const string &name) ;
    void insertVariable (Variable *var) ;
    void insertMacro (Macro *macro) ;
    Node *instantiateMacro (char *args, Macro *macro, Scope *scope) ;
    bool isParentPackage (Package *package) ;

    Variable *pass ;			// current pass
    Variable *dot ;			// current address
    int currentPass ;
    Block *currentMajorScope ;
    StackFrame *currentStack ;			// used for evaluate and load
    Function *currentFunction ;
    Class *currentClass ;
    int currentScopeLevel ;
    StackFrame *mainStack ;
    StaticLink mainSLink;
    Package *mainPackage ;
    Scope *currentScope ;
    Node *importedTree ;                // all the code from imports

    Node *parseStream (std::istream &in) ;
    Node *expression() ;
    Node *single_expression() ;

    bool cacheVariables ;       // true for keeping variable cacheZ

    // Java related functions called from the src/java files
    bool importFile (const string &name) ;
#ifdef JAVA
    Tag *findJavaClass (string &classname) ;
    Tag *createJavaClass (string &classname, Tag *super, int accessflags, int index = 0) ;
    void insertJavaInterface (Tag *cls, Tag *iface) ;
    Variable *createJavaField (Tag *cls, string &name, bool isconst, Token access) ;
    Variable *createJavaStaticField (Tag *cls, string &name, bool isconst, Token access) ;
    void initializeJavaStaticField (Tag *tag, Variable *var, Node *initvalue) ;
    Tag *createJavaMethod (Tag *cls, string &name, Token access, int accessflags, int numArgs) ;
    Tag *createJavaStaticMethod (Tag *cls, string &name, Token access, int accessflags, int numArgs) ;
    void finishJavaClass(Tag *cls) ;
    void addClassInit (Node *call) ;
#endif

    void setLockdownRoot(std::string d) {
        lockdownRootdir = d ;
    }

    void setLockdownUser (std::string user) {
        lockdownUser = user ;
    }
    std::string lockdownRootdir ;          // root directory for lockdown mode
    std::string lockdownUser ;          // user name for lockdown mode


    // alias handling.
    typedef std::map<std::string, std::string> AliasMap ;
    AliasMap aliases ;
    
    void set_alias(std::string name, std::string value, std::ostream &os) ;
    void show_alias (std::string name, std::ostream &os) ;
    void unset_alias (std::string name, std::ostream &os) ;
    
    bool find_alias (std::string name, std::string &value) ;
    void expand_alias (std::string aliasvalue, std::string &cmd) ;
    
    void handle_interrupt() {
        interrupted = true ;
    }
    
private:
    typedef std::map<string, int> OpInfoMap ;
    OpInfoMap opinfo ;
    void checkOperatorDefinition (InterpretedBlock *block) ;

    bool isOperator (char ch) ;
    bool isOverloadableOperator (Token tok) ;
    bool isComplex (Node *n) ;
    Node *parseStatement() ;
    Node *parseCatch();
    Node *parseFinally();
    Node *parseAssignment(string var) ;		// assignment to variable
    Node *parseCommand (Token access, bool isstatic, bool issync) ;		// command

    // annotations
    AnnotationList parseAnnotationSequence();
    void parseAnnotationDefinition(Interface *iface);
    void generateProperty (Variable *var, Block *block, Annotation *propann);

    Node *parseIf() ;
    Node *parseNew(bool emptyok = true) ;
    Node *parseExpressionStatement() ;
    Node *parseStatementSequence(Scope *scope = NULL) ;
    void readForeignLines (std::iostream &s) ;
    bool isRecognized() ;
    Node *getBlock (Token tok, bool isstatic, bool issync, AnnotationList &annotations) ;

    Node *importStream (string full, string fn, std::istream &in, string dbg) ;
    bool appendIfExists (const string &name, std::vector<ImportFile> &result) ;
    void expandImportName (string &name, std::vector<ImportFile> &result) ;

    Node *parsePackage (string name, bool isstatic, bool issync) ;
    void parseUsing() ;
    void extendBlock (Block *block) ;
    Node *getBlockContents (Token command, Tag *tag, Variable *var, InterpretedBlock *block, bool isstatic, bool issync) ;

    Node *addNativeFunction (string name, int np, NativeFunction::FuncPtr, int level = 0, Scope *parentScope = NULL, Class *parentClass = NULL, int pmask = 0, bool israw = false) ;
    Node *addSystemVar (string name, Token t, const Value &v, bool isconst = true) ;
    Node *addFixedTypeVar (string name, Token t, const Value &v, bool isconst = true) ;
    Node *importSystem() ;
    
    string findOperator (Token tok) ;
    Node *parseEnum(Enum *e) ;
    void parseInterfaceBody (Interface *iface) ;
    
    typedef std::map <string, Token, std::less<string> > TokenMap ;
    TokenMap reservedWords ;
    TokenMap lexTokens ;
    typedef std::map<Token, string, std::less<Token> > OperatorMap ;
    OperatorMap operators ;

    std::vector<string> importpaths ;
    std::vector<string> libraries ;		// imported shared libraries

    int blockLevel ;
    int nloops ;
    int nfuncs ;
    int nbackticks ;
    int nswitches ;
    int mainNum ;		// instance number for case of multiple 'main' functions
    bool mainFound ;		// real main found

    Tracer *tracer ;

    Token currentAccessMode ;			// current access mode
    
    bool isBlock() ;
    void copyToEnd(std::ostream &out) ;		// copy input up until end stream
    void skipToEnd() ;

    void checkAssignment (Node *node) ;

    Node *assignmentExpression() ;
    Node *conditionalExpression() ;
    Node *streamExpression() ;
    Node *logicalOrExpression() ;
    Node *logicalAndExpression() ;
    Node *orExpression() ;
    Node *xorExpression() ;
    Node *andExpression() ;
    Node *equalityExpression() ;
    Node *relationalExpression() ;
    Node *shiftExpression() ;
    Node *additiveExpression() ;
    Node *multExpression() ;
    Node *unaryExpression() ;
    Node *postfixExpression(Node *primary = NULL) ;
    Node *newExpression() ;
    Node *identifierExpression(bool fromNew=false) ;
    Node *rangeExpression() ;
    Node *primaryExpression() ;
    
    Node *anonBlock(Token command) ;

    // constant expressions
    INTEGER constExpression() ;
    INTEGER constOrExpression() ;
    INTEGER constXorExpression() ;
    INTEGER constAndExpression() ;
    INTEGER constEqualityExpression() ;
    INTEGER constRelationalExpression() ;
    INTEGER constShiftExpression() ;
    INTEGER constAdditiveExpression() ;
    INTEGER constMultExpression() ;
    INTEGER constUnaryExpression() ;
    INTEGER constPrimaryExpression() ;



    Node *copyTree (Node *node) ;
    void convertAssign (Node *left) ;



    void collectActualArgs (char *args, Context::ParaMap &map, Macro *macro) ;
    void replaceMacroParameters(char *inch, char *out, Context::ParaMap &map) ;
    
// object code dumping
public:
    typedef std::vector<Scope *> ScopeVec ;		// all registered scopes
    ScopeVec scopevec ;
    int nscopes ;
    int registerScope (Scope *s) ;

    void dumpByte (int b, std::ostream &s) ;
    void dumpWord (int w, std::ostream &s) ;
    void dumpString (std::string &str, std::ostream &s) ;
    void dumpString (const string &str, std::ostream &s) ;
    void dumpDirectString (std::string &str, std::ostream &s) ;
    void dumpDirectString (const string &str, std::ostream &s) ;
    void dumpVariableRef (Variable *var, std::ostream &os) ;
    void dumpTagRef (Tag *tag, std::ostream &os) ;
    void dumpTagList (std::list<Tag*> &tlist, std::ostream &os) ;
    void dumpObjectCode (std::string filename) ;
    void dumpValue (const Value &v, std::ostream &os) ;

// object code loading
    int loadpass ;

    void loadObjectCode (std::string filename) ;

    int readByte (std::istream &s) ;
    int peekByte (std::istream &s) ;
    int readWord (std::istream &s) ;
    std::string readString (std::istream &s) ;
    std::string readDirectString (std::istream &s) ;

    Variable *readVariableRef (std::istream &s) ;
    Tag *readTagRef (std::istream &s) ;
    void readTagList (std::list<Tag*> &tlist, std::istream &is) ;
    Value readValue (std::istream &is) ;
    void checkByte (int b, std::istream &is) ;
    Scope *readScope (std::istream &is) ;

    typedef std::map<std::string, int> StringMap ;
    typedef std::vector<std::string> StringVec ;

    StringMap stringmap ;
    StringVec stringvec ;

    int poolString (std::string &s) ;
    std::string &lookupString (int index) ;

   
} ;


// exception creation
Exception AIKIDO_EXPORT newException(VirtualMachine *vm, StackFrame *stack, string reason) ;
Exception AIKIDO_EXPORT newFileException(VirtualMachine *vm, StackFrame *stack, string filename, string reason) ;
Exception AIKIDO_EXPORT newParameterException(VirtualMachine *vm, StackFrame *stack, string func, string reason) ;



// a worker is passed anything that the aikido language doesn't understand.
struct WorkerContext {
    WorkerContext (StackFrame *stk, StaticLink *sl, Scope *cs, int csl, SourceLocation *p, Context *c) : stack(stk), slink(sl), 
                currentScope (cs), currentScopeLevel(csl), currentPos(p), ctx(c) {}
    StackFrame *stack ;
    StaticLink *slink ;
    Scope *currentScope ;
    int currentScopeLevel ;
    SourceLocation *currentPos ;
    Context *ctx;
};

class Worker {
public:
    Worker(Aikido *b) : aikido (b), currentToken (b->currentToken), ch(b->ch), line(b->line),
                      spelling (b->spelling), number (b->number) {}
    ~Worker() {}
    virtual void initialize() = 0 ;			// initialize
    virtual void parse(std::istream &in, int scopelevel, int pass, std::ostream &out, std::istream &input, WorkerContext *ctx) = 0 ;				// parse the streams pa
    virtual void execute() = 0 ;			// execute the parsed data
    virtual void finalize() = 0 ;			// do things to close down
    virtual void print (std::ostream &os) {}		// print parsed data
    virtual bool isClaimed (std::string s) { return false ; }
    virtual void preparePass (int pass) {}
    virtual bool isLoopback() { return false; }

    Aikido *aikido ;
    void error (const char *error,...) ; 
    void error (string filename, int lineno, const char *error,...) ; 
    void needcomma() { aikido->needcomma() ; }
    bool isComma() { return aikido->isComma() ; } 
    void needbrack(Token tok) { aikido->needbrack(tok) ; }
    bool match (Token tok) { return aikido->match(tok) ; }
    void readLine() { aikido->readLine() ; }
    void skipSpaces() { aikido->skipSpaces() ; }
    void nextToken() { aikido->nextToken() ; }
    Variable *addVariable(string n) { 
        aikido->mainStack->checkCapacity() ; 
        Variable *var = new Variable (n, aikido->mainPackage->stacksize++) ;
        aikido->mainPackage->insertVariable (var) ; 
        var->setAccess (PUBLIC) ; 
        return var ; 
    }
    Variable *findVariable (WorkerContext *ctx, const string name, int &level) { return aikido->findVariable (name, level, VAR_PUBLIC, ctx->currentPos, ctx->currentScope, ctx->currentScopeLevel) ; }
    void addToken (WorkerContext *ctx, string s, int tok) { aikido->addToken (s,(Token)tok) ; }
    Value getExpression(WorkerContext *ctx) { return aikido->getExpression(ctx->stack, ctx->slink, ctx->currentScope, ctx->currentScopeLevel) ; }
    Value getExpression(WorkerContext *ctx, std::string expr) { return aikido->getExpression(expr, ctx->stack, ctx->slink, ctx->currentScope, ctx->currentScopeLevel) ; }
    Value getVariableValue (WorkerContext *ctx, Variable *var, int level) { return var->getValue (aikido->getStack (ctx->stack, ctx->slink, level)) ; }
    Value *getAddress (WorkerContext *ctx, Variable *var, int level) { return var->getAddress (aikido->getStack (ctx->stack, ctx->slink, level)) ; }
    void setVariableValue (WorkerContext *ctx, Variable *var, Value val, int level) { var->setValue (val, aikido->getStack (ctx->stack, ctx->slink, level)) ; }
    Value loadFile (WorkerContext *ctx, std::string filename, std::vector<Value> &args, std::string outfile, std::ostream *output=NULL, std::istream *input=NULL) {
        return aikido->loadFile (filename, args, ctx->stack, ctx->slink, ctx->currentScope, ctx->currentScopeLevel, outfile, output, input) ;
    }
    void readFile (WorkerContext *ctx, std::string filename, std::string outfile, std::ostream *output=NULL, std::istream *input = NULL) {
        aikido->readFile (filename, ctx->stack, ctx->slink, ctx->currentScope, ctx->currentScopeLevel, outfile, output, input) ;
    }
    Value preload (WorkerContext *ctx, std::string filename) {
        return aikido->preload (filename, ctx->stack, ctx->slink, ctx->currentScope, ctx->currentScopeLevel) ;
    }

    Value executePreload (WorkerContext *ctx, Block *block, std::vector<Value> &args, std::string outfile, std::ostream *output=NULL, std::istream *input=NULL) {
        return aikido->executePreload (block, args, ctx->stack, ctx->slink, ctx->currentScope, ctx->currentScopeLevel, outfile, output, input) ;
    }

    Value call (WorkerContext *ctx, Value &func, int nargs, Value *args) {
        return aikido->call (func, ctx->stack, ctx->slink, nargs, args) ;
    }
    
    Value newObject (WorkerContext *ctx, std::string classname, std::vector<Value> &args) {
        return aikido->newObject (classname, ctx->stack, ctx->slink, args) ;
    }
    
    void parse(std::istream &in, StackFrame *stk, StaticLink *staticLink, Scope *scope, int scopelevel, SourceLocation *source, std::ostream &os, std::istream &is, Context *ctx, int pass = 1) { 

        WorkerContext wctx (stk, staticLink, scope, scopelevel, source, ctx);

        // call the parser
        parse (in, scopelevel, pass, os, is, &wctx) ; 
    }
    
    Token &currentToken ;
    char *&ch ;
    char (&line)[4096] ;
    char (&spelling)[1024] ;
    INTEGER &number ;
    
} ;



class LoopbackWorker : public Worker {
public:
    LoopbackWorker (Aikido *a) : Worker(a) {}
    ~LoopbackWorker() {}
    virtual bool isLoopback() { return true; }
    void initialize() ;			// initialize
    void parse(std::istream &in, int scopelevel, int pass, std::ostream &out, std::istream &input, WorkerContext *ctx) ;				// parse the streams pa
    void execute() ;			// execute the parsed data
    void finalize() ;			// do things to close down
    void print (std::ostream &os) ;		// print parsed data
    bool isClaimed (std::string s) ;
    void preparePass (int pass) ;
};


inline void Variable::checkAccess (Aikido *p, Block *block, SourceLocation *source, int required) {
    if (!noaccesscheck && (flags & required) == 0) {
        string err = "Access denied to " ;
        if (flags & VAR_PRIVATE) {
            err += "private" ;
        }
        if (flags & VAR_PROTECTED) {
            err += "protected" ;
        }
        string fullname = block->name ;
        Block *b = block->parentBlock ;
        while (b != NULL) {
            fullname.insert (0, b->name + ".") ;
            b = b->parentBlock ;
        }

        err += " member " + fullname + "." + name ;
        if (source != NULL) {
            p->runtimeError (source, err.c_str()) ;
        } else {
            p->error (err.c_str()) ;
        }
    }
}

inline void Variable::setAccess (Token access) {
    switch (access) {
    case PUBLIC:
        flags |= VAR_PUBLIC ;
        break ;
    case PRIVATE:
        flags |= VAR_PRIVATE ;
        break ;
    case PROTECTED:
        flags |= VAR_PROTECTED ;
        break ;
    default:;
    }
}


extern std::string lockFilename (Aikido *b, const Value &v) ;

//
// native functions
//


#if defined(_OS_WINDOWS)
#define AIKIDO_NATIVE(func) aikido::Value __declspec(dllexport) Aikido__##func (aikido::Aikido *b, aikido::VirtualMachine *vm, const aikido::ValueVec &paras, aikido::StackFrame *stack, aikido::StaticLink *staticLink, aikido::Scope *currentScope, int currentScopeLevel)
#else
#define AIKIDO_NATIVE(func) aikido::Value Aikido__##func (aikido::Aikido *b, aikido::VirtualMachine *vm, const aikido::ValueVec &paras, aikido::StackFrame *stack, aikido::StaticLink *staticLink, aikido::Scope *currentScope, int currentScopeLevel)

#endif
AIKIDO_NATIVE(monitorWait) ;
AIKIDO_NATIVE(monitorTimewait) ;
AIKIDO_NATIVE(monitorNotify) ;
AIKIDO_NATIVE(monitorNotifyAll) ;
AIKIDO_NATIVE(threadSleep) ;
AIKIDO_NATIVE(setThreadPriority) ;
AIKIDO_NATIVE(getThreadPriority) ;
AIKIDO_NATIVE(getThreadID) ;
AIKIDO_NATIVE(threadJoin) ;


}

#endif 
