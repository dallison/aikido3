/*
 * aikidocodegen.h
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
 * Version:  1.16
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */



/*
The Intermediate Form instructions use the same opcodes as the real instructions.


Registers:

There is an unlimited number of registers available.  Each register has a number and
holds a single value


Operands:

An operand is one of:

    1. a constant value
    2. a register (a value or an address of a value)
    3. a variable (Variable pointer + number of levels up stack)


Instructions:

All instructions have a destination operand.  The following instructions are
defined.  There needs to be an instruction for every overloadable operator.  I need
to be able to search an object at runtime for the operator the programmer actually
used.  This means I can't use the same instruction for 2 different operations.

	// moves

	MOV dest, src			// move operand to operand
	MOVC dest, src			// move operand to constant operand
	MOVF dest, src			// forced move to operand
        MOVO dest, src			// override 

	LD dest, src			// load indirect via register
	ST dest, src, value		// store indirect via register
	COPY src			// make a copy
	TEMP				// temporary register

        // literals
        MKVECTOR dest, nelems		// elements on stack
	MKMAP dest, nelems		// elements on stack in pairs

	// arithmetics

	ADD dest, src1, src2
	SUB dest, src1, src2
	MUL dest, src1, src2
	DIV dest, src1, src2
	MOD dest, src1, src2
	SLL dest, src1, src2
	SRL dest, src1, src2
	SRA dest, src1, src2

 	OR dest, src1, src2		// bit or
 	XOR dest, src1, src2		// bit xor
 	AND dest, src1, src2		// bit and
 	COMP dest, src			// ~
	NOT dest, src			// !
	UMINUS dest, src		// unary minus

	SIZEOF dest, src
	TYPEOF dest, src
	CAST dest, src1, src2 [,pnum]		// src2 cast to type of src1 (optional para num)

 	MUX dest, cond, src1, src2	// conditional
	
    	// comparisons
	CMPEQ dest, src1, src2		// equal
	CMPNE dest, src1, src2		// not equal
	CMPLT dest, src1, src2		// less 
	CMPLE dest, src1, src2		// less than or equal
	CMPGT dest, src1, src2		// greater than
	CMPGE dest, src1, src2		// greater or equal

        IN dest, src1, src2		// src1 in src2

  	// control
	B address			// address is constant pc address
	BT src, address			// branch if true.  src is boolean integer
	BF src, address			// branch if false
	CALL dest, nargs, block		// run a piece of code and save return address
	RET				// return to saved return address
	RETVAL src			// return to saved return address with a value
    YIELD src           // yeild coroutine and return a value, saving state
	SUPERCALL block			// call superblock constructor

  	TRY catchlabel, endlabel	// try block
	CATCH var
	THROW src			// throw an exception

	// misc

        NOP
	NEW dest, nargs, block
	NEWVECTOR dest, ndims, ctstart, ctend

	DELETE src
	STREAM dest, src1, src2		// src2 is an address of a value

        MACRO macro, endlabel		// macro instance
        ENUM block			// enum instance


	// address finding
	FINDV dest, src1, src2		// find member variable value
	PUSHV src1, src2		// find member variable value and push the value`
	STV dest, src1, src2, value	// store value in member variable
	FINDA dest, src1, src2		// find member variable address
	ADDR dest, src			// address of value
	
	// subscripting
        ADDRSUB src, base, s1 [, s2]
	SETSUB src, base, s1 [,s2]
	GETSUB dest, base, s1 [, s2]
        DELSUB base, s2 [, s2]		// delete subscript

	GETOBJ dest, src		// get instance of package
        PACKAGEASSIGN dest, src

 	GETTHIS dest, src		// get a "this" pointer from an expression

	PUSHSCOPE scope
	POPSCOPE ns, nt, nf		// pop numscopes, numtrys and numforeach 

	FOREACH var, src, endlabel [, endsrc]
	NEXT 

	// argument passing
        PUSHADDR src		// push address of src onto stack
	PUSH src		// push onto stack
	POP n			// pop n elements off stack


 	FOREIGN src		// foreign command sequence
        INLINE dest, endaddr 	// inlined block (followed immediately by code)

	// Java specific instructions (experimental)
	CALLJAVA dest, args, noverloads
   	CALLJNI dest, nargs, methodptr
	INSTANCEOF dest, object, class
	CHECKCAST dest, class, object
	REALCOMPARE dest, v1, v2, nanflag
	TABLESWITCH value, low, high		// followed by default and (high - low + 1) B instructions
	LOOKUPSWITCH value, npairs		// followed by default (B instruction) and pairs
	RTS					// return from java jsr (addr is pushed)
	JMPT src, label, trylevels		// jump if true and pop try levels
	MONITORENTER src
	MONITOREXIT src

FIXED TYPE INSTRUCTIONS

.type is:

.I 	int
.R	real
.S	string
.O	object
.C	char

	MOV.<type> dest, src			// move operand to operand
	MOVC.<type> dest, src			// move operand to constant operand
	MOVF.<type> dest, src			// forced move to operand

	ADD.<type> dest, src1, src2
	SUB.<type> dest, src1, src2
	MUL.<type> dest, src1, src2
	DIV.<type> dest, src1, src2
	MOD.<type> dest, src1, src2
	SLL.<type> dest, src1, src2
	SRL.<type> dest, src1, src2
	SRA.<type> dest, src1, src2

 	OR.<type> dest, src1, src2		// bit or
 	XOR.<type> dest, src1, src2		// bit xor
 	AND.<type> dest, src1, src2		// bit and
 	COMP.<type> dest, src			// ~
	NOT.<type> dest, src			// !
	UMINUS.<type> dest, src		// unary minus

	RETVAL.<type> src			// return to saved return address with a value

	CMPEQ.<type> dest, src1, src2		// equal
	CMPNE.<type> dest, src1, src2		// not equal
	CMPLT.<type> dest, src1, src2		// less 
	CMPLE.<type> dest, src1, src2		// less than or equal
	CMPGT.<type> dest, src1, src2		// greater than
	CMPGE.<type> dest, src1, src2		// greater or equal

	SETSUB.V src, base, s1 [,s2]
	GETSUB.V dest, base, s1 [, s2]
	



NOTES:

assignments are problematic.  We need to get the address of the left hand side in order
to store something in it.  For a regular variable assignment, the left is just a variable
operand.  For a calculated destination we need to get something in a register.  We can assign
to one of the following:

1. a plain variable up several levels in the static chain
2. a subscript
3. a variable obtained from a FIND on an object (. operator)

subscript:
since indices are dynamic and non constant we have to get the address of the thing being
subscripted and the index.


Calls:

The arguments for a call are pushed from right to left onto the "stack".  The call
mechanism takes the arguments off the stack and inserts them into the variables
or native parameter list of the called function.  It is the responsibility of the
caller to push default parameter values onto the stack.  The call mechanism
deals with the varargs case (insertion of additional args into the args vector).

When the callee is invoked the arguments will have the values passed from
the caller.

The call instruction pops all the arguments off the stack

The CALL instruction takes 3 operands: the destination for the result of the call; 
the number of arguments; and the value to call.

If the block being called is a member function then the first parameter on the
stack must be the "this" pointer.  Have to find a way to get the value of this
from the parse tree.

example:

x = func (1,2,3)
	
	PUSH 3
	PUSH 2
	PUSH 1
	CALL x, 3, func


The NEW instruction is similar to call.  The arguments are pushed onto the
stack in reverse order and the call is made.  The object returned is placed
in the destination location.

var x = new Tree (1,2)

	PUSH 2
	PUSH 1
	NEW x, 2, Tree

SUPERCALL:
The SUPERCALL instruction calls the constructor for the superblock of an
object during the construction of the object.  Like a regular call, the 
arguments are pushed onto the stack and popped by the instruction.


NEWVECTOR:
This allows a vector to be created, it is pretty complex.  There can be any
number of dimensions to the vector and each dimension will have a specified
size.  The last dimension can specify an object to be constructed.

The NEWVECTOR instruction has the following format:

NEWVECTOR dest, ndims, ctstart, ctend

dest: destination
ndims: number of dimensions of vector
ctstart: label specifying start of constructor code 
ctend: label specifying end of constructor code


The sizes of the dimensions are pushed onto the stack starting with the last
dimension.  For example

new [100][10][5]

	PUSH 5
	PUSH 10
	PUSH 100
	NEWVECTOR dest, 3, 0, 0

new Class (1,2) [10][5]

	PUSH 5
	PUSH 10
	NEWVECTOR dest, 2, l1, l2
l1:	
	PUSH 2
	PUSH 1
	NEW dest, 2, Class
	END
l2:

The sizes of the dimensions are popped by the NEWVECTOR instruction


CALLJAVA

This is used to make a call to a set of java methods based on the supplied
arguments.  The instruction takes 4 operands:

1. destination
2. value of 'args' vector
3. number of overloads on stack

The overloads are pushed onto the stack after the arguments and are not in any
particular order.  

Example:

     	PUSH thisobj
	PUSH toString__()V
	PUSH toString__(I)V
	CALLJAVA dest, args, #2

The purpose is to make a call to one of a set of blocks based on 
a computation performed on the number and types of the arguments 
passed.   The 'args' vector contains the args passed to the function
making this call.  This implies that the function must be a variable
argument function.  The vector length and types of its elements
is used to determine the overload to call.



*/

