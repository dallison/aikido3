/*
 * debug.cc
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
 * Version:  1.25
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/08/11
 */



//
// aikido debugger
// Author: David Allison
// Version: 1.25 08/11/03
//

#include "aikidotoken.h"
#include "aikido.h"
#include "aikidodebug.h"
#include <stdlib.h>
#include <ctype.h>
#include "aikidointerpret.h"
#include <signal.h>


namespace aikido {

extern bool readingsystem ;		// am I reading the system files?

using codegen::Instruction ;
using codegen::CodeSequence ;

bool localinterrupted ;

extern "C" void localinterrupt (int sig) {
    localinterrupted = true ;
    printf ("** interrupted\n") ;
}


//
// breakpoints.  These work by replacing the opcode of an instruction with
// codegen::BREAKPOINT and storing the original opcode in the breakpoint object
//

Breakpoint::Breakpoint (const InstructionPtr &n, string c, string cond) : inst (n), cmd (c), condition (cond), disabled(false) {
    if (inst.getInst()->opcode == codegen::opBREAKPOINT) {
        throw "There is already a breakpoint at this address" ;
    }
    opcode = inst.getInst()->opcode ;

    inst.getInst()->opcode = codegen::opBREAKPOINT ;
}

Breakpoint::~Breakpoint() {
    if (!disabled) {
        inst.getInst()->opcode = static_cast<codegen::Opcode>(opcode );
    }
}

void Breakpoint::disable() {
    if (!disabled) {
        disabled = true ;
        inst.getInst()->opcode = static_cast<codegen::Opcode>(opcode );
    }
}

void Breakpoint::enable() {
    if (disabled) {
        disabled = false ;
        inst.getInst()->opcode = codegen::opBREAKPOINT ;
    }
}



//
// our idea of a file
//

File::File (string n) : name (n) {
}


//
// add a line to a file
//
void File::addLine (const InstructionPtr &inst) {
    lines[inst.getInst()->source->lineno] = inst ;
}

InstructionPtr &File::findLine (int line) {
    LineMap::iterator result = lines.find (line) ;
    if (result == lines.end()) {
        throw "No code at this line" ;
    } else {
        return (*result).second ;
    }
}

//
// the debugger's representation of a block
//
DBlock::DBlock (Block *blk, SourceLocation *source, CodeSequence *c, bool sys) : code (c), block (blk), systemblock(sys) {
    filename = source->filename ;
    lineno = source->lineno ;
    Block *b = block->parentBlock ;
    fullname = block->fullname ;
    fullname.insert (0, string (".")) ;
    //std::cout << "block " << fullname << " inserted for file " << file << " at line " << line << '\n' ;
}

DBlock::DBlock (Block *blk, const string &fn, int ln, bool sys) : code (NULL), block (blk), systemblock(sys) {
    filename = fn ;
    lineno = ln ;
    code = new CodeSequence() ;		// empty code sequence
    Block *b = block->parentBlock ;
    fullname = block->fullname ;
    fullname.insert (0, string (".")) ;
    //std::cout << "block " << fullname << " inserted for file " << file << " at line " << line << '\n' ;
}

// 
// main debugger constructor
//
Debugger::Debugger (Aikido *d, bool on) : aikido (d), debugging (on) {
    currentFile = NULL ;
    currentScope = NULL ;
    currentScopeLevel = 0 ;
    currentInstruction = NULL ;
    stopOnThrow = false ;
    stopped = false ;
    blockedthreads = 0 ;

    commands["quit"] = CMD_QUIT ;
    commands["exit"] = CMD_QUIT ;
    commands["stop"] = CMD_STOP ;
    commands["cont"] = CMD_CONT ;
    commands["c"] = CMD_CONT ;
    commands["run"] = CMD_RUN ;
    commands["clear"] = CMD_CLEAR ;
    commands["step"] = CMD_STEP ;
    commands["s"] = CMD_STEP ;
    commands["next"] = CMD_NEXT ;
    commands["n"] = CMD_NEXT ;
    commands["print"] = CMD_PRINT ;
    commands["status"] = CMD_STATUS ;
    commands["help"] = CMD_HELP ;
    commands["up"] = CMD_UP ;
    commands["down"] = CMD_DOWN ;
    commands["where"] = CMD_WHERE ;
    commands["list"] = CMD_LIST ;
    commands["file"] = CMD_FILE ;
    commands["files"] = CMD_FILES ;
    commands["show"] = CMD_SHOW ;
    commands["set"] = CMD_SET ;
    commands["call"] = CMD_CALL ;
    commands["stepi"] = CMD_STEPI ;
    commands["nexti"] = CMD_NEXTI ;
    commands["si"] = CMD_STEPI ;
    commands["ni"] = CMD_NEXTI ;
    commands["alias"] = CMD_ALIAS ;
    commands["unalias"] = CMD_UNALIAS ;
    commands["dis"] = CMD_DIS ;
    commands["thread"] = CMD_THREAD ;
    commands["threads"] = CMD_THREADS ;
    commands["disable"] = CMD_DISABLE ;
    commands["enable"] = CMD_ENABLE ;
    commands["history"] = CMD_HISTORY ;
}

//
// parse a number from a command
//

int Debugger::getNumber (string s, int &i) {
    int n = 0 ;
    int len = s.size() - i ;
    while (i < s.size() && isspace (s[i])) i++ ;
    if (len > 2 && s[i] == '0' && (s[i + 1] == 'x' || s[i+1] == 'X')) {		// hex number
        i += 2 ;
        while (len-- > 0 && isxdigit (s[i])) {
            char c = toupper (s[i++]) ;
            if (c >= 'A') {
                n = (n << 4) | (c - 'A' + 10) ; 
            } else {
                n = (n << 4) | (c - '0') ; 
            }
        }
    } else {
        while (len-- > 0 && isdigit (s[i])) {
            n = (n * 10) + s[i++] - '0' ;
        }
    }
    return n ;
}

//
// parse a string from a command
//
string Debugger::getString (string s, int &i) {
    int len = s.size() ;
    while (i < len && isspace (s[i])) i++ ;
    string r ;
    while (i < len && !isspace (s[i])) {
        r += s[i++] ;
    }
    return r ;
}

//
// utility to check if a string ends with another string
//
static bool endswith (std::string str, std::string name) {
    int len = str.size() ;
    int namelen = name.size() ;
    if (namelen > len) {
        return false ;
    }
#if defined(_CC_GCC) && __GNUC__ == 2
    return str.compare (name, len - namelen, namelen) == 0 ;
#else
    return str.compare (len - namelen, namelen, name) == 0 ;
#endif
}


//
// get the condition from a command
//
string Debugger::getCondition (string cmd, int &index) {
    string ifword = getString (cmd, index) ;
    if (ifword == "") {
        return "" ;
    }
    return cmd.str.substr (index) ;
}

//
// stop command
//
void Debugger::stop (string cmd) {
    int index = 0 ;
    string condition ;
    string sub = getString (cmd, index) ;
    if (sub == "at") {                          // stop at
        int line = getNumber (cmd, index) ;
        condition = getCondition (cmd, index) ;
        InstructionPtr &inst = currentFile->findLine (line) ;
        Breakpoint *bp = new Breakpoint (inst, cmd, condition) ;
        breakpoints.push_back (bp) ;
    } else if (sub == "in") {                   // stop in
        string name = getString (cmd, index) ;
        condition = getCondition (cmd, index) ;
        typedef std::vector<DBlock*> BlockVec ;
        BlockVec result ;
        BlockVec::iterator i ;
        for (i = blocks.begin() ; i != blocks.end() ; i++) {
            DBlock *block = *i ;
            if (endswith (block->fullname.str, "." + name.str)) {
                result.push_back (block) ;
            }
        }
        DBlock *block = NULL ;
        if (result.size() == 0) {
            std::cerr << "Unknown block " << name << '\n' ;
        } else {
            // ask use for block if there is more than one matching
            if (result.size() > 1) {
                std::cout << "Ambiguous block name, choose one of the following:\n" ;
                std::cout << " 0) cancel\n" ;
                int n = 1 ;
                for (i = result.begin() ; i != result.end() ; i++) {
                    std::cout << ' ' << n++ << ") " << (*i)->fullname.c_str() + 1 << '\n' ;
                }
                std::cout << "> " ;
                std::cout.flush() ;
                std::cin.clear() ;
                std::cin >> n ;
                std::cin.get() ;		// absorb newline
                if (n == 0) {
                    std::cout << "cancelled\n" ;
                    return ;
                }
                if (n > result.size()) {
                    std::cout << "cancelled\n" ;
                    return ;
                }
                block = result[n - 1] ;
            } else {
                block = result[0] ;
            }
            Breakpoint *bp = new Breakpoint (block->code->firstStatement(), cmd, condition) ;
            breakpoints.push_back (bp) ;
        }
    } else if (sub == "throw") {
        string p1 = getString (cmd, index) ;
        if (p1 == "on") {
            stopOnThrow = true ;
        } else if (p1 == "off") {
            stopOnThrow = false ;
        } else {
            throw "Invalid stop throw command, on or off expected" ;
        }

    } else {
        throw "invalid stop command" ;
    }
}

static void indent (std::ostream &os, int i) {
    while (i-- > 0) {
        os << ' ' ;
    }
}

//
// print the contents of a block
//
void Debugger::printBlock (std::ostream &os, Block *blk, Value *base, int ind) {
    indent (os, ind) ;
    os << "Block " << blk->name << " {\n" ;
    if (blk->superblock != NULL) {
        printBlock (os, blk->superblock->block, base, ind + 4) ;
    }
    Scope::VarMap::iterator i ;
    for (i = blk->variables.begin() ; i != blk->variables.end() ; i++) {
        Variable *var = (*i).second ;
        Value &v = base[var->offset] ;
        // don't print these
        if (v.type == T_FUNCTION || v.type == T_CLASS ||
            v.type == T_THREAD || v.type == T_MONITOR ||
            v.type == T_PACKAGE || v.type == T_ENUM || v.type == T_INTERFACE ||
            v.type == T_ENUMCONST) {
            continue ;
        }
        
        // don't print 'this' or internal names
        if (var->name == "this" || var->name[0] == '.') {
           continue ;
        }
  
        // don't print constants
        if (var->flags & VAR_CONST) {
            continue ;
        }
        indent (os, ind) ;
        os << var->name << ": " ;
        switch (v.type) {
        case T_INTEGER:
            os << v.integer ;
            break ;
        case T_BYTE:
            os << std::hex << "0x" << v.integer << std::dec ;
            break ;
        case T_CHAR:
            os << '\'' << (char)v.integer << '\'' ;
            break ;
        case T_REAL:
            os << v.real ;
            break ;
        case T_STRING:
            os << '"' << v.str->str << '"' ;
            break ;
        case T_VECTOR: 
            os << "<vector>" ;
            break ;
        case T_MAP: 
            os << "<map>" ;
            break ;
        case T_STREAM:
            os << "<stream>" ;
            break ;
        case T_OBJECT: 
            if (v.object == NULL) {
                os << "null" ;
                return ;
            }
            os << "<object of type " << v.object->block->name << '>' ;
            break ;
        case T_NONE:
            os << "<none>" ;
            break ;
        default:
            os << "<unknown type>" ;
            break ;
        }
        os << '\n' ;
    }
    indent (os, ind) ;
    os << "}\n" ;
}


// print a string, expanding escape codes
void printstring (std::ostream &os, std::string &s) {
    os << '"' ;
    int len = s.size() ;
    int i = 0 ;
    while (i < len) {
        if (s[i] == '"') {
            os << "\\\"" ;
        } else if (s[i] == '\\') {
            os << "\\\\" ;
        } else if (isprint (s[i])) {
            os << s[i] ;
        } else {
           os << '\\' ;
           switch (s[i]) {
           case '\n': os << 'n' ; break ;
           case '\r': os << 'r' ; break ;
           case '\a': os << 'a' ; break ;
           case '\t': os << 't' ; break ;
           case '\v': os << 'v' ; break ;
           case '\b': os << 'b' ; break ;
           default: 
               os << "x" << std::hex << (int)s[i] << std::dec ;
           }
        }
        i++ ;
    }
    os << '"' ;
}

//
// print a value
//
void Debugger::printValue (std::ostream &os, Value &v) {
    switch (v.type) {
    case T_INTEGER:
        os << v.integer ;
        break ;
    case T_BYTE:
        os << "0x" << std::hex << v.integer << std::dec ;
        break ;
    case T_CHAR:
        os << '\'' << (char)v.integer << '\'' ;
        break ;
    case T_REAL:
        os << v.real ;
        break ;
    case T_STRING:
        printstring (os, v.str->str) ;
        break ;
    case T_VECTOR: {
        for (int i = 0 ; !localinterrupted && i < v.vec->size() ; i++) {
            os << '[' << i << "] " ;
            printValue (os,(*v.vec)[i]) ;
            os << '\n' ;
        }
        break ;
        }
    case T_MAP: {
        map::iterator s = v.m->begin() ;
        map::iterator e = v.m->end() ;
        os << "{\n" ;
        while (!localinterrupted && s != e) {
            map::value_type v = *s ;
            Value v1 = (*s).first ;
            Value &v2 = (*s).second ;
            printValue (os, v1) ;
            os << " = " ;
            printValue (os, v2) ;
            os << '\n' ;
            s++ ;
        }
        os << "}\n" ;
        break ;
        }
    case T_STREAM:
        os << "<stream>" ;
        break ;
    case T_OBJECT: 
        if (v.object == NULL) {
            os << "null" ;
            return ;
        }
        printBlock (os, v.object->block, v.object->varstack, 4) ;
        break ;
    case T_FUNCTION:
    case T_CLASS:
    case T_THREAD:
    case T_MONITOR:
    case T_PACKAGE:
    case T_ENUM:
        os << v.block->name ;
        break ;
    case T_ENUMCONST:
        os << v.ec->name ;
        break ;
    case T_NONE:
        os << "<none>" ;
        break ;
    default:
        os << "<unknown type>" ;
        break ;
    }
}

void Debugger::print (string cmd) {
    int errors = aikido->numErrors ;
    Value v = exec (cmd) ;
    if (aikido->numErrors == errors) {
        std::cout << cmd << " = " ;
        void (*oldint)(int) = signal (SIGINT, localinterrupt) ;
        printValue (std::cout, v) ;
        std::cout << '\n'; 
        localinterrupted = false ;
        signal (SIGINT, oldint) ;
    }
}

//
// evaluate a condition in the current context
//
bool Debugger::evalCondition (string cond, Scope *scope, int level, StackFrame *stack, StaticLink *staticLink) {
#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream stream ;
#else
    std::stringstream stream ;
#endif
    stream << cond ;
    Context ctx (stream, aikido->currentContext) ;
    aikido->currentContext = &ctx ;
    aikido->readLine() ;
    aikido->nextToken() ;
    int errors = aikido->numErrors ;
    noaccesscheck = true ;
    stopped = false ;
    bool result = false ;
    try {
        Value v = aikido->getExpression (stack, staticLink, scope, level) ;
        noaccesscheck = false ;
        stopped = true ;
        if (aikido->numErrors == errors) {
            result = v ;
        }
    } catch (...) {
        noaccesscheck = false ;
        stopped = true ;
    }
    aikido->currentContext = ctx.getPrevious() ;
    return result ;
}

//
// show stack location
//
void Debugger::where (string cmd) {
    StackFrame *f = stackTop.frame ;
    int n = 1 ;
    Instruction *inst = topInstruction ;
    while (f != NULL && inst != NULL && f->block != NULL) {
        if (f == currentStack) {
            std::cout << "=>" ;
        } else {
            std::cout << "  " ;
        }
        std::cout << '[' << n++ << "] " <<
            f->block->name << " at line " << inst->source->lineno << 
                " in file " << inst->source->filename << '\n' ;
        inst = f->inst ;
        f = f->dlink ;
    }
}

//
// stack motion
//
void Debugger::up (string cmd) {
    if (scopeSP == 0) {
        return ;
    }
    if (stackLevel == callStack.size() - 1) {
        std::cerr << "already at top of call stack\n" ;
        return ;
    }
    StackFrame *frame = callStack[stackLevel].frame ;
    StaticLink *slink = callStack[stackLevel].slink ;
    StackFrame *newframe =  callStack[++stackLevel].frame ;
    // move the current scope to the next scope up the stack whose majorScope is
    // the newframe->block
    int sp = scopeSP ;
    while (sp != 0 && scopeStack[sp-1]->majorScope != newframe->block) {
        sp-- ;
    }
    if (sp == 0) {
        std::cerr << "Unable to find enclosing scope" ;
        return ;
    }
    scopeSP = sp ;
    currentScope = scopeStack[sp-1] ;
    currentScopeLevel = newframe->block->level ;
    currentInstruction = frame->inst ;
    currentStaticLink = slink ;
    currentStack = newframe ;
    printLine (currentInstruction->source) ;
}

void Debugger::down (string cmd) {
    if (scopeSP == 0) {
        return ;
    }
    if (stackLevel == 0) {
        std::cerr << "already at bottom of call stack\n" ;
        return ;
    }
    StackFrame *newframe =  callStack[--stackLevel].frame ;		// new frame
    int sp  = scopeSP ;
    if (stackLevel == 0) {
        currentInstruction = topInstruction ;
        currentStaticLink = stackTop.slink ;
        sp = topSP ;
    } else {
        StackFrame *prevFrame = callStack[stackLevel - 1].frame ;
        StaticLink *slink = callStack[stackLevel - 1].slink ;
        currentInstruction = prevFrame->inst ;
        currentStaticLink = slink ;
        // sp is currently the last scope in this block, moving up one
        // makes it the first scope in the next block
        sp++ ;
        // sp now at the first scope in the new frame
        // move the sp to the next scope.  This is the last scope whose
        // majorScope is newframe->block
        while (sp != topSP && scopeStack[sp-1]->majorScope == newframe->block) {
            sp++ ;
        }
        // sp will be either the top of the stack or the first scope in the 
        // frame below the one we want
        if (sp != topSP) {
            sp-- ;
        }
    }
    scopeSP = sp ;
    currentScope = scopeStack[sp-1] ;
    currentScopeLevel = newframe->block->level ;
    currentStack = newframe ;
    printLine (currentInstruction->source) ;
}

void Debugger::readLine (std::istream &is, char *buf, int len) {
    while (!is.eof() && len-- > 0) {
        char c = is.get() ;
        if (c == '\n') {
            *buf = 0 ;
            return ;
        }
        *buf++ = c ;
    }
}


//
// list command
//
void Debugger::list (string cmd) {
    if (currentInstruction == NULL) {
        return ;
    }
    int index = 0 ;
    int start, end ;
    char buf[1024] ;

    int n = getNumber (cmd, index) ;
    if (n != 0) {
        start = n ;
    } else {
        start = currentInstruction->source->lineno ;
    }
    n = getNumber (cmd, index) ;
    if (n == 0) {
        end = start + 10 ;
    } else {
        end = n ;
    }
    string fname = currentInstruction->source->filename ;

    void (*oldint)(int) = signal (SIGINT, localinterrupt) ;
    std::ifstream s (currentInstruction->source->filename) ;
    if (!s) {
        //std::cerr << "can't find file " << currentNode->filename << '\n' ;
        return ;
    }
    n = 0 ;
    while (!localinterrupted && !s.eof() && n < start) {			// skip to start line
        readLine (s, buf, 1024) ;
        n++ ;
    }
    if (!s.eof()) {
        std::cout << n << ": " << buf << '\n' ;
        while (!localinterrupted && !s.eof() && n <= end) {			// print to end line
            readLine (s, buf, 1024) ;
            n++ ;
            if (!s.eof()) {
                std::cout << n << ": " << buf << '\n' ;
            }
        }
    }
    s.close() ;
    localinterrupted = false ;
    signal (SIGINT, oldint) ;
}

void Debugger::file (string cmd) {
    int index = 0 ;
    string filename = getString (cmd, index) ;
    if (filename == "") {
        std::cout << "Current file is " << currentFile->name << "\n" ;
    } else {
        currentFile = findFile (filename) ;
    }
}


void Debugger::printLine (SourceLocation *source) {
    if (source == NULL) {
        return ;
    }
    string file = source->filename ;
    int line = source->lineno ;
    char buf[1024] ;
    int n = 0 ;
    std::ifstream s (file.c_str()) ;
    if (!s) {
        //std::cerr << "can't find file " << file << '\n' ;
        return ;
    }
    while (!s.eof() && n < line) {
        readLine (s, buf, 1024) ;
        n++ ;
    }
    if (!s.eof()) {
        std::cout << line << ": " << buf << '\n' ;
    }
    s.close() ;
}


void Debugger::listFiles (string cmd) {
    FileMap::iterator i ;
    for (i = files.begin() ; i != files.end() ; i++) {
        std::cout << (*i).first.str << '\n' ;
    }
    
}

void Debugger::help (string cmd) {
    std::cout << "Aikido debugger\n" ;
    std::cout << "Copyright (C) 2003 Sun Microsystems, Inc.  All rights reserved.\n" <<
                 "U.S. Government Rights - Commercial software.\n" <<
                 "Government users are subject to the Sun Microsystems, Inc. standard license agreement and applicable\n" <<
                 "provisions of the FAR and its supplements.  Use is subject to license terms.  Sun,  Sun Microsystems,  the\n" <<
                 "Sun logo and  Java are trademarks or registered trademarks of Sun Microsystems, Inc. in the U.S. and other\n" <<
                 "countries.\n\n" ;
    std::cout << "	quit (or exit)			exit the debugger\n" ;
    std::cout << "	history				show command history (use with history substitution)\n" ;
    std::cout << "	alias				list all aliases\n" ;
    std::cout << "	alias name			print the definition of the named alias\n" ;
    std::cout << "	alias name defn			define the named alias\n" ;
    std::cout << "	unalias name 			delete the named alias\n" ;
    std::cout << "	stop in <name> [if <expr>]	stop in named block\n" ;
    std::cout << "	stop at <line> [if <expr>]	stop at given line\n" ;
    std::cout << "	stop throw on|off		switch on or off break on exception handling\n" ;
    std::cout << "	cont				continue execution\n" ;
    std::cout << "	run				run the program\n" ;
    std::cout << "	clear				clear all breakpoints\n" ;
    std::cout << "	clear <bpnum>			clear the given breakpoint\n" ;
    std::cout << "	step				single step one line\n" ;
    std::cout << "	step up				continue until return\n" ;
    std::cout << "	nexti				single step one instruction (over calls)\n" ;
    std::cout << "	stepi				single step one instruction\n" ;
    std::cout << "	next				single step one line (over calls)\n" ;
    std::cout << "	status				show debugger status\n" ;
    std::cout << "	print <expr>			print value of expression\n" ;
    std::cout << "	set <expr>			execute expression\n" ;
    std::cout << "	call <expr>			call expression\n" ;
    std::cout << "	up				move up stack one frame\n" ;
    std::cout << "	down				move down one stack frame\n" ;
    std::cout << "	where				show stack trace\n" ;
    std::cout << "	list [s] [e]			list lines from s to e\n" ;
    std::cout << "	dis [n]				print the next n (10) instructions\n" ;
    std::cout << "	file <name>			select current file\n" ;
    std::cout << "	files				list all available files\n" ;
    std::cout << "	threads				list all threads\n" ;
    std::cout << "	thread [n]			select thread n (or show current thread)\n" ;
    std::cout << "	disable <bpnum>			disable breakpoint\n" ;
    std::cout << "	enable <bpnum>			enable breakpoint\n" ;
    std::cout << "	show <what>			show debugger things:\n" ;
    std::cout << "		blocks			show all non-system blocks\n" ;
    std::cout << "		allblocks		show all blocks\n" ;
    std::cout << "		breaks			show all breakpoints\n" ;
    std::cout << "		files			show all available files\n" ;
    std::cout << "		stack			show stack trace\n" ;
    std::cout << "		threads			show all threads\n" ;
    std::cout << "		vars			show all variables\n" ;
}

void Debugger::controlBreakpoint (bool enable, string cmd) {
    int index = 0 ;
    int bpnum = getNumber (cmd, index) ;
    if (bpnum == 0) {
        for (int i = 0 ; i < breakpoints.size() ; i++) {
            if (enable) {
                breakpoints[i]->enable() ;
            } else {
                breakpoints[i]->disable() ;
            }
        }
    } else {
        if (bpnum > breakpoints.size()) {
            std::cerr << "No such breakpoint\n" ;
        } else {
            if (enable) {
                breakpoints[bpnum - 1]->enable() ;
            } else {
                breakpoints[bpnum - 1]->disable() ;
            }
        }
    }
}


void Debugger::status (string cmd) {
    for (int i = 0 ; i < breakpoints.size() ; i++) {
        Breakpoint *bp = breakpoints[i] ;
        if (bp != NULL) {
            if (bp->disabled) {
                std::cout << '[' << i + 1 << "]" ;
            } else {
                std::cout << '(' << i + 1 << ")" ;
            }
            std::cout << " stop " << bp->cmd << " in file " << bp->inst.getInst()->source->filename << " at line " << bp->inst.getInst()->source->lineno ;
            if (bp->condition != "") {
                std::cout << " if " << bp->condition ;
            }
            std::cout << '\n' ;
        }
    }
    std::cout << "Exception breakpoint handling is " << (stopOnThrow?"on":"off") << "\n" ;
}

void Debugger::clear (string cmd) {
    int index = 0 ;
    int bpnum = getNumber (cmd, index) ;
    if (bpnum == 0) {
        for (int i = 0 ; i < breakpoints.size() ; i++) {
            delete breakpoints[i] ;
            breakpoints[i] = NULL ;
        }
    } else {
        if (bpnum > breakpoints.size()) {
            std::cerr << "No such breakpoint\n" ;
        } else {
            delete breakpoints[bpnum - 1] ;
            breakpoints[bpnum - 1] = NULL ;
        }
    }
}

static const char *blocktype (DBlock *blk) {
    switch (blk->block->type) {
    case T_THREAD:
	return "thread" ;
    case T_FUNCTION:
	return "function" ;
    case T_CLASS:
	return "class" ;
    case T_MONITOR:
	return "monitor" ;
    case T_PACKAGE:
	return "package" ;
    case T_INTERFACE:
	return "interface" ;
    }
    return "block" ;

}


// show all the variables in the scope at the given stack offset (sp)
void Debugger::showvars (int sp, StackFrame *f) {
    Scope::VarMap::iterator i ;
    Scope *scope = scopeStack[sp-1] ;
    Value *base = f->varstack ;
    for (i = scope->variables.begin() ; i != scope->variables.end() ; i++) {
        Variable *var = i->second ;
        if (var->name[0] == '.') {
            continue ;
        }
        Value &v = base[var->offset] ;
        if (v.type == T_FUNCTION || v.type == T_CLASS ||
            v.type == T_THREAD || v.type == T_MONITOR ||
            v.type == T_PACKAGE || v.type == T_INTERFACE ||
            v.type == T_ENUMCONST) {
            continue ;
        }
        if (var->name == "this" || var->name[0] == '.') {
           continue ;
        }
  
        if (var->flags & VAR_CONST) {
            continue ;
        }
        std::cout << scope->majorScope->fullname << '.' << var->name << " = " ;
        printValue (std::cout, v) ;
        std::cout << "\n" ;
    }
}

void Debugger::showvars (string cmd) {
    int sp = scopeSP ;
    if (sp == 0) {
        return ;
    }
    StackFrame *f = currentStack ;
    Block *majorScope = scopeStack[sp-1]->majorScope ;
    while (sp > 0) {
        if (scopeStack[sp-1]->majorScope != majorScope) {       // moved to a new block?
            f = f->dlink ;                                      // yes, move stack frame
        }
        if (f == NULL) {
            break ;
        }
        showvars (sp--, f) ;
        majorScope = scopeStack[sp]->majorScope ;
    }
}

void Debugger::show (string cmd) {
    if (cmd == "") {
       status (cmd) ;
       return ;
    }
    string what ;
    int index = 0 ;
    while (index < cmd.size()) {
        what = getString (cmd, index) ;
        if (what == "blocks") {
            for (int i = 0 ; i < blocks.size() ; i++) {
                DBlock *block = blocks[i] ;
                if (!block->systemblock) {
                    std::cout << blocktype (block) << " " << block->fullname.c_str() + 1 << " in file " << block->filename << " at line " << block->lineno << '\n' ;
                }
            }
        } else if (what == "allblocks") {
            for (int i = 0 ; i < blocks.size() ; i++) {
                DBlock *block = blocks[i] ;
                std::cout << blocktype (block) << " " << block->fullname.c_str() + 1 << " in file " << block->filename << " at line " << block->lineno << '\n' ;
            }
        } else if (what == "breaks") {
            status(cmd) ;
        } else if (what == "files") {
            listFiles(cmd) ;
        } else if (what == "stack") {
            where(cmd) ;
        } else if (what == "threads") {
            listThreads(cmd) ;
        } else if (what == "vars") {
            showvars(cmd) ;
        }
    }
}

Value Debugger::exec (string cmd) {
#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream stream ;
#else
    std::stringstream stream ;
#endif
    stream << cmd ;
    Context ctx (stream, aikido->currentContext) ;
    aikido->currentContext = &ctx ;
    aikido->readLine() ;
    stream.clear() ;
    aikido->nextToken() ;
    Value v ;
    noaccesscheck = true ;
    stopped = false ;
    try {
        v = aikido->getExpression (currentStack, currentStaticLink, currentScope, currentScopeLevel) ;
    } catch (Exception e) {
        v = 0 ;
        aikido->reportException (e) ;
    } catch (...) {
        std::cerr << "unexpected exception\n" ;
    }
    noaccesscheck = false ;
    stopped = true ;
    aikido->currentContext = ctx.getPrevious() ;
    return v ;
}

void Debugger::listThreads (string cmd) {
    std::list<VirtualMachine*>::iterator i ;
    int n = 0 ;
    for (i = threads.begin() ; i != threads.end() ; i++) {
        VirtualMachine *vm = *i ;
        if (vm == currentVM) {
            std::cout << "* " ;
        } else {
            std::cout << "  " ;
        }
        std::cout << n++ << "\t" ;
        vm->showPosition (std::cout) ;
        std::cout << "\n" ;
    }
}

void Debugger::switchThread (string cmd) {
    if (cmd == "") {			// print current thread?
        std::cout << "Current thread: " ;
        currentVM->showPosition (std::cout) ;
        std::cout << "\n" ;
        return ;
    }
    int index = 0 ;
    int n = getNumber (cmd, index) ;
    if (n < 0 || n > threads.size()) {
        std::cerr << "No such thread\n" ;
        return ;
    }
    std::list<VirtualMachine*>::iterator i = threads.begin() ;
    while (n > 0) {
        i++ ;
        n-- ;
    }

    if (currentVM == *i) {
        return ;
    }
    currentVM->debugState = RUN ;	// cause old thread to continue on return from 'run'

    currentVM = *i ;
    currentVM->getPosition (currentScope, currentScopeLevel, currentStack, currentStaticLink, currentInstruction) ;
    stackTop.frame = currentStack ;
    stackTop.slink = currentStaticLink ;
    stackLevel = 0 ;
    topInstruction = currentInstruction ;

    callStack.clear() ;

    StackFrame *stk = currentStack ;
    StaticLink *slink = currentStaticLink ;
    while (stk != NULL) {
        if (slink->frame != stk) {
            slink = slink->next ;
        }
        callStack.push_back (StackContext (stk, slink)) ;
        stk = stk->dlink ;
    }

    
    if (currentInstruction != NULL) {
        printLine (currentInstruction->source) ;
    }

}

void Debugger::showHistory() {
    for (int i = 0 ; i < history.size() ; i++) {
        std::cout << i << "\t" << history[i] << "\n" ;
    }
}

std::string Debugger::replaceHistory (const std::string &command) {
    int i = 0 ;
    std::string newcommand ;
    for (int i = 0 ; i < command.size() ; i++) {
        if (command[i] == '\\') {
            newcommand += command[i] ;
            if (i != (command.size() - 1)) {
                newcommand += command[i+1] ;
                i++ ;
            }
            continue ;
        }
        if (command[i] == '!') {
            if (i == (command.size() - 1)) {
                throw "Bad history substitution" ;
            }
            i++ ;
            if (command[i] == '!') {
                if (history.size() == 0) {
                    continue ;
                }
                newcommand += history[history.size() - 1] ;
            } else if (isdigit (command[i])) {
                int n = 0 ;
                while (i < command.size() && isdigit (command[i])) {
                    n = (n * 10) + command[i] - '0' ;
                    i++ ;
                }
                i-- ;
                if (n >= history.size()) {
                    throw "Bad history substitution" ;
                }
                newcommand += history[n] ;
            } else {
                std::string buf ;
                if (command[i] == '{') {
                    i++ ;
                    while (command[i] != '}') {
                        buf += command[i] ;
                        i++ ;
                    }
                } else {
                    while (i < command.size() && !isspace (command[i])) {
                        buf += command[i];
                        i++ ;
                    }
                    i-- ;
                }
                bool found = false ;
                for (int j = history.size() - 1 ; j >= 0 ; j--) {
                    if (strncmp (buf.c_str(), history[j].c_str(), buf.size()) == 0) {
                        newcommand += history[j] ;
                        found = true ;
                        break ;
                    }
                }
                if (!found) {
                    throw "Bad history substitution" ;
                }
            }

        } else {
            newcommand += command[i] ;
        }
    }
    return newcommand ;
}

// reset the state back to the start of the run
void Debugger::rerun() {
    threadlock.lock() ;
    threads.clear() ;
    threadlock.unlock() ;
}


// run the debugger by reading a set of commands.  Called inline before
// code is executed, so this returns when the code is to continue

// CAUTION: writes directly to currentVM->debugState so that we can switch threads

void Debugger::run(VirtualMachine *vm, Instruction *inst, Scope **sStack, int sSP, StackFrame *stack, StaticLink *staticLink) {

    // only one thread can be at the prompt at a single time.  If a thread
    // is already got a prompt (stopped = true) and another one calls run
    // this the second thread must behave as if it called blockThread instead

    threadlock.lock() ;
    if (stopped) {			// someone already here?
        threadlock.unlock() ;		// yes, unlock
        blockThread() ;			// wait for continue
        return ;			// return 
    } else {
        stopped = true ;		// we are the first here, acquire the run function
        threadlock.unlock() ;		// unlock
    }

    currentVM = vm ;
    scopeStack = sStack ;
    scopeSP = sSP ;
    if (scopeSP > 0) {          // first time in, scopeSP == 0
        topSP = scopeSP ;
        currentScope = scopeStack[scopeSP-1]   ;
        currentScopeLevel = currentScope->majorScope->level ;
    } else {
#ifdef _OS_WINDOWS
        std::string rcfile = "aikido.rc" ;
#else
        std::string home = getenv ("HOME") ;
        std::string rcfile = home + "/.aikidorc" ;
#endif
        std::ifstream rc (rcfile.c_str()) ;
        if (rc) {
            while (!rc.eof()) {
                std::string command  ;
                getline (rc, command) ;
                if (command == "") {
                    continue ;
                }
                command.erase (0, command.find_first_not_of (" \t")) ;
                if (command == "" || command[0] == '#') {
                    continue ;
                }
                std::string::size_type index = command.find_first_of (" \t") ;
                if (index == std::string::npos) {
                    index = command.size() ;
                }
                std::string firstword = command.substr (0, index) ;
                std::string tail = command.substr (index, command.size()) ;
                tail.erase (0, tail.find_first_not_of (" \t")) ;

                CommandMap::iterator c ;
                Commands cmd = CMD_NONE ;
                c = commands.find (firstword) ;
                if (c == commands.end()) {
                    continue ;
                } else {
                    cmd = (Commands)(*c).second ;
                }
                switch (cmd) {
                case CMD_ALIAS:
                    alias (tail) ;
                    break ;
                case CMD_UNALIAS:
                    unalias (tail) ;
                    break ;
                    }
                }
            }
        }
    currentInstruction = inst ;
    currentStack = stack ;
    currentStaticLink = staticLink ;
    stackTop.frame = stack ;
    stackTop.slink = staticLink ;
    stackLevel = 0 ;
    topInstruction = currentInstruction ;

    callStack.clear() ;
    StackFrame *stk = stack ;
    StaticLink *slink = staticLink ;
    while (stk != NULL) {
        if (slink->frame != stk) {
            slink = slink->next ;
        }
        callStack.push_back (StackContext (stk, slink)) ;
        stk = stk->dlink ;
    }

    if (inst != NULL) {
        printLine (inst->source) ;
    }
    bool running = true ;
    DebugState result ;

    signal (SIGINT, SIG_IGN) ;
    while (running && !std::cin.eof()) {
        std::string command ;
        std::cout << "aikido> " ;
        std::cout.flush() ;
        std::cin.clear() ;
        getline (std::cin, command) ;
    aliasdone:
        if (command == "") {
            continue ;
        }
        command.erase (0, command.find_first_not_of (" \t")) ;
        if (command == "") {
            continue ;
        }
        try {
            // replace the history in the command
            command = replaceHistory (command) ;
        } catch (const char *s) {
            std::cerr << s << '\n' ;
            continue ;
#ifndef _OS_WINDOWS
        } catch (char *s) {
            std::cerr << s << '\n' ;
            continue ;
#endif
        } catch (...) {
        }
        if (command == "") {
            continue ;
        }
        std::string::size_type index = command.find_first_of (" \t") ;
        if (index == std::string::npos) {
            index = command.size() ;
        }
        std::string firstword = command.substr (0, index) ;
        std::string tail = command.substr (index, command.size()) ;
        tail.erase (0, tail.find_first_not_of (" \t")) ;

        AliasMap::iterator al = aliases.find (firstword) ;
        if (al != aliases.end()) {
            command = (*al).second.str ;
            goto aliasdone ;
        }
        CommandMap::iterator c ;
        Commands cmd = CMD_NONE ;
        c = commands.find (firstword) ;
        if (c == commands.end()) {
            //std::cerr << "Unknown command: " << firstword << '\n' ;
            ::system (command.c_str()) ;
            continue ;
        } else {
            cmd = (Commands)(*c).second ;
        }
        history.push_back (command) ;
        try {
            switch (cmd) {
            case CMD_QUIT:
                exit (0) ;
                break ;
            case CMD_STOP:
                stop (tail) ;
                break ;
            case CMD_CONT:
                result =  RUN ;
                goto cont ;

            case CMD_RUN:
                if (scopeSP > 0) {
                    result = RERUN ;
                } else {
                    result =  RUN ;
                }
                goto cont ;
                break ;
            case CMD_CLEAR:
                clear (tail) ;
                break ;
            case CMD_STEP:
                if (tail == "up") {
                    result = STEPUP ;
                } else {
                    result = STEP ;
                }
                goto cont ;
                break ;
            case CMD_NEXT:
                result = NEXT ;
                goto cont ;
                break ;
            case CMD_STEPI:
                result = STEPI ;
                goto cont ;
                break ;
            case CMD_NEXTI:
                result =  NEXTI;
                goto cont ;
                break ;
            case CMD_PRINT:
                print (tail) ;
                break ;
            case CMD_STATUS:
                status (tail) ;
                break ;
            case CMD_HELP:
                help (tail) ;
                break ;
            case CMD_UP:
                up (tail) ;
                break ;
            case CMD_DOWN:
                down (tail) ;
                break ;
            case CMD_WHERE:
                where (tail) ;
                break ;
            case CMD_LIST:
                list (tail) ;
                break ;
            case CMD_FILE:
                file (tail) ;
                break ;
            case CMD_SHOW:
                show (tail) ;
                break ;
            case CMD_FILES:
                listFiles (tail) ;
                break ;
            case CMD_SET:
            case CMD_CALL:
                exec (tail) ;
                break ;
            case CMD_ALIAS:
                alias (tail) ;
                break ;
            case CMD_UNALIAS:
                unalias (tail) ;
                break ;
            case CMD_DIS:
                dis (tail) ;
                break ;
            case CMD_THREADS:
                listThreads (tail) ;
                break ;
            case CMD_THREAD:
                switchThread (tail) ;
                break ;
            case CMD_ENABLE:
                controlBreakpoint (true, tail) ;
                break ;
            case CMD_DISABLE:
                controlBreakpoint (false, tail) ;
                break ;
            case CMD_HISTORY:
                showHistory() ;
                break ;
            }
        } catch (const char *s) {
            std::cerr << s << '\n' ;
#ifndef _OS_WINDOWS
        } catch (char *s) {
            std::cerr << s << '\n' ;
#endif
        } catch (Exception e) {
        }
    }
cont:
    stopped = false ;
    unblockThreads(result == RERUN ? RERUN : RUN) ;
    currentVM->debugState = result ;		// write to debugState
    if (result == RERUN) {
        rerun() ;
    }
}


void Debugger::alias (string cmd) {
    int index = 0 ;
    string aliasname = getString (cmd, index) ;
    if (aliasname == "") {
        AliasMap::iterator i ;
        for (i = aliases.begin() ; i != aliases.end() ; i++) {
            std::cout << (*i).first.str << "\t" << (*i).second.str << '\n' ;
        }
    } else {
        string value = cmd.str.substr (index) ;			// rest of line
        if (value == "") {
             AliasMap::iterator i = aliases.find (aliasname) ;
             if (i != aliases.end()) {
                 std::cout << (*i).second << "\n" ;
             }
        } else {
             AliasMap::iterator i = aliases.find (aliasname) ;
             if (i != aliases.end()) {
                 aliases.erase (i) ;
             }
             aliases[aliasname] = value ;
        }
    }
}

void Debugger::unalias (string cmd) {
    int index = 0 ;
    string aliasname = getString (cmd, index) ;
    if (aliasname != "") {
        AliasMap::iterator i ;
        for (i = aliases.begin() ; i != aliases.end() ; i++) {
            if (i->first == aliasname) {
                aliases.erase (i) ;
                return ;
            }
        }
        std::cerr << "No such alias " << aliasname << "\n" ;
    }
}

void Debugger::dis (string cmd) {
    if (currentInstruction == NULL) {
        return;
    }
    int index = 0 ;
    int n = getNumber (cmd, index) ;
    if (n == 0) {
        n = 10 ;
    }
    Instruction *inst = currentInstruction ;
    for (int i = 0 ; i < n ; i++) {
        inst->print (std::cout) ;
        if (inst->opcode == opEND) {
            break ;
        }
        inst++ ;
    }
}

void Debugger::breakpointHit (VirtualMachine *vm, Instruction *inst, Scope **sStack, int sSP, StackFrame *stack, StaticLink *staticLink, int &op) {
    Breakpoint *bp = findBreakpoint (inst) ;
    op = bp->opcode ;
    if (bp->condition != "") {
        if (!evalCondition (bp->condition, sStack[sSP-1], sStack[sSP-1]->majorScope->level, stack, staticLink)) {
            return ;
        }
    }
    std::cout << "stopped at breakpoint set in " << inst->source->filename << " at line " << inst->source->lineno << '\n' ;
    run (vm, inst, sStack, sSP, stack, staticLink) ;
}


int Debugger::findBreakpointOp (Instruction *inst) {
    Breakpoint *bp = findBreakpoint (inst) ;
    return bp->opcode ;
}

Breakpoint *Debugger::findBreakpoint (Instruction *inst) {
    for (int i = 0 ; i < breakpoints.size() ; i++) {
        if (breakpoints[i] != NULL && breakpoints[i]->inst.getInst() == inst) {
            return breakpoints[i] ;
        }
    }
    throw "No such breakpoint" ;
}

void Debugger::registerFile (string filename) {
    if (!debugging) {
        return ;
    }
    File *file = new File (filename) ;
    files[filename] = file ;
    currentFile = file ;
}

void *Debugger::pushFile (string filename) {
    if (!debugging) {
        return NULL ;
    }
    void *ctx = (void*)currentFile ;
    registerFile (filename) ;
    return ctx ;
}

void Debugger::popFile (void *ctx) {
    if (!debugging) {
        return ;
    }
    currentFile = (File*)ctx ;
}

void Debugger::registerInstruction (CodeSequence *c, int i) {
    if (!debugging) {
        return ;
    }
    if (currentFile == NULL) {
        throw "internal error: don't know the current file" ;
    }
    currentFile->addLine (InstructionPtr (c, i)) ;
}

void Debugger::setFile (Block *blk) {
    DBlock *db = NULL ;

    for (int i = 0 ; i < blocks.size() ; i++) {
        if (blocks[i]->block == blk) {
            db = blocks[i] ;
            break ;
        }
    }
    if (db == NULL) {
        throw "No such block" ;
    }
    // now find the file in which the block resides
    currentFile = findFile (db->filename) ;
}


void Debugger::registerBlock (Block *blk) {
    if (!debugging) {
        return ;
    }
    if (blk->code == NULL) {
       return ;
    }
    registerBlock (blk, blk->code->firstStatement().getInst()->source, blk->code) ;
}

void Debugger::registerPackageBlock (Block *blk, const string &filename, int lineno) {
    if (!debugging) {
        return ;
    }
    DBlock *block = new DBlock (blk, filename, lineno, readingsystem) ;
    blocks.push_back (block) ;
}

void Debugger::registerBlock (Block *blk, SourceLocation *source, CodeSequence *c) {
    if (!debugging) {
        return ;
    }
    DBlock *block = new DBlock (blk, source, c, readingsystem) ;
    blocks.push_back (block) ;
}


// look up a block given its name.  The name is matched as a partial match
// in the block

DBlock *Debugger::findBlock (string name) {
    return NULL ;
}

static bool filesmatch (std::string name1, std::string name2) {
    // first remove the .aikido from the names
    std::string::size_type i = name1.find (".aikido") ;
    if (i != std::string::npos) {
        name1.erase (i, 8) ;
    }
    i = name2.find (".aikido") ;
    if (i != std::string::npos) {
        name2.erase (i, 8) ;
    }

    // now trim the filename component of the names

    i = name1.rfind ("/") ;
    if (i != std::string::npos) {
        name1.erase (0, i) ;
    }

    i = name2.rfind ("/") ;
    if (i != std::string::npos) {
        name2.erase (0, i) ;
    }

    // remove the zip:
    if (name1.size() > 4 && name1.substr(0,4) == "zip:") {
        name1 = name1.substr (4) ;
    }
    if (name2.size() > 4 && name2.substr(0,4) == "zip:") {
        name2 = name2.substr (4) ;
    }
    return name1 == name2 ;
}

File *Debugger::findFile (string name) {
    FileMap::iterator result = files.find (name) ;		// first try a fast search
    if (result == files.end()) {
        // now do it slowly
        for (result = files.begin() ; result != files.end() ; result++) {
            if (filesmatch (name.str, (*result).first.str)) {
                return (*result).second ;
            }
        }
        std::cerr << "No such file: " << name << "\n" ;
        File *f = new File (name) ;
        files[name] = f ;
        return f ;
    }
    return (*result).second ;
}


void Debugger::stopInMain() {
    stop ("in main") ;
}

// reloate all references to 

void Debugger::relocate (Block *blk, CodeSequence *code, int start, int end, int offset) {
    DBlock *db = NULL ;
    File *file = NULL ;

    for (int i = 0 ; i < blocks.size() ; i++) {
        if (blocks[i]->block == blk) {
            db = blocks[i] ;
            break ;
        }
    }
    if (db == NULL) {
        throw "No such block" ;
    }
    // now find the file in which the block resides
    file = findFile (db->filename) ;

    // now pass through all the lines in the line map and relocate them
    File::LineMap::iterator i = file->lines.begin() ;
    InstructionPtr st (code, start) ;
    InstructionPtr e (code, end) ;
    while (i != file->lines.end()) {
        InstructionPtr &inst = i->second ;
        if (inst >= st & inst <= e) {
            inst.relocate (offset) ;
        }
        i++ ;
    }
}


//
// thread management
//

void Debugger::registerThread (VirtualMachine *v) {
    threadlock.lock() ;
    threads.push_back (v) ;
    threadlock.unlock() ;
}

void Debugger::unregisterThread (VirtualMachine *v) {
    threadlock.lock() ;
    std::list<VirtualMachine*>::iterator i = threads.begin() ;
    while (i != threads.end()) {
        if (*i == v) {
            threads.erase (i) ;
            break ;
        }
        i++ ;
    }
    threadlock.unlock() ;
}

void Debugger::blockThread() {
    threadlock.lock() ;
    blockedthreads++ ;
    threadlock.unlock() ;
    threadsem.wait() ;
}

void Debugger::unblockThreads(DebugState result) {
    threadlock.lock() ;
    std::list<VirtualMachine*>::iterator i = threads.begin() ;
    while (i != threads.end()) {
        (*i)->debugState = result ;
        i++ ;
    }
    threadsem.post (blockedthreads) ;
    blockedthreads = 0 ;
    threadlock.unlock() ;
    
}



		
}		// namespace aikido

