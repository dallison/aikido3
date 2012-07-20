/*
 * aikidointerpret.h
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
 * Closure support is Copyright (C) 2004 David Allison.  All rights reserved.
 * 
 * Contributor(s): dallison
 *
 * Version:  1.25
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */

// CHANGE LOG
// 10/21/2004   David Allison         Added closure support



#ifndef _aikidointerpret_h
#define _aikidointerpret_h

#include "aikido.h"
#include "aikidocodegen.h"
#if defined(_CC_GCC)
#ifndef __THROW
#define __THROW
#endif
typedef unsigned char uchar_t ;
extern "C" int pipe (int[]) __THROW ;
#endif


namespace aikido {

using namespace codegen ;

class VirtualMachine ;

extern Package *system ;
extern Class *pairClass ;

class Foreach {
public:
    Foreach (VirtualMachine *vm, Value *var, Value &val, int end, int pc, InterpretedBlock *opfunc) ;
    Foreach (VirtualMachine *vm, Value *var, Value &val, int end, int pc, InterpretedBlock *opfunc, Value &endval) ;
    ~Foreach() ;
    void next() ;
    int getPC() { return pc ; }
    int getEnd() { return endaddr ; }
    void setControlVar (const Value &v) { *controlVar = v ; }

private:
    VirtualMachine *vm ;
    Value *controlVar ;
    Value value ;		// must be copy of parameter
    int endaddr ;
    int pc ;			// pc of block body

    Type workingType ;		// type being worked on
    int direction ;		// 1 == up, -1 == down
    int currindex ;		// current index
    int endindex ;		// end index

    // for maps
    map::iterator currmap ;
    map::iterator endmap ;

    // for object with operator foreach
    InterpretedBlock *opfunc ;
    Value iterator ;

    // for enumeration constants
    EnumConst *currec ;
    EnumConst *endec ;

    // for coroutines

} ;

typedef std::map<string, OS::Regex *> RegExMap ;

typedef std::map<string, Function*> BuiltinMethodMap ;

// new interpreter
// one VirtualMachine object for each running thread

class VirtualMachine {
    friend struct RawNativeFunction ;
public:
    VirtualMachine (Aikido *aikido) ;
    ~VirtualMachine() ;
    bool execute(InterpretedBlock *block, StackFrame *f, StaticLink *slink, int startaddr) ;
    bool execute(InterpretedBlock *block, CodeSequence *code, StackFrame *f, StaticLink *slink, int startaddr) ;
    bool execute(CodeSequence *code) ;
    bool execute (CodeSequence *code, StackFrame *f, StaticLink *slink, int startaddr) ;