#ifndef _aikidocodegen_h_included
#define _aikidocodegen_h_included

#include "aikido.h"
#include "aikidodebug.h"


namespace aikido {

namespace codegen {

class CodeGenerator ;
struct Instruction ;
struct CodeSequence ;

enum Opcode {
    opNOP,
    opVALUE,
    opVARIABLE,
    opMKVECTOR,
    opMKMAP,
    opMOV,
    opMOVC,
    opMOVF,
    opMOVO,
    opLD,
    opST,
    opCOPY,
    opTEMP,
    opADD,
    opSUB,
    opMUL,
    opDIV,
    opMOD,
    opSLL,
    opSRL,
    opSRA,
    opOR,
    opXOR,
    opAND,
    opCOMP,
    opNOT,
    opUMINUS,
    opSIZEOF,
    opTYPEOF,
    opTHROW,
    opCAST,
    opIN,
    opCMPEQ,
    opCMPNE,
    opCMPLT,
    opCMPLE,
    opCMPGT,
    opCMPGE,
    opB,
    opBT,
    opBF,
    opCALL,
    opSUPERCALL,
    opRET,
    opRETVAL,
    opTRY,
    opCATCH,
    opNEW,
    opNEWVECTOR,
    opDELETE,
    opSTREAM,
    opFINDV,
    opPUSHV,
    opSTV,
    opFINDA,
    opGETOBJ,
    opPACKAGEASSIGN,
    opPUSHSCOPE,
    opPOPSCOPE,
    opLABEL,
    opADDR,
    opADDRSUB,
    opGETSUB,
    opSETSUB,
    opDELSUB,
    opEND,
    opMUX,
    opFOREACH,
    opNEXT,
    opPUSH,
    opPUSHADDR,
    opPOP,
    opGETTHIS,
    opFOREIGN,
    opMACRO,
    opENUM,
    opINLINE,
    opCALLJAVA,
    opCALLJNI,
    opINSTANCEOF,
    opCHECKCAST,
    opREALCOMPARE,
    opTABLESWITCH,
    opLOOKUPSWITCH,
    opRTS,
    opJMPT,
    opMONITORENTER,
    opMONITOREXIT,
    opBREAKPOINT,		// debugger breakpoint
    opSTATEMENT,		// IF node to mark start of statement
    opYIELD,
    opYIELDVAL,
    opANNOTATE,

// fixed type instructions

