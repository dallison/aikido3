/*
 * aikidodebug.h
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

#ifndef _aikidodebug_h_included
#define _aikidodebug_h_included


namespace aikido {

// debugger classes
//
// Debugger operation notes
// ------------------------
//
// The debugger is called wholly from the interpreter main loop.  The main
// interface function is 'run'.  This is called when a virtual machine
// wants to stop and get a debugger prompt.  The debugger then prompts the
// user and waits for a command.  The command entered by the user can
// be either an internal debugger command (in which case the debugger
// stays in the loop), or a command that causes the debugger to
// exit the loop.
//
// If the debugger exits the loop it will set the 'debugState' of
// the current virtual machine to one of the known debug
// states.
//
// Threads:
// --------
// A program may be multithreaded.  When one of the threads in the program
// wants to enter the debugger it will call the 'run' function.  All the
// other threads in the program need to be stopped while this is happening.
// The other threads detect that the debugger has stopped by calling the
// 'isStopped' function and if so, they then call the 'blockThread'
// function.  They remain blocked in the 'blockThread' function
// until the run resumes.
//
// If the user switches threads while in the debugger, it is arranged
// so that the thread that originally called the 'run' function (and
// is still there of course) will have its debugState variable set
// to RUN and the thread to which the user has switched will
// have its debugState set to the command the user issued upon
// return from 'blockThread'
//
// Breakpoints
// -----------
// Breakpoints are implemented by replacing the opcode of the instruction
// at a particular address with the special BREAKPOINT instuction.  When
// the interpreter hits this instruction it will call 'breakpointHit'
// in the debugger.  This will call the 'run' function to wait for commands.
// When the run function terminates, the breakpointHit function will
// set the last parameter to the real opcode that was replaced
// by the BREAKPOINT opcode.  The interpreter will reexecute the
// same instruction, but this time with the real opcode.



// smart pointer used by debugger.  This is used because block extension may
// move code around and the debugger needs pointers to the code.

class InstructionPtr {
public:
    InstructionPtr() : code (NULL), offset (0) {}
    InstructionPtr (CodeSequence *c, int off) : code (c), offset (off) {
    }


    Instruction *getInst() const ;

    bool operator <= (InstructionPtr &p) {
        return offset <= p.offset ;
    }

    bool operator >= (InstructionPtr &p) {
        return offset >= p.offset ;
    }

    void relocate (int delta) {
        offset += delta ;
    }

private:
    CodeSequence *code ;
    int offset ;
} ;


// to allow the interpreter to rerun
class RerunException {
} ;

// a breakpoint in the debugger
class Breakpoint {
public:
    Breakpoint (const InstructionPtr &i, string cmd, string cond) ;
    ~Breakpoint() ;

    string cmd ;		// breakpoint command
    string condition ;		// condition
    InstructionPtr inst ;	// the instruction that was replaced
    int opcode ;
    void disable() ;
    void enable() ;
    bool disabled ;
} ;

// a file as far as the debugger is concerned
struct File {
    File (string name) ;
    void addLine (const InstructionPtr &inst) ;
    InstructionPtr &findLine (int line) ;

    typedef std::map<int, InstructionPtr> LineMap ;
    LineMap lines ;
    string name ;
} ;


// the debugger representation of a block
class DBlock {
public:
    DBlock (aikido::Block *block, SourceLocation *src, CodeSequence *code, bool sysblock) ;
    DBlock (aikido::Block *block, const string &filename, int lineno, bool sysblock) ;
    CodeSequence *code ;
    string fullname ;
    string filename ;
    int lineno ;
    Block *block ;
    bool systemblock ;
} ;

enum DebugState {
    RUN, STEP, NEXT, NEXTRUN, NEXTI, STEPI, STEPUP, RERUN
} ;

struct StackContext {
    StackContext (StackFrame *f=NULL, StaticLink *s=NULL) : frame (f), slink(s) {}
    StackFrame *frame ;
    StaticLink *slink ;
} ;

// the debugger itself
class Debugger {
public:
    Debugger (Aikido *aikido, bool on) ;
    void run(VirtualMachine *vm, Instruction *inst, Scope **scopeStack, int scopeSP, StackFrame *stack, StaticLink *staticLink) ;
    void breakpointHit (VirtualMachine *vm, Instruction *inst, Scope **scopeStack, int scopeSP, StackFrame *stack, StaticLink *staticLink, int &bpop) ;
    int findBreakpointOp (Instruction *inst) ;
    Breakpoint *findBreakpoint (Instruction *inst) ;

    void registerFile (string filename) ;
    void *pushFile (string filename) ;		// push file and return old context
    void popFile(void *file) ;			// restore old context
    void registerInstruction (CodeSequence *c, int i) ;
    void registerBlock (Block *blk) ;
    void registerPackageBlock (Block *blk, const string &filename, int lineno) ;		// register an open package block
    void registerBlock (Block *blk, SourceLocation *source, CodeSequence *code) ;
    void relocate (Block *blk, CodeSequence *code, int start, int end, int offset) ;
    void setFile (Block *blk) ;

    void stopInMain() ;

    bool stopThrow() { return stopOnThrow ; }

    bool isStopped() { return stopped ; }


    void registerThread (VirtualMachine *vm) ;
    void unregisterThread (VirtualMachine *vm) ;
    void blockThread() ;

private:

    enum Commands {
		CMD_QUIT, CMD_STOP, CMD_CONT, CMD_RUN, CMD_CLEAR, CMD_STEP, CMD_NONE, CMD_STATUS,
		CMD_HELP, CMD_NEXT, CMD_PRINT, CMD_UP, CMD_DOWN, CMD_WHERE, CMD_LIST, CMD_VI, CMD_FILE,
                CMD_FILES, CMD_SHOW, CMD_SET, CMD_CALL, CMD_NEXTI, CMD_STEPI, CMD_ALIAS, CMD_DIS, CMD_UNALIAS,
                CMD_THREAD, CMD_THREADS, CMD_DISABLE, CMD_ENABLE, CMD_HISTORY
    } ;

    typedef std::map<std::string, int> CommandMap ;
    CommandMap commands ;

    File *currentFile ;
    StackFrame *currentStack ;
    StaticLink *currentStaticLink ;
    typedef std::map<string, File *> FileMap ;
    FileMap files ;
    bool stopped ;		// is the program stopped in the debugger?
    std::vector<Breakpoint*> breakpoints ;
    bool stopOnThrow ;

    File *findFile (string name) ;
    Breakpoint *setBreakpoint (string blockname) ;			// stop in block
    Breakpoint *setBreakpoint (string file, int line) ;			// stop at file:line
    void deleteBreakpoint (Breakpoint *bp) ;
    bool evalCondition (string cond, Scope *scope, int level, StackFrame *stack, StaticLink *staticLink) ;

    int getNumber (string s, int &i) ;
    string getString (string s, int &i) ;
    string getCondition (string s, int &i) ;
    void stop (string cmd) ;
    void where (string cmd) ;
    void up (string cmd) ;
    void down (string cmd) ;
    void print (string cmd) ;
    void list (string cmd) ;
    void file (string cmd) ;
    Value exec (string cmd) ;
    void printLine (SourceLocation *source) ;
    void readLine (std::istream &is, char *buf, int len) ;
    void status (string cmd) ;
    void help (string cmd) ;
    void listFiles (string cmd) ;
    void clear (string cmd) ;
    void show (string cmd) ;
    void alias (string cmd) ;
    void unalias (string cmd) ;
    void dis (string cmd) ;
    void controlBreakpoint (bool enable, string cmd) ;
    void listThreads (string cmd) ;
    void switchThread (string cmd) ;
    void showvars (int sp, StackFrame *f) ;
    void showvars (string cmd) ;
    void printBlock (std::ostream &os, Block *blk, Value *base, int ind) ;
    void printValue (std::ostream &os, Value &v) ;

    Aikido *aikido ;
    Scope *currentScope ;
    Scope **scopeStack ;
    int scopeSP ;
    int topSP ;
    int currentScopeLevel ;
    Instruction *currentInstruction ;
    StackContext stackTop ;
    int stackLevel ;
    Instruction *topInstruction ;
    bool debugging ;
    std::vector<StackContext> callStack ;
    std::vector<DBlock*> blocks ;

    typedef std::map<string, string> AliasMap ;
    AliasMap aliases ;
   
    DBlock *findBlock (string name) ;

    void rerun() ;

    VirtualMachine *currentVM ;

    OSMutex threadlock ;
    OSSemaphore threadsem ;
    int blockedthreads ;
    
    std::list<VirtualMachine*> threads ;
    void unblockThreads(DebugState result) ;

    // history
    std::vector<std::string> history ;
    std::string replaceHistory (const std::string &cmd) ;
    void showHistory() ;
} ;


// execution tracer - not used very much yet
class Tracer {
public:
    Tracer (Aikido *aikido, Debugger *dbg, int level, std::ostream &out) : aikido (aikido), debugger (dbg), tracelevel (level), os (out) {
    }

    void setTraceLevel (int level) { tracelevel = level ; }

    void traceCall (Type type, Block *blk) ;		// trace a call to something
    void traceValue (Value &v) ;			// trace a value
    void traceVariable (Variable *var, Value &v) ;	// trace assignment to variable
    void traceLine (string file, int line) ;
    void traceNewline() ;

private:
    int tracelevel ;
    Aikido *aikido ;
    Debugger *debugger ;
    std::ostream &os ;
} ;

}

#endif