    void runtimeError (SourceLocation *, const char *s,...);
    void runtimeError (const char *s,...);
    void callFunction (Operand *dest, Block *func, const Value &thisptr) ;
    void callFunction (Operand *dest, Block *func, Object *thisptr) ;
    void callFunction (Operand *dest, Block *func, StaticLink *slink, const Value &thisptr, const Value &arg) ;
    void callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg) ;
    void callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg1, const Value &arg2) ;
    void callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg1, const Value &arg2, const Value &) ;
    void callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg1, const Value &arg2, const Value &, const Value &) ;
    void callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg1, const Value &arg2, const Value &, const Value &, const Value &) ;
    void callFunction (Operand *dest, Block *func, const Value &thisptr, const Value &arg1, const Value &arg2, const Value &, const Value &, const Value&, const Value&) ;

    void randomCall (Operand *dest, Block *func, StackFrame *frame, StaticLink *staticLink, 
           const Value &thisptr, const Value &arg, bool isclosure=false) ;
    void randomCall (Operand *dest, Block *func, StackFrame *frame, StaticLink *staticLink, 
           const Value &thisptr, const Value &arg1, const Value &arg2, bool isclosure=false) ;
    void randomCall (Operand *dest, Block *func, StackFrame *frame, StaticLink *staticLink, bool isclosure=false) ;
    void randomCall (Operand *dest, Block *func, Object *thisptr, StackFrame *frame, StaticLink *staticLink, bool isclosure=false) ;
    void randomCall (Operand *dest, Block *func, StackFrame *f, StaticLink *slink, const Value &thisptr, const std::vector<Value> &args, bool isclosure=false) ;


    // call a block and return a value
    Value call (Block *func, StackFrame *frame, StaticLink *staticLink, int nargs, Value *args) ;
    Value newObject (Block *block, StackFrame *f, StaticLink *slink, std::vector<Value> &args) ;


    InterpretedBlock *checkForOperator (const Value &left, Token tok);

    Value retval ;			// return value from call
    DebugState debugState ;
    VirtualMachine *rerunVM ;           // written by debugger for rerun

    void stream (Operand *dest, Value &left, Value &right) ;

    void showPosition (std::ostream &os) ;
    void getPosition (Scope *&scope, int &level, StackFrame *&stack, StaticLink *&slink, Instruction *&inst) ;

    StaticLink *getStaticLink() {
        return staticLink ;
    }

    // get and set the result of last system call
    int getLastSystemResult() { return lastSystemResult ; }
    void setLastSystemResult (int r) { lastSystemResult = r ; }

    Aikido *aikido ;
    Value output ;                    // output stream for this vm
    Value input ;                    // input stream for this vm

    void *save_state(int pc, int ssp, int regsp, int fss, StackFrame *f, Object *thisobj, CodeSequence *code) ;
    void restore_state (void *state) ;
    static void free_state (void *state) ;
    Value invoke_coroutine (Closure *closure) ;

    Instruction *getIR() { return ir;}
 private:
    bool execute (int startaddr) ;

    void processSignal (int sig) ;

    static const int MAXREGS = 1000 ;
    static const int MAXSTACK = 100 ;
    static const int MAXSCOPE = 10000 ;


    int currstacksize ;
    void growStack() ;			// stack is about to overflow, grow it

    Value *registers ;
    Value *stack ;
    int sp ;				// stack pointer
    int regs ;				// start of register set
    Value *regfile ;			// current register file
    int pc ;				// program counter
    Scope *scopeStack[MAXSCOPE] ;
    int scopeSP ;
    Value exception ;			// store for exception handler
    StackFrame *frame ;			// current stack frame
    StaticLink *staticLink ;		// parent's stack frame
    CodeSequence *currentCode ;         // current code sequence
    Instruction *ir ;			// instruction register
    std::stack<Foreach*> forStack ;	// stack for foreach loops
    RegExMap regexMap ;			// map of string versus compiled regular expressions
    Value none ;			// hard coded value for none
    int lastSystemResult ;              // exit code from last system call

    Value &get (Operand *op) ;
    void checkType (const string &varname, const Value &val, Type t, const string &typen, Object *obj = NULL)  ;
    void checkType (const string &varname, const Value &val, Type t, const Value &typen, Object *obj = NULL)  ;
    void checkType (const string &varname, const Value &val, Type t, const char *typen, Object *obj = NULL)  ;
    void checkType (const string &varname, const Value &val, Object *obj)  ;
    void setVariable (Variable *var, StackFrame *stk, const Value &v, bool checkconst, bool checktype) ;
    void setVariable (Variable *var, StackFrame *stk, Object *obj, bool checkconst, bool checktype) ;
    void set (Operand *dest, const Value &v, bool checkconst = true, bool checktype = true) ;
    void overassign (Operand *dest, Value &src) ;
    Value copy (const Value &v, bool replacevars) ;
    void dosizeof (Operand *dest, Operand *op) ;
    void dotypeof (Operand *dest, Operand *op) ;
    Value *getaddr (Operand *op) ;
    void call (Operand *dest, Operand *nargs, Operand *func, Operand *types=NULL) ;
    bool supercall(Operand *nargs, Operand *func, Operand *types=NULL) ;
    Object *getthis (Operand *src) ;
    StackFrame *getStack (int level);
    Variable *findMember (Value &val, Block *block, StackFrame *stack, bool directParent) ;
    void dotry (Operand *catchop, Operand *endop, Operand *finallyop);

    bool callOperatorDot (Operand *dest, Value &left, Value &right);

    // type fixed functions
    void setint (Operand *dest, INTEGER v) ;
    void setreal (Operand *dest, double v) ;
    void setstring (Operand *dest, string *s) ;
    void setbool (Operand *dest, bool v) ;
    void setvector (Operand *dest, Value::vector *v) ;

    void callFunction (Operand *dest, int nargs, Function *func, bool isclosure = false) ;

    Node *getBlockAnnotationValue (Annotation *ann, Tag *tag, std::string func);
    void getAnnotations (Operand *dest, Operand *var) ;

    void callThread (Operand *dest, int nargs, Function *func);
    bool assignParameters (Function *func, StackFrame *newframe, int nactuals, bool thispresent, int ntypes=0);
    bool assignNativeParameters (Function *func, StackFrame *newframe, int nactuals, bool thispresent, ValueVec &parameters);
    void newObject (Operand *dest, int nargs, Type type, InterpretedBlock *block, bool thispresent, int ntypes=0);
    void makeVector (Operand *dest, Operand *n) ;
    void makeMap (Operand *dest, Operand *n) ;
    void streamCopy (Operand *dest, Value &left, Value &right) ;
    void streamObject (Operand *dest, Value &left, Value &right) ;
    bool convertStringToInt (string str, INTEGER &r) ;
    void cast (Operand *dest, Operand *to, Operand *from, Operand *pnum) ;
    void findValue (Operand *dest, Operand *s1, Operand *s2) ;
    void storeValue (Operand *dest, Operand *s1, Operand *s2, Operand *val) ;
    void findAddress (Operand *dest, Operand *s1, Operand *s2) ;
    const char *getMemname (Value &val) ;
    bool findParentBlock (Block *block, StackFrame *&s) ;
    bool isParentBlock (Block *block) ;
    bool blocksCompatible (Block *a, Block *b) ;
    void instantiateMacro (Macro *mac, Operand *lab) ;
    void foreach (Value *var, Value val, int end) ;
    void foreach (Value *var, Value val, Value endval, int end) ;
    void next() ;
    void newVector (Operand *dest, int index, int ndims) ;
    void newVector (Operand *dest, int index, int ndims, int ctstart, int ctend, Value &firstelement) ;
    void newByteVector (Operand *dest, int index, int ndims, int ctstart, int ctend, unsigned char firstelement) ;
    void dodelete (Value &addr) ;
    void doenum (Value &en) ;
    void delegate (Value &s) ;
    void doinline(Operand *dest, Operand *endop) ;

    void illegalop (const Value &v1, const Value &v2, const char *op) ;
    void illegalop (const Value &v, const char *op) ;