    opFIRST_FIXED_TYPE,
// integer
    opMOV_I,
    opMOVC_I,
    opMOVF_I,
    opADD_I,
    opSUB_I,
    opMUL_I,
    opDIV_I,
    opMOD_I,
    opSLL_I,
    opSRL_I,
    opSRA_I,
    opOR_I,
    opXOR_I,
    opAND_I,
    opCOMP_I,
    opNOT_I,
    opUMINUS_I,
    opRETVAL_I,
    opCMPEQ_I,
    opCMPNE_I,
    opCMPLT_I,
    opCMPLE_I,
    opCMPGT_I,
    opCMPGE_I,

// real
    opMOV_R,
    opMOVC_R,
    opMOVF_R,
    opADD_R,
    opSUB_R,
    opMUL_R,
    opDIV_R,
    opMOD_R,
    opNOT_R,
    opUMINUS_R,
    opRETVAL_R,
    opCMPEQ_R,
    opCMPNE_R,
    opCMPLT_R,
    opCMPLE_R,
    opCMPGT_R,
    opCMPGE_R,

// string
    opMOV_S,
    opMOVC_S,
    opMOVF_S,
    opADD_S,
    opRETVAL_S,
    opCMPEQ_S,
    opCMPNE_S,
    opCMPLT_S,
    opCMPLE_S,
    opCMPGT_S,
    opCMPGE_S,

// char
    opRETVAL_C,

// vector
    opADD_V,
    opAND_V,
    opRETVAL_V,

    opSETSUB_V,
    opGETSUB_V,
    opADDRSUB_V,

    opLAST_FIXED_TYPE
} ;

struct Register {
    Register (int n) : num (n), next (NULL) {}
    int num ;
    Register *next ;
} ;

enum OpType {
    tUNKNOWN, tVALUE, tREGISTER, tVARIABLE, tLOCALVAR
} ;


//
// Intermediate Form instruction
//

struct IFNode {
    IFNode (Node *n, Opcode op) : fenode (n), opcode (op), next(NULL), prev (NULL), reg(NULL), dest(NULL), refCount(0),flags(0) {
        id = ++nodecount ;
    }
    virtual ~IFNode() {}

    static int nodecount ;

    int id ;			// node identifier
    Opcode opcode ;		// opcode
    Register *reg ;			// register assigned
    IFNode *dest ;
    int refCount ;		// reference count
    IFNode *prev ;
    IFNode *next ;
    Node *fenode ;		// parser node
    int flags;
    virtual void print (std::ostream &os) ;
    virtual void regalloc(CodeGenerator *cg) {}
    void issueBefore (IFNode *n, CodeGenerator *cg) ;
    void issueAfter (IFNode *n, CodeGenerator *cg) ;
    virtual bool isA (OpType t) { return false ; }
    virtual bool isExpression() { return false ; }
    void setDest (IFNode *d) ;
} ;

struct IFValue : public IFNode {
    IFValue (Node *n, const Value &v) : IFNode (n, opVALUE), value (v) {}
    ~IFValue() {}

    Value value ;
    void print (std::ostream &os) ;
    bool isA (OpType t) { return t == tVALUE ; }
} ;

struct IFVariable : public IFNode {
    IFVariable (Node *n, Variable *v, int l) : IFNode (n, opVARIABLE), var (v), level (l) {}
    ~IFVariable() {}
    int level ;
    Variable *var ;
    void print (std::ostream &os) ;
    bool isA (OpType t) { return t == tVARIABLE  || t == tLOCALVAR; }
} ;

struct IFInstruction : public IFNode {
    static const int NOPERS = 4 ;
    IFInstruction (Node *n, Opcode op) : IFNode (n, op) {
        init() ;
    }
    IFInstruction (Node *n, Opcode op, IFNode *op1) : IFNode (n, op) {
        init() ;
        setOperand (0, op1) ;
    }
    IFInstruction (Node *n, Opcode op, IFNode *op1, IFNode *op2) : IFNode (n, op) {
        init() ;
        setOperand (0, op1) ;
        setOperand (1, op2) ;
    }
    IFInstruction (Node *n, Opcode op, IFNode *op1, IFNode *op2, IFNode *op3) : IFNode (n, op) {
        init() ;
        setOperand (0, op1) ;
        setOperand (1, op2) ;
        setOperand (2, op3) ;
    }
    IFInstruction (Node *n, Opcode op, IFNode *op1, IFNode *op2, IFNode *op3, IFNode *op4) : IFNode (n, op) {
        init() ;
        setOperand (0, op1) ;
        setOperand (1, op2) ;
        setOperand (2, op3) ;
        setOperand (3, op4) ;
    }