public:
    bool convertType (const Value &from, Value &to) ;
    string typestring (const Value &v) ;

    // closures
    void make_closure (Value &dest, const Value &v) ;
    void make_closure (Value &dest, Operand *op) ;
    void make_closure_addr (Value &dest, Operand *op) ;

    // value manipulation functions
    bool cmpeq (const Value &v1, const Value &v2, const char *op = "==") ;
    bool cmpne (const Value &v1, const Value &v2) ;
    bool cmplt (const Value &v1, const Value &v2, const char *op = "<") ;
    bool cmple (const Value &v1, const Value &v2) ;
    bool cmpgt (const Value &v1, const Value &v2, const char *op = "<") ;
    bool cmpge (const Value &v1, const Value &v2) ;

    void add (Operand *r, const Value &v1, const Value &v2) ;
    void sub (Operand *r, const Value &v1, const Value &v2) ;
    void mul (Operand *r, const Value &v1, const Value &v2) ;
    void div (Operand *r, const Value &v1, const Value &v2) ;
    void mod (Operand *r, const Value &v1, const Value &v2) ;
    void srl (Operand *r, const Value &v1, const Value &v2) ;
    void sra (Operand *r, const Value &v1, const Value &v2) ;
    void sll (Operand *r, const Value &v1, const Value &v2) ;
    void bitwiseand (Operand *r, const Value &v1, const Value &v2) ;
    void bitwiseor (Operand *r, const Value &v1, const Value &v2) ;
    void bitwisexor (Operand *r, const Value &v1, const Value &v2) ;
    void uminus (Operand *r, const Value &v1) ;
    void boolnot (Operand *r, const Value &v1) ;
    void comp (Operand *r, const Value &v1) ;
 
    void checkZero (INTEGER v) ;
    void checkZero (double v) ;

    bool isIntegral (const Value &v) ;
    bool subscriptok (const Value &v, int i) ;

    // single index subscripting
    void addrsub (Operand *dest, Value &srcaddr, Value &index) ;
    void getsub (Operand *dest, Value &srcaddr, Value &index) ;
    void setsub (Operand *dest, Value &val, Value &srcaddr, Value &index) ;
    void delsub (Value &srcaddr, Value &index) ;

    // 2 index subscripting
    void getsub (Operand *dest, Value &srcaddr, Value &lo, Value &hi) ;
    void setsub (Operand *dest, Value &val, Value &srcaddr, Value &lo, Value &hi) ;
    void delsub (Value &srcaddr, Value &lo, Value &hi) ;

    void in (Operand *dest, Operand *src1, Operand *src2) ;
    void inrange (Operand *dest, Operand *src1, Operand *src2, Operand *src3) ;

    void instanceof (Operand *dest, Operand *obj, Operand *cls) ;
#ifdef JAVA
    // java specific
    void calljava (Operand *dest, Operand *args, int noverloads) ;
    void calljni (Operand *dest, int nargs, void *code) ;
    void checkcast (Operand *dest, Operand *cls, Operand *obj) ;
    void realcompare (Operand *dest, Operand *v1, Operand *v2, int nanflag) ;
  
    void tableswitch (Operand *value, int lo, int hi) ;
    void lookupswitch (Operand *value, int npairs) ;
#endif

    void monitorEnter (Operand *obj) ;
    void monitorExit (Operand *obj) ;

    BuiltinMethodMap builtinMethods ;
    void initBuiltinMethods() ;
    Function *findBuiltinMethod (const string &name) ;
   
    StackFrame *getMainStack() ;
} ;

// this is thrown when a yield is executed.  It is caught at the function level and used
// to capture the current state
struct YieldValue {
    YieldValue (int pc, int scopeSP, int regs, CodeSequence *code, const Value &value) : pc(pc), scopeSP(scopeSP), regs(regs), code(code), value(value) {
    }
    int pc ;
    int scopeSP ;   // scope sp at point of yield
    int regs ;      // reg pointer at point of yield
    CodeSequence *code ;
    Value value ;   // value yielded
} ;

}

#endif