    ~IFInstruction() {}
    void init() {
        for (int i= 0 ; i < NOPERS ; i++) {
            ops[i] = NULL ;
        }
        next = prev = NULL ;
    }

    IFNode *ops[NOPERS] ;
    void print (std::ostream &os) ;
   
    void setOperand (int op, IFNode *n) ;
    virtual void regalloc(CodeGenerator *cg) ;

} ;

struct IFExpression : public IFInstruction {
    IFExpression (Node *n, Opcode op) : IFInstruction (n, op), uses(0) {}
    IFExpression (Node *n, Opcode op, IFNode *op1) : IFInstruction (n, op, op1), uses(0) {}
    IFExpression (Node *n, Opcode op, IFNode *op1, IFNode *op2) : IFInstruction (n, op, op1, op2), uses(0) {}
    IFExpression (Node *n, Opcode op, IFNode *op1, IFNode *op2, IFNode *op3) : IFInstruction (n, op, op1, op2, op3), uses(0) {}
    IFExpression (Node *n, Opcode op, IFNode *op1, IFNode *op2, IFNode *op3, IFNode *op4) : IFInstruction (n, op, op1, op2, op3, op4), uses(0) {}
    
    ~IFExpression() {}
    int uses ;
    void print (std::ostream &os) ;
    virtual void regalloc(CodeGenerator *cg) ;
    bool isA (OpType t) { return t == tREGISTER ; }
    bool isExpression() { return true ; }
} ;

struct IFLabel : public IFInstruction {
   IFLabel (Node *n) : IFInstruction (n, opLABEL), addr (-1) {}
    ~IFLabel() {}
   int reference (int i, int tok) ;					// refer to the label
   void define (CodeSequence *seq, int a) ;						// define the label

   int addr ;							// address label set to
   struct Fixup {
       int inst ;
       int token ;
   } ;
   std::vector<Fixup> fixups ;				// all instructions needing fixups (indices)
} ;

struct VarPoolKey {
    VarPoolKey (Variable *v, int l) ;
    std::string name ;
    bool operator< (const VarPoolKey &k) const {
        return name < k.name ;
    }
} ;

typedef std::map<VarPoolKey, IFNode *> VarPoolMap ;

struct VariablePool {
    IFNode *get (Node *node, Variable *var, int level) ;
    VarPoolMap pool ;
    void clear() { pool.clear() ; }
} ;


typedef std::map<Value, IFNode *> ConstantMap ;

struct ConstantPool {
    IFNode *get (Node *node, Value v) ;
    ConstantMap pool ;
} ;


// real instructions

struct Operand {
    Operand() : type (tUNKNOWN), ref(0) {}
    Operand (Value &v) : val (v), type (tVALUE), ref(0) {}
    Operand (int r) : type (tREGISTER), ref(0) { val.type = T_NONE ; val.integer = r ; }
    Operand (Variable *v, int lev): type (tVARIABLE), ref(0) {  val.ptr = (void*)v ; val.type = (Type)(lev + 100) ; }
    Operand (Variable *v): type (tLOCALVAR), ref(0) {  val.ptr = (void*)v ; val.type = T_INTEGER ; }
    ~Operand() {}
    Value val ;
    OpType type ;
    void print (std::ostream &os) ;
    int ref ;
    void dump (Aikido *aikido, std::ostream &os) ;
    void load (Aikido *aikido, std::istream &is) ;
} ;

const int INST_STATEMENT = 1 ;		// instruction is a statement starter
const int INST_PASSBYNAME = 2;      // call instruction uses pass-by-name sematics for actuals
const int INST_REPLACEVARS = 4;     // replace $ vars in string

struct Instruction {
    Instruction() : source (NULL), opcode (opNOP) { init() ; }
    Instruction (SourceLocation *s, Opcode op) : source (s), opcode (op) {
        init() ;
    }
    Instruction (Node *n, Opcode op) : source (n == NULL ? NULL : n->source), opcode (op) {
        init() ;
    }
    Instruction (const Instruction &inst) ;
    ~Instruction() ;
    Instruction &operator= (const Instruction &inst) ;

    void setDest (Operand *d) ;
    void setOperand (int i, Operand *op) ;

    Opcode opcode ;
    Operand *dest ;
    Operand *src[4] ;
    SourceLocation *source ;		// source location
    int flags ;
    void print (std::ostream &os) ;
    void fixup (int addr, int oper) ;
    void init() {
        dest = NULL ;
        for (int i = 0 ; i < 4 ; i++) {
            src[i] = NULL ;
        }
        flags = 0 ;
    }
    void dump (Aikido *aikido, std::ostream &os) ;
    void load (Aikido *aikido, std::istream &is) ;

} ;

// a code sequence for a block looks like:
// NOTE: this has been changed for 'pass-by-name' calll semantics.  See below

// for func (a, b, c = 1, d = 2 : integer, e = 3 : integer)

/*
                             +--------------------+
                             |  B codestart       |
                             +--------------------+   <-- A
                             |  B default_e       |
                             +--------------------+
                             |  B default_d       |
                             +--------------------+
                             |  set c = 1         |
                             +--------------------+
         default_d:          |  set d = 2         |
                             +--------------------+
         default_e:          |  set e = 3         |
                             +--------------------+   <-- B
         codestart:          |  cast<int>(d)      |
                             +--------------------+
                             |  cast<int>(e)      |
                             +--------------------+
                             |                    |
                             .  body of func      .
                             |                    |
                             +--------------------+

To append another code seqence to this we need to:

1. at point A - insert branches to the default parameter assignment code for
   the new default parameters.  This will move all the subsequent code down.
   The branches are done in reverse order of parameter position

2. at (new) point B, insert the code for the default parameter assignments
   in the order they appear in the code

3. after the default assignments are inserted, insert the code for the parameter
   casting checks

The number of registers used by the code should not increase, but it can happen if
the default parameter assignment code is complex.

So, after adding a new parameter f = 4 : int

                             +--------------------+
                             |  B codestart       |
                             +--------------------+
                             |  B default_f       | <====
                             +--------------------+
                             |  B default_e       |
                             +--------------------+
                             |  B default_d       |
                             +--------------------+
                             |  set c = 1         |
                             +--------------------+
         default_d:          |  set d = 2         |
                             +--------------------+
         default_e:          |  set e = 3         |
                             +--------------------+  
         default_f:          |  set f = 4         | <====
                             +--------------------+
         codestart:          |  cast<int>(f)      | <====
                             +--------------------+
         oldcodestart:       |  cast<int>(d)      |
                             +--------------------+
                             |  cast<int>(e)      |
                             +--------------------+
                             |                    |
                             .  body of func      .
                             |                    |
                             +--------------------+
 */


/*
For pass-by-name call semantics the user can omit any parameter value and they are not passed in sequence.  The 
code for this omits the branch table completely and instead puts each default value in the code as the code:

if (p == none) {
    p = defaultvalue
}

*/

struct Extension {
    Extension() {}
    ~Extension() ;
    CodeSequence *paracode ;		// code for parameter default assignments
    CodeSequence *code ;		// code for body
    std::vector<int> paraaddrs ;		// addresses for parameters
    int caststart ;			// offset to cast code
    int nregs ;
} ;

struct CodeSequence {
    CodeSequence() ;
    std::vector<Instruction> code ;		// code itself
    int nregs ;					// max number of regs used
    int argstart ;				// pc at start of default arg assignments
    int codestart ;				// pc at end of arg checking (after casts)

    OSMutex lock ;
    void print (std::ostream &os) ;
    void relocate (int start, int offset, bool movecode = true) ;     // relocate code from start by offset bytes
    void append (Block *blk, Debugger *db, Extension *x) ;
    void registerInstructions (Debugger *debugger) ;
    InstructionPtr firstStatement() ;

    void dump (Aikido *aikido, std::ostream &os) ;
    void load (Aikido *aikido, std::istream &is) ;
} ;


class CodeGenerator {
    friend class IFNode ;
public:
    CodeGenerator() ;
    CodeSequence *generate (Node *node) ;		// generate code from unsequenced node
    CodeSequence *generate (InterpretedBlock *block) ;		// generate all code for a block
    Extension *generateExtension (Node *node, std::vector<Parameter*> &paras) ;

    void print (std::ostream &os) ;

    // public register allocation functions
    void freeReg (Register *reg) ;
    void allocateRegisters() ;
    Register *allocateReg (IFNode *node) ;

    std::map<Token, Opcode> tokenMap ;
    typedef std::map<Type, Opcode> OpTypeMap ;
    typedef std::map<Token, const OpTypeMap*> TokenOpTypeMap ;
    TokenOpTypeMap fixedTypes ;
    OpTypeMap movOps ;
    OpTypeMap movcOps ;
    OpTypeMap movfOps ;

    std::stack<IFNode*> breakLabels ;
    std::stack<IFNode*> continueLabels ;
    std::stack<int> breakscopes ;
    std::stack<int> continuescopes ;
    std::deque<IFNode*> monitorStack ;

    VariablePool variables ;
    ConstantPool constants ;
    int scopeLevel ;
    int trylevel ;
    int foreachlevel ;
    int monitorlevel ;
    IFNode *zero ;
    Register *regFreeList ;
    int makeScopeLevel() { return monitorlevel << 28 | trylevel << 20 | (scopeLevel << 10) | foreachlevel ; }
    int getTryLevel (int l) { return (l >> 20) & 0xff ; }
    int getScopeLevel (int l) { return (l >> 10) & 0x3ff ; }
    int getForeachLevel (int l) { return (l >> 0) & 0x3ff ; }
    int getMonitorLevel (int l) { return (l >> 28) & 0x1f ; }

    void tidyup() ;			// tidy up from last run and prepare this one

    // IF building
    void build (Node *node) ;
    void issuePools() ;


    IFNode *code ;
    IFNode *lastInstruction ;

    IFNode *issue (IFNode *node) ;

    IFNode *expression (Node *n) ;
    IFExpression *store (Node *node, Node *left, IFNode *value) ;
    IFExpression *store (Node *n) ;
    IFNode *construct (Node *n) ;
    IFNode *newVector (Node *n) ;
    IFNode *address (Node *n) ;
    void pushaddress (Node *n) ;
    IFNode *call (Node *n) ;
    IFNode *integer (Node *node, int n) ;
    IFNode *conditional (Node *n) ;
    IFNode *logicalAnd (Node *n) ;
    IFNode *logicalOr (Node *n) ;
    IFNode *mkvector (Node *n) ;
    IFNode *mkmap (Node *n) ;

    IFNode *compareTrue (Node *n, IFNode *left) ;

    void statement (Node *n) ;
    void buildIf (Node *n) ;
    void buildWhile (Node *n) ;
    void buildDo (Node *n) ;
    void buildSynchronized (Node *n) ;
    void buildFor (Node *n) ;
    void buildCompound (Node *n) ;
    void buildBreak (Node *n) ;
    void buildContinue (Node *n) ;
    void buildForeach (Node *n) ;
    void buildSwitch (Node *n) ;
    void buildDelete (Node *n) ;
    void buildTry (Node *n) ;
    void buildReturn (Node *n) ;
    void buildYield (Node *n) ;
    void buildThrow (Node *n) ;
    void buildMacro (Node *n) ;
    void buildEnum (Node *n) ;
    void foreign (Node *n) ;
    void supercall (Node *n) ;
    IFExpression *subnode (Node *n, Opcode op, Opcode fixedop) ;

    // register allocation
    void freeRegs (IFNode *node) ;
    int nregs ;
    void resetRegisters() ;

    
    // code emission
    CodeSequence *codesequence ;
    CodeSequence *emit() ;
    int flags ;				// flags for next instruction
    void emit (IFNode *node) ;
    void expression (IFExpression *expr) ;
    void statement (IFInstruction *inst) ;
    void loadstore (IFExpression *expr) ;
    void move (IFExpression *expr) ;
    void issue (Instruction &inst) ;
    Operand *operand (IFNode *node) ;
    int currentAddr ;
    void setDest (Instruction &inst, IFExpression *ex) ;

} ;

}	// namespace codegen
}	// namespace aikido

#endif
