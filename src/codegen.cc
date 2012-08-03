/*
 * codegen.cc
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
 * Contributor(s): dallison
 *
 * Version:  1.19
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */

// CHANGE LOG
//	1/21/2004	David Allison of PathScale Inc.
//		Added dump and load functions



#include "aikidocodegen.h"

#ifdef JAVA
#include "java/class.h"
#endif

bool disassemble ;
bool printif ;		// print IF instructions

namespace aikido {

namespace codegen {

// map of opcode versus opcode name
std::map<Opcode, std::string> opnames ;
bool opnamesok ;

int IFNode::nodecount = 0 ;


void IFNode::print (std::ostream &os) {
    os << '$' << id << '\t' << opnames[opcode] << " " ;
}

//
// issue this node before node n
//

void IFNode::issueBefore (IFNode *n, CodeGenerator *cg) {
    next = n ;
    prev = n->prev ;
    n->prev = this ;
    if (n->prev == NULL) {
        cg->code = this ;
        if (cg->lastInstruction == n) {
            cg->lastInstruction = this ;
        }
    } else {
        n->prev->next = this ;
    }
}

//
// issue this node after node n
//

void IFNode::issueAfter (IFNode *n, CodeGenerator *cg) {
    next = n->next ;
    prev = n ;
    n->next = this ;
    if (n->next == NULL) {
        cg->lastInstruction = this ;
        if (cg->code == n) {
            cg->code = this ;
        }
    } else {
        n->next->prev = this ;
    }
}

// print a node to a stream
void IFValue::print (std::ostream &os) {
    IFNode::print (os) ;
    os << "type: " << value.type ;
    if (value.type == T_INTEGER) {
        os << " integer: " << value.integer << '\n' ;
    } else if (value.type == T_STRING) {
        os << " string: " << *value.str << '\n' ;
    } else {
        os << "\n" ;
    }
}

void IFVariable::print (std::ostream &os) {
    IFNode::print (os) ;
    os << "name: " << var->name << ", level: " << level << " *" << refCount << '\n' ;
}

void IFInstruction::print (std::ostream &os) {
    IFNode::print (os) ;
    os << "(" ;
    if (ops[0] != NULL) {
        os << "$" << ops[0]->id ;
    }
    if (ops[1] != NULL) {
        os << ",$" << ops[1]->id ;
    }
    if (ops[2] != NULL) {
        os << ",$" << ops[2]->id ;
    }
    if (ops[3] != NULL) {
        os << ",$" << ops[3]->id ;
    }
    os << ") " ;
    if (dest != NULL) {
        os << ">$" << dest->id << " " ;
    }
    if (reg != NULL) {
        os << "R" << reg->num ;
    }
    os <<  " *" << refCount << '\n' ;

}

void IFInstruction::regalloc(CodeGenerator *cg) {
    for (int i = 0 ; i < NOPERS ; i++) {
        IFNode *op = ops[i] ;
        if (op != NULL) {
           if (op->reg != NULL) {
               IFExpression *expr = dynamic_cast<IFExpression*>(op) ;
               expr->uses-- ;
               if (expr->uses <= 0) {
                   cg->freeReg (op->reg) ;
               }
            }
        }
    }
}

void IFExpression::print (std::ostream &os) {
    IFInstruction::print (os) ;
}

//
// for FINDA and ADDRSUB, the destination register cannot be the
// same as the source (it might cause the object or vector to be
// garbage collected before the result is read).  To do this
// we free the registers after allocating the destination for these
// opcodes
//

void IFExpression::regalloc(CodeGenerator *cg) {
    if (opcode != opFINDA && opcode != opADDRSUB) {
        IFInstruction::regalloc(cg) ;
    }
    if (dest == NULL) {
        reg = cg->allocateReg (this) ;
        uses = refCount ;
        if (uses == 0) {
            cg->freeReg (reg) ;		// free it again if no use
        }
    }
    if (opcode == opFINDA || opcode == opADDRSUB) {
        IFInstruction::regalloc(cg) ;
    }
}


//
// set the operand of an instruction
//
void IFInstruction::setOperand (int op, IFNode *n) {
    if (ops[op] != NULL) {
        ops[op]->refCount-- ;
    }
    ops[op] = n ;
    if (n != NULL) {
        n->refCount++ ;
    }
}

//
// set the destination of an instruction.  This is where the result is held
//
void IFNode::setDest (IFNode *d) {
    if (dest != NULL) {
        dest->refCount-- ;
    }
    dest = d ;
    if (d != NULL) {
        d->refCount++ ;
    }
    
}

//
// generate a reference to a label
//
int IFLabel::reference (int i, int tok) {
    if (addr != -1) {
        return addr ;
    }
    Fixup f ;
    f.inst = i ;
    f.token = tok ;

    fixups.push_back (f) ;
    return -1 ;
}

//
// define a label and fixup all predefined references
//
void IFLabel::define (CodeSequence *seq, int a) {
    addr = a ;
    for (int i = 0 ; i < fixups.size() ; i++) {
        seq->code[fixups[i].inst].fixup (addr, fixups[i].token) ;
    }
}

// variable pool
VarPoolKey::VarPoolKey (Variable *var, int level) {
    char buf[100] ;
    sprintf (buf, "%p_%d", var, level) ;
    name = buf ;
}

IFNode *VariablePool::get (Node *node, Variable *var, int level) {
    VarPoolKey key (var, level) ;
    VarPoolMap::iterator i = pool.find (key) ;
    if (i == pool.end()) {
        IFNode *varnode = new IFVariable (node, var, level) ;
        pool[key] = varnode ;
        return varnode ;
    }
    return (*i).second ;
}

IFNode *ConstantPool::get (Node *node, Value v) {
    ConstantMap::iterator i = pool.find (v) ;
    if (i == pool.end()) {
        IFNode *valnode = new IFValue (node, v) ;
        pool[v] = valnode ;
        return valnode ;
    }
    return (*i).second ;
}


//
// main constructor for the code generator
//
CodeGenerator::CodeGenerator() {
    scopeLevel = 0 ;
    tokenMap[PLUS] = opADD ;
    tokenMap[MINUS] = opSUB ;
    tokenMap[STAR] = opMUL ;
    tokenMap[SLASH] = opDIV ;
    tokenMap[PERCENT] = opMOD ;
    tokenMap[LSHIFT] = opSLL ;
    tokenMap[RSHIFT] = opSRA ;
    tokenMap[ZRSHIFT] = opSRL ;
    tokenMap[CARET] = opXOR ;
    tokenMap[TILDE] = opCOMP ;
    tokenMap[BITOR] = opOR ;
    tokenMap[AMPERSAND] = opAND ;
    tokenMap[BANG] = opNOT ;
    tokenMap[LESS] = opCMPLT ;
    tokenMap[LESSEQ] = opCMPLE ;
    tokenMap[GREATER] = opCMPGT ;
    tokenMap[GREATEREQ] = opCMPGE ;
    tokenMap[EQUAL] = opCMPEQ ;
    tokenMap[NOTEQ] = opCMPNE ;
    tokenMap[UMINUS] = opUMINUS ;
    tokenMap[SIZEOF] = opSIZEOF ;
    tokenMap[TYPEOF] = opTYPEOF ;
    tokenMap[ARROW] = opSTREAM ;
    tokenMap[CAST] = opCAST ;
    tokenMap[INSTANCEOF] = opINSTANCEOF ;
    tokenMap[TIN] = opIN ;

    // initialize the op type maps
    movOps[T_INTEGER] = opMOV_I ;
    movOps[T_BOOL] = opMOV_I ;
    movOps[T_REAL] = opMOV_R ;
    movOps[T_STRING] = opMOV_S ;
    movOps[T_CHAR] = opMOV_I ;

    movcOps[T_INTEGER] = opMOVC_I ;
    movcOps[T_BOOL] = opMOVC_I ;
    movcOps[T_REAL] = opMOVC_R ;
    movcOps[T_STRING] = opMOVC_S ;
    movcOps[T_CHAR] = opMOVC_I ;

    movfOps[T_INTEGER] = opMOVF_I ;
    movfOps[T_BOOL] = opMOVF_I ;
    movfOps[T_REAL] = opMOVF_R ;
    movfOps[T_STRING] = opMOVF_S ;
    movfOps[T_CHAR] = opMOVF_I ;

    OpTypeMap *ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opADD_I ;
    (*ops)[T_BOOL] = opADD_I ;
    (*ops)[T_REAL] = opADD_R ;
    (*ops)[T_STRING] = opADD_S ;
    (*ops)[T_CHAR] = opADD_I ;
    (*ops)[T_VECTOR] = opADD_V ;
    fixedTypes[PLUS] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opSUB_I ;
    (*ops)[T_BOOL] = opSUB_I ;
    (*ops)[T_REAL] = opSUB_R ;
    (*ops)[T_CHAR] = opSUB_I ;
    fixedTypes[MINUS] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opMUL_I ;
    (*ops)[T_BOOL] = opMUL_I ;
    (*ops)[T_REAL] = opMUL_R ;
    (*ops)[T_CHAR] = opMUL_I ;
    fixedTypes[STAR] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opDIV_I ;
    (*ops)[T_BOOL] = opDIV_I ;
    (*ops)[T_REAL] = opDIV_R ;
    (*ops)[T_CHAR] = opDIV_I ;
    fixedTypes[SLASH] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opMOD_I ;
    (*ops)[T_BOOL] = opMOD_I ;
    (*ops)[T_REAL] = opMOD_R ;
    (*ops)[T_CHAR] = opMOD_I ;
    fixedTypes[PERCENT] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opSLL_I ;
    (*ops)[T_BOOL] = opSLL_I ;
    (*ops)[T_CHAR] = opSLL_I ;
    fixedTypes[LSHIFT] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opSRL_I ;
    (*ops)[T_BOOL] = opSRL_I ;
    (*ops)[T_CHAR] = opSRL_I ;
    fixedTypes[ZRSHIFT] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opSRA_I ;
    (*ops)[T_BOOL] = opSRA_I ;
    (*ops)[T_CHAR] = opSRA_I ;
    fixedTypes[RSHIFT] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opOR_I ;
    (*ops)[T_BOOL] = opOR_I ;
    (*ops)[T_CHAR] = opOR_I ;
    fixedTypes[BITOR] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opXOR_I ;
    (*ops)[T_BOOL] = opXOR_I ;
    (*ops)[T_CHAR] = opXOR_I ;
    fixedTypes[CARET] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opAND_I ;
    (*ops)[T_BOOL] = opAND_I ;
    (*ops)[T_CHAR] = opAND_I ;
    (*ops)[T_VECTOR] = opAND_V ;
    fixedTypes[AMPERSAND] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opCOMP_I ;
    (*ops)[T_BOOL] = opCOMP_I ;
    (*ops)[T_CHAR] = opCOMP_I ;
    fixedTypes[TILDE] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opNOT_I ;
    (*ops)[T_BOOL] = opNOT_I ;
    (*ops)[T_CHAR] = opNOT_I ;
    fixedTypes[BANG] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opUMINUS_I ;
    (*ops)[T_BOOL] = opUMINUS_I ;
    (*ops)[T_CHAR] = opUMINUS_I ;
    fixedTypes[UMINUS] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opRETVAL_I ;
    (*ops)[T_BOOL] = opRETVAL_I ;
    (*ops)[T_REAL] = opRETVAL_R ;
    (*ops)[T_STRING] = opRETVAL_S ;
    (*ops)[T_CHAR] = opRETVAL_I ;
    (*ops)[T_VECTOR] = opRETVAL_V ;
    fixedTypes[RETURN] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opCMPEQ_I ;
    (*ops)[T_BOOL] = opCMPEQ_I ;
    (*ops)[T_REAL] = opCMPEQ_R ;
    (*ops)[T_STRING] = opCMPEQ_S ;
    (*ops)[T_CHAR] = opCMPEQ_I ;
    fixedTypes[EQUAL] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opCMPNE_I ;
    (*ops)[T_BOOL] = opCMPNE_I ;
    (*ops)[T_REAL] = opCMPNE_R ;
    (*ops)[T_STRING] = opCMPNE_S ;
    (*ops)[T_CHAR] = opCMPNE_I ;
    fixedTypes[NOTEQ] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opCMPLT_I ;
    (*ops)[T_BOOL] = opCMPLT_I ;
    (*ops)[T_REAL] = opCMPLT_R ;
    (*ops)[T_STRING] = opCMPLT_S ;
    (*ops)[T_CHAR] = opCMPLT_I ;
    fixedTypes[LESS] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opCMPLE_I ;
    (*ops)[T_BOOL] = opCMPLE_I ;
    (*ops)[T_REAL] = opCMPLE_R ;
    (*ops)[T_STRING] = opCMPLE_S ;
    (*ops)[T_CHAR] = opCMPLE_I ;
    fixedTypes[LESSEQ] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opCMPGT_I ;
    (*ops)[T_BOOL] = opCMPGT_I ;
    (*ops)[T_REAL] = opCMPGT_R ;
    (*ops)[T_STRING] = opCMPGT_S ;
    (*ops)[T_CHAR] = opCMPGT_I ;
    fixedTypes[GREATER] = ops ;

    ops = new OpTypeMap ;
    (*ops)[T_INTEGER] = opCMPGE_I ;
    (*ops)[T_BOOL] = opCMPGE_I ;
    (*ops)[T_REAL] = opCMPGE_R ;
    (*ops)[T_STRING] = opCMPGE_S ;
    (*ops)[T_CHAR] = opCMPGE_I ;
    fixedTypes[GREATEREQ] = ops ;


    if (!opnamesok) {
        opnames[opVALUE] = "VALUE" ;
        opnames[opVARIABLE] = "VARIABLE" ;
        opnames[opMKVECTOR] = "MKVECTOR" ;
        opnames[opMKMAP] = "MKMAP" ;
        opnames[opMOV] = "MOV" ;
        opnames[opMOVC] = "MOVC" ;
        opnames[opMOVF] = "MOVF" ;
        opnames[opMOVO] = "MOVO" ;
        opnames[opCOPY] = "COPY" ;
        opnames[opTEMP] = "TEMP" ;
        opnames[opLD] = "LD" ;
        opnames[opST] = "ST" ;
        opnames[opADD] = "ADD" ;
        opnames[opSUB] = "SUB" ;
        opnames[opMUL] = "MUL" ;
        opnames[opDIV] = "DIV" ;
        opnames[opMOD] = "MOD" ;
        opnames[opSLL] = "SLL" ;
        opnames[opSRL] = "SRL" ;
        opnames[opSRA] = "SRA" ;
        opnames[opOR] = "OR" ;
        opnames[opXOR] = "XOR" ;
        opnames[opAND] = "AND" ;
        opnames[opCOMP] = "COMP" ;
        opnames[opNOT] = "NOT" ;
        opnames[opUMINUS] = "UMINUS" ;
        opnames[opSIZEOF] = "SIZEOF" ;
        opnames[opTYPEOF] = "TYPEOF" ;
        opnames[opCAST] = "CAST" ;
        opnames[opIN] = "IN" ;
        opnames[opCMPEQ] = "CMPEQ" ;
        opnames[opCMPNE] = "CMPNE" ;
        opnames[opCMPLT] = "CMPLT" ;
        opnames[opCMPLE] = "CMPLE" ;
        opnames[opCMPGT] = "CMPGT" ;
        opnames[opCMPGE] = "CMPGE" ;
        opnames[opB] = "B" ;
        opnames[opBT] = "BT" ;
        opnames[opBF] = "BF" ;
        opnames[opCALL] = "CALL" ;
        opnames[opRET] = "RET" ;
        opnames[opRETVAL] = "RETVAL" ;
        opnames[opYIELD] = "YIELD" ;
        opnames[opANNOTATE] = "ANNOTATE" ;
        opnames[opYIELDVAL] = "YIELDVAL" ;
        opnames[opTHROW] = "THROW" ;
        opnames[opTRY] = "TRY" ;
        opnames[opCATCH] = "CATCH" ;
        opnames[opNEW] = "NEW" ;
        opnames[opNEWVECTOR] = "NEWVECTOR" ;
        opnames[opDELETE] = "DELETE" ;
        opnames[opSTREAM] = "STREAM" ;
        opnames[opFINDV] = "FINDV" ;
        opnames[opPUSHV] = "PUSHV" ;
        opnames[opSTV] = "STV" ;
        opnames[opFINDA] = "FINDA" ;
        opnames[opADDR] = "ADDR" ;
        opnames[opADDRSUB] = "ADDRSUB" ;
        opnames[opGETOBJ] = "GETOBJ" ;
        opnames[opPACKAGEASSIGN] = "PACKAGEASSIGN" ;
        opnames[opPUSHSCOPE] = "PUSHSCOPE" ;
        opnames[opPOPSCOPE] = "POPSCOPE"  ;
        opnames[opLABEL] = "LABEL" ;
        opnames[opSETSUB] = "SETSUB" ;
        opnames[opGETSUB] = "GETSUB" ;
        opnames[opDELSUB] = "DELSUB" ;
        opnames[opEND] = "END" ;
        opnames[opMUX] = "MUX" ;
        opnames[opFOREACH] = "FOREACH" ;
        opnames[opNEXT] = "NEXT" ;
        opnames[opFOREIGN] = "FOREIGN" ;
        opnames[opSUPERCALL] = "SUPERCALL" ;
        opnames[opPUSH] = "PUSH" ;
        opnames[opPUSHADDR] = "PUSHADDR" ;
        opnames[opPOP] = "POP" ;
        opnames[opGETTHIS] = "GETTHIS" ;
        opnames[opMACRO] = "MACRO" ;
        opnames[opENUM] = "ENUM" ;
        opnames[opNOP] = "NOP" ;
        opnames[opBREAKPOINT] = "BREAKPOINT" ;
        opnames[opSTATEMENT] = "STATEMENT" ;
        opnames[opINLINE] = "INLINE" ;
        opnames[opCALLJAVA] = "CALLJAVA" ;
        opnames[opCALLJNI] = "CALLJNI" ;
        opnames[opINSTANCEOF] = "INSTANCEOF" ;
        opnames[opCHECKCAST] = "CHECKCAST" ;
        opnames[opREALCOMPARE] = "REALCOMPARE" ;
        opnames[opTABLESWITCH] = "TABLESWITCH" ;
        opnames[opLOOKUPSWITCH] = "LOOKUPSWITCH" ;
        opnames[opRTS] = "RTS" ;
        opnames[opJMPT] = "JMPT" ;
        opnames[opMONITORENTER] = "MONITORENTER" ;
        opnames[opMONITOREXIT] = "MONITOREXIT" ;
        opnames[opMOV_I] = "MOV.I" ;
        opnames[opMOVC_I] = "MOVC.I" ;
        opnames[opMOVF_I] = "MOVF.I" ;
        opnames[opADD_I] = "ADD.I" ;
        opnames[opSUB_I] = "SUB.I" ;
        opnames[opMUL_I] = "MUL.I" ;
        opnames[opDIV_I] = "DIV.I" ;
        opnames[opMOD_I] = "MOD.I" ;
        opnames[opSLL_I] = "SLL.I" ;
        opnames[opSRL_I] = "SRL.I" ;
        opnames[opSRA_I] = "SRA.I" ;
        opnames[opOR_I] = "OR.I" ;
        opnames[opXOR_I] = "XOR.I" ;
        opnames[opAND_I] = "AND.I" ;
        opnames[opCOMP_I] = "COMP.I" ;
        opnames[opNOT_I] = "NOT.I" ;
        opnames[opUMINUS_I] = "UMINUS.I" ;
        opnames[opRETVAL_I] = "RETVAL.I" ;
        opnames[opCMPEQ_I] = "CMPEQ.I" ;
        opnames[opCMPNE_I] = "CMPNE.I" ;
        opnames[opCMPLT_I] = "CMPLT.I" ;
        opnames[opCMPLE_I] = "CMPLE.I" ;
        opnames[opCMPGT_I] = "CMPGT.I" ;
        opnames[opCMPGE_I] = "CMPGE.I" ;

        opnames[opMOV_R] = "MOV.R" ;
        opnames[opMOVC_R] = "MOVC.R" ;
        opnames[opMOVF_R] = "MOVF.R" ;
        opnames[opADD_R] = "ADD.R" ;
        opnames[opSUB_R] = "SUB.R" ;
        opnames[opMUL_R] = "MUL.R" ;
        opnames[opDIV_R] = "DIV.R" ;
        opnames[opMOD_R] = "MOD.R" ;
        opnames[opNOT_R] = "NOT.R" ;
        opnames[opUMINUS_R] = "UMINUS.R" ;
        opnames[opRETVAL_R] = "RETVAL.R" ;
        opnames[opCMPEQ_R] = "CMPEQ.R" ;
        opnames[opCMPNE_R] = "CMPNE.R" ;
        opnames[opCMPLT_R] = "CMPLT.R" ;
        opnames[opCMPLE_R] = "CMPLE.R" ;
        opnames[opCMPGT_R] = "CMPGT.R" ;
        opnames[opCMPGE_R] = "CMPGE.R" ;
        opnames[opRETVAL_R] = "RETVAL.R" ;

        opnames[opMOV_S] = "MOV.S" ;
        opnames[opMOVC_S] = "MOVC.S" ;
        opnames[opMOVF_S] = "MOVF.S" ;
        opnames[opADD_S] = "ADD.S" ;
        opnames[opRETVAL_S] = "RETVAL.S" ;
        opnames[opCMPEQ_S] = "CMPEQ.S" ;
        opnames[opCMPNE_S] = "CMPNE.S" ;
        opnames[opCMPLT_S] = "CMPLT.S" ;
        opnames[opCMPLE_S] = "CMPLE.S" ;
        opnames[opCMPGT_S] = "CMPGT.S" ;
        opnames[opCMPGE_S] = "CMPGE.S" ;

        opnames[opADD_V] = "ADD.V" ;
        opnames[opAND_V] = "AND.V" ;
	opnames[opRETVAL_V] = "RETVAL.V" ;
	opnames[opGETSUB_V] = "GETSUB.V" ;
	opnames[opSETSUB_V] = "SETSUB.V" ;
	opnames[opADDRSUB_V] = "ADDRSUB.V" ;

        opnamesok = true ;
    }

    nregs = 0 ;
    regFreeList = NULL ;
    code = NULL ;
    lastInstruction = NULL ;
}

//
// issue the pools
//
void CodeGenerator::issuePools() {
    VarPoolMap::iterator v ;
    ConstantMap::iterator c ;
    for (v = variables.pool.begin() ; v != variables.pool.end() ; v++) {
        issue ((*v).second) ;
    }
    //for (c = constants.pool.begin() ; c != constants.pool.end() ; c++) {
        //issue ((*c).second) ;
    //}
}


void CodeGenerator::print (std::ostream &os) {
    IFNode *n = code ;
    while (n != NULL) {
       n->print (os) ;
       n = n->next ;
    }
}


//
// generate code for a block
//

CodeSequence *CodeGenerator::generate (InterpretedBlock *block) {
    tidyup() ;
    std::vector<IFNode *> deflabels ;

#if 0
    // assign default values to those not already assigned
    for (int i = 0 ; i < block->parameters.size() ; i++) {
        Parameter *p = block->parameters[i] ;
        if (p->def != NULL) {
            IFNode *label = issue (new IFLabel (p->def)) ;
            deflabels.push_back (label) ;
            IFNode *defnode = expression (p->def) ;
            issue (new IFExpression (p->def, opMOV, variables.get (p->def, p, 0), defnode)) ;
        }
    }

    // issue a label for the start of the parameter casting code
    IFNode *label = issue (new IFLabel (block->body)) ;

    for (int i = 1 ; i < deflabels.size() ; i++) {
        IFNode *bra = new IFInstruction (block->body, opB, deflabels[i]) ;
        bra->issueAfter (zero, this) ;
    }


    // insert either a B or NOP
    if (deflabels.size() > 0) {
        // insert first branch
        IFNode *bra = new IFInstruction (block->body, opB, label) ;
        bra->issueAfter (zero, this) ;
    } else {
        IFNode *nop = new IFInstruction (block->body, opNOP) ;
        nop->issueAfter (zero, this) ;
    }
#else

    // version 2 to support pass-by-name call semantics
    // for each parameter with a default value generate the following code:
    // 
    // if (p == none) {
    //    p = def
    // } 

    // assign default values to those not already assigned
    for (int i = 0 ; i < block->parameters.size() ; i++) {
        Parameter *p = block->parameters[i] ;
        if (p->def != NULL) {
            IFLabel *skiplabel = new IFLabel (p->def);
            IFValue *none = new IFValue (p->def, Value());
            IFExpression *cmp = new IFExpression (p->def, opCMPEQ, variables.get (p->def, p, 0), none);
            issue (cmp);
            issue (new IFInstruction (p->def, opBF, cmp, skiplabel));
            IFNode *defnode = expression (p->def) ;
            issue (new IFExpression (p->def, opMOV, variables.get (p->def, p, 0), defnode)) ;
            issue (skiplabel);
        }
    }
#endif
    
    IFLabel *endparas = static_cast<IFLabel*>(issue (new IFLabel (block->body))) ;

    // cast the parameters to their formal type
    for (int i = 0 ; i < block->parameters.size() ; i++) {
        Parameter *p = block->parameters[i] ;
        if (p->type != NULL) {
            IFNode *typenode = expression (p->type) ;
            IFNode *var = variables.get (p->type, p, 0) ;
            IFNode *cast = issue (new IFExpression (p->type, opCAST, typenode, var, integer (p->type, i))) ;
            issue (new IFExpression (p->type, opMOVF, var, cast)) ;
        }
    }


    build (block->body) ;
    issue (new IFInstruction (block->body, opEND)) ;
    issuePools() ;
    allocateRegisters() ;
    if (printif) {
        print (std::cout) ;
    }
    CodeSequence *seq = emit() ;

    seq->codestart = endparas->addr ;			// set end of arg checking code

    if (disassemble) {
        std::cout << "\nBlock: " << block->name << ":\n" ;
        seq->print (std::cout) ;
    }
    return seq ;
}


//
// generate code for a block extension
//

Extension *CodeGenerator::generateExtension (Node *node, std::vector<Parameter*> &paras) {
    tidyup() ;
    std::vector<IFNode *> deflabels ;
    Extension *x = new Extension() ;

#if 0
    // assign default values to those not already assigned
    for (int i = 0 ; i < paras.size() ; i++) {
        Parameter *p = paras[i] ;
        if (p->def != NULL) {
            IFNode *label = issue (new IFLabel (p->def)) ;
            deflabels.push_back (label) ;
            IFNode *defnode = expression (p->def) ;
            issue (new IFExpression (p->def, opMOV, variables.get (p->def, p, 0), defnode)) ;
        }
    }

#else
    // version 2 to support pass-by-name call semantics
    // for each parameter with a default value generate the following code:
    // 
    // if (p == none) {
    //    p = def
    // } 

    // assign default values to those not already assigned
    for (int i = 0 ; i < paras.size() ; i++) {
        Parameter *p = paras[i] ;
        if (p->def != NULL) {
            IFLabel *skiplabel = new IFLabel (p->def);
            IFValue *none = new IFValue (p->def, Value());
            IFExpression *cmp = new IFExpression (p->def, opCMPEQ, variables.get (p->def, p, 0), none);
            issue (cmp);
            issue (new IFInstruction (p->def, opBF, cmp, skiplabel));
            IFNode *defnode = expression (p->def) ;
            issue (new IFExpression (p->def, opMOV, variables.get (p->def, p, 0), defnode)) ;
            issue (skiplabel);
        }
    }
#endif
    // label at start of cast code
    IFNode *castnode = issue (new IFLabel (node)) ;

    // cast the parameters to their formal type
    for (int i = 0 ; i < paras.size() ; i++) {
        Parameter *p = paras[i] ;
        if (p->type != NULL) {
            IFNode *typenode = expression (p->type) ;
            IFNode *var = variables.get (p->type, p, 0) ;
            IFNode *cast = issue (new IFExpression (p->type, opCAST, typenode, var, integer (p->type, i))) ;
            issue (new IFExpression (p->type, opMOVF, var, cast)) ;
        }
    }

    issuePools() ;
    allocateRegisters() ;
    x->paracode = emit() ;

    x->caststart = static_cast<IFLabel*>(castnode)->addr ;		// find address of cast code


    // get the addresses for the parameter default code
    for (int i = deflabels.size() - 1 ; i >= 0 ; i--) {
        x->paraaddrs.push_back (static_cast<IFLabel*>(deflabels[i])->addr) ;
    }

    x->nregs = nregs ;

    // now do the code for the body
    tidyup() ;
    build (node) ;
    issue (new IFInstruction (node, opEND)) ;
    issuePools() ;
    allocateRegisters() ;
    if (printif) {
        print (std::cout) ;
    }
    x->code = emit() ;

    if (disassemble) {
        std::cout << "\nExtension code for block:\n" ;
        x->paracode->print (std::cout) ;
        x->code->print (std::cout) ;
    }

    if (nregs > x->nregs) {
        x->nregs = nregs ;
    }
    return x ;
}


//
// generate code for a whole parse tree
//
CodeSequence *CodeGenerator::generate (Node *node) {
    tidyup() ;
    build (node) ;
    issue (new IFInstruction (node, opEND)) ;
    issuePools() ;
    allocateRegisters() ;
    if (printif) {
        print (std::cout) ;
    }
    CodeSequence *seq = emit() ;
    if (disassemble) {
        std::cout << "\nMain program:\n" ;
        seq->print (std::cout) ;
    }
    return seq ;
}

//
// clean up after a previous run
//

void CodeGenerator::tidyup() {
    variables.clear() ;
    resetRegisters() ;
    IFNode *node, *next ;
    node = code ;
    while (node != NULL) {
        next = node->next ;
        delete node ;
        node = next ;
    }
    code = NULL ;
    lastInstruction = NULL ;
    zero = integer (NULL, 0) ;
    while (!breakLabels.empty()) {
        breakLabels.pop() ;
    }
    while (!continueLabels.empty()) {
        continueLabels.pop() ;
    }
    while (!breakscopes.empty()) {
        breakscopes.pop() ;
    }
    while (!continuescopes.empty()) {
        continuescopes.pop() ;
    }
    scopeLevel = 0 ;
    trylevel = 0 ;
    foreachlevel = 0 ;
    flags = 0 ;
    monitorlevel = 0 ;
}

//
// build and issue the code for a parse tree node and its children
//

void CodeGenerator::build (Node *node) {
    IFNode *inst ;
    if (node == NULL) {
        return ;
    }
    switch (node->op) {
    case IDENTIFIER:
    case NUMBER:
    case FNUMBER:
    case STREAM:
    case FUNCTION:
    case MONITOR:
    case OPERATOR:
    case THREAD:
    case PACKAGE:
    case CLASS:
    case TINTERFACE:
    case CHAR:
    case ENUM:
    case STRING:
    case VECTOR:
    case MAP:
    case PLUS:
    case MINUS:
    case STAR:
    case SLASH:
    case PERCENT:
    case LSHIFT:
    case RSHIFT:
    case ZRSHIFT:
    case CARET:
    case TILDE:
    case BANG:
    case EQUAL:
    case LESS:
    case GREATER:
    case LESSEQ:
    case GREATEREQ:
    case INSTANCEOF:
    case NOTEQ:
    case UMINUS:
    case SIZEOF:
    case TYPEOF:
    case ASSIGN:
    case CAST:
    case NEW:
    case NEWVECTOR:
    case DOT:
    case LBRACK:
    case LSQUARE:
    case ARROW:
    case IMPLICITPACKAGE:
    case PACKAGEASSIGN:
    case CONSTASSIGN:
    case OVERASSIGN:
    case QUESTION:
    case LOGOR:
    case LOGAND:
    case AMPERSAND:
    case BITOR:
    case INLINE:
    case TIN:
        inst = expression (node) ;
        break ;
    
    case IF:
    case WHILE:
    case SYNCHRONIZED:
    case DO:
    case FOR:
    case COMPOUND:
    case BREAK:
    case CONTINUE:
    case FOREACH:
    case SWITCH:
    case TDELETE:
    case TRY:
    case RETURN:
    case YIELD:
    case THROW:
    case ENUMBLOCK:
    case SUPERCALL:
    case MACRO:
        statement (node) ;
        break ;
    case FOREIGN:
        foreign (node) ;
        break ;

    case SEMICOLON:
        build (node->left) ;
        build (node->right) ;
        break ;
    default:
       std::cerr << node->op << "\n" ;
       throw "unknown node type" ;
    }
}

// build, and issue an expression

IFNode *CodeGenerator::expression (Node *node) {
    if (node == NULL) {
        return NULL ;
    }
    IFExpression *expr ;
    IFNode *ifnode ;

    switch (node->op) {
    case SEMICOLON:			// expression separator
        expression (node->left) ;
        ifnode = expression (node->right) ;
        break ;

    case IDENTIFIER:
        //ifnode = new IFVariable (node, node->var, node->value.integer) ;
        ifnode = variables.get (node, node->var, node->value.integer) ;
        break ;

    case ANNOTATE:
        ifnode = variables.get (node, node->var, node->value.integer) ;
        ifnode = issue (new IFExpression (node, opANNOTATE, ifnode)) ;
        break;

    case NUMBER:
        //ifnode = new IFValue (node, node->value) ;
        //ifnode = constants.get (node, node->value) ;		//XXX doesn't work
        //break ;
    case FNUMBER:
    case CHAR:
    case FUNCTION:
    case STREAM:
    case MONITOR:
    case OPERATOR:
    case THREAD:
    case PACKAGE:
    case CLASS:
    case TINTERFACE:
    case ENUM:
        ifnode = new IFValue (node, node->value) ;
        issue (ifnode) ;
        break ;

    case STRING:
        ifnode = issue (new IFValue (node, node->value)) ;
        ifnode = issue (new IFExpression (node, opCOPY, ifnode)) ;
        if (node->flags & NODE_REPLACEVARS) {
           ifnode->flags |= INST_REPLACEVARS;
        }
        break ;

    case VECTOR:
        ifnode = mkvector (node) ;
        break ;

    case MAP:
        ifnode = mkmap (node) ;
        break ;

    case PLUS:
    case MINUS:
    case STAR:
    case SLASH:
    case PERCENT:
    case LSHIFT:
    case RSHIFT:
    case ZRSHIFT:
    case CARET:
    case TILDE:
    case BANG:
    case EQUAL:
    case LESS:
    case GREATER:
    case LESSEQ:
    case GREATEREQ:
    case INSTANCEOF:
    case NOTEQ:
    case UMINUS:
    case SIZEOF:
    case TYPEOF:
    case CAST:
    case AMPERSAND:
    case BITOR: {
        if (node->flags & NODE_TYPEFIXED) {
            TokenOpTypeMap::iterator o = fixedTypes.find (node->op) ;
            if (o != fixedTypes.end()) {
                const OpTypeMap *omap = o->second ;
                OpTypeMap::const_iterator ot = omap->find (node->left->type) ;	// take left as type of operation
                if (ot != omap->end()) {
                    expr = new IFExpression (node, ot->second) ;
                    goto exprok ;
                }
            }
        }
        expr = new IFExpression (node, tokenMap[node->op]) ;
    exprok:
        expr->setOperand (0, expression (node->left)) ;
        expr->setOperand (1, expression (node->right)) ;
        ifnode = expr ;
        issue (expr) ;
        break ; 
        }

    case TIN:
        expr = new IFExpression (node, opIN) ;
        if (node->right->op == RANGE) {
            expr->setOperand (0, expression (node->left)) ;
            expr->setOperand (1, expression (node->right->left)) ;
            expr->setOperand (2, expression (node->right->right)) ;
        } else {
            expr->setOperand (0, expression (node->left)) ;
            expr->setOperand (1, expression (node->right)) ;
        }
        ifnode = expr ;
        issue (expr) ;
        break ;

    case INLINE:  {
        expr = new IFExpression (node, opINLINE) ;
        issue (expr) ;
        int oldscopelevel = scopeLevel ;
        scopeLevel = 0 ;			// don't want to pop scopes right out
        build (node->left) ;
        scopeLevel = oldscopelevel ;
        issue (new IFInstruction (node, opEND)) ;
        expr->setOperand (0, issue (new IFLabel (node))) ;
        ifnode = expr ;
        break ;
        }

    case ARROW:
        issue (new IFInstruction (node, opSTATEMENT)) ;
        expr = new IFExpression (node, opSTREAM) ;
        expr->setOperand (0, expression (node->left)) ;
        expr->setOperand (1, address (node->right)) ;
        ifnode = expr ;
        issue (expr) ;
        break ;

    case LOGOR:
        ifnode = logicalOr (node) ;
        break ;
    case LOGAND:
        ifnode = logicalAnd (node) ;
        break ;

    case ASSIGN:
    case CONSTASSIGN:
    case OVERASSIGN:
    case PACKAGEASSIGN:
        expr = store (node) ;
        ifnode = expr ;
        break ;

    case IMPLICITPACKAGE: 
        ifnode = new IFValue (node, node->pkg) ;
        issue (ifnode) ;
        break ;

    case NEW:
        ifnode = construct (node) ;
        break ;
    case NEWVECTOR:
        ifnode = newVector (node) ;
        break ;
    case DOT:
        expr = new IFExpression (node, opFINDV) ;
        expr->setOperand (0, expression (node->left)) ;
        expr->setOperand (1, issue (new IFValue (node, node->right->value))) ;	
        issue (expr) ;
        ifnode = expr ;
        break ;
    case LBRACK:
        ifnode = call (node) ;
        break ;

    case LSQUARE: {
        expr = subnode (node, opGETSUB, opGETSUB_V) ;
        expr->setOperand (0, address (node->left)) ;
        IFNode *lo, *hi ;
        if (node->right->op == COLON) {
            lo = expression (node->right->left) ;
            hi = expression (node->right->right) ;
            expr->setOperand (1, lo) ;
            expr->setOperand (2, hi) ;
        } else {
            lo = expression (node->right) ;
            expr->setOperand (1, lo) ;
        }
        issue (expr) ;
        ifnode = expr ;
        break ;
        }
    case QUESTION:
#if 0
        expr = new IFExpression (node, opMUX) ;
        expr->setOperand (0, expression (node->left)) ;
        expr->setOperand (1, expression (node->right->left)) ;
        expr->setOperand (2, expression (node->right->right)) ;
        issue (expr) ;
#endif
        ifnode = conditional (node) ;
        break ;

    default:
        throw "unknown expression node";
    }
    return ifnode ;
}

//
// build code for a store to a variable
//

IFExpression *CodeGenerator::store (Node *node, Node *left, IFNode *value) {
    IFExpression *expr ;
    switch (left->op) {
    case IDENTIFIER:
        if (node->op == PACKAGEASSIGN) {
            expr = new IFExpression (node, opPACKAGEASSIGN) ;
        } else if (node->op == CONSTASSIGN) {
            expr = new IFExpression (node, opMOVC) ;
        } else if (node->op == OVERASSIGN) {
            expr = new IFExpression (node, opMOVO) ;
        } else {
            if (value->opcode == opMOV || value->opcode == opMOVC || value->opcode == opMOVO) {
                expr = new IFExpression (node, opMOV) ;
                value = ((IFInstruction*)value)->ops[0] ;             // copy result of MOV to source of this mov
            } else if (!value->isExpression() || value->dest != NULL) {
                expr = new IFExpression (node, opMOV) ;
            } else {
                value->setDest (expression (left)) ;
                value->fenode = node ;
                return static_cast<IFExpression *>(value) ;
            }
        }
        expr->setOperand (0, expression (left)) ;
        expr->setOperand (1, value) ;
        break ;
    case LSQUARE: {
        IFNode *lo, *hi ;
        if (left->right->op == COLON) {
            lo = expression (left->right->left) ;
            hi = expression (left->right->right) ;
            expr = new IFExpression (node, opSETSUB, value, address (left->left), lo, hi) ;
        } else {
            lo = expression (left->right) ;
            expr = new IFExpression (node, opSETSUB, value, address (left->left), lo) ;
        }
        }
        break ;
    case DOT: {
        expr = new IFExpression (node, opSTV) ;
        expr->setOperand (0, expression (left->left)) ;
        expr->setOperand (1, issue (new IFValue (node, left->right->value))) ;	
        expr->setOperand (2, value) ;
        break ;
        }

    case VECTOR: {
        // lhs is a vector.  Assign to each of the members of the vector.  Assign the
        // value, subscripted, to each of the vector members
        for (unsigned int i = 0 ; i < left->vec.size() ; i++) {
            IFNode *sub = issue (new IFExpression (node, opGETSUB, value, issue (new IFValue (node, i)))) ;
            expr = store (node, left->vec[i], sub) ;
        }
        return expr ;
        break ;
        }
    }
    issue (expr) ;
    return expr ;
}


IFExpression *CodeGenerator::store (Node *node) {
    if (node->op != PACKAGEASSIGN && node->op != CONSTASSIGN && 
            node->op != OVERASSIGN && node->right->op != LBRACK) {		// dont want to stop if call will stop
        issue (new IFInstruction (node, opSTATEMENT)) ;
    }
    IFNode *value = expression (node->right) ;
    return store (node, node->left, value) ;
}


IFExpression *CodeGenerator::subnode (Node *node, Opcode op, Opcode fixedop) {
    if (node->flags & NODE_TYPEFIXED) {
        bool ints = false ;
        if (node->right->op == COLON) {
            ints = node->right->left->flags & NODE_TYPEFIXED && node->right->left->type == T_INTEGER ;
            ints = ints && node->right->right->flags & NODE_TYPEFIXED && node->right->right->type == T_INTEGER ;
        } else {
            ints = node->right->flags & NODE_TYPEFIXED && node->right->type == T_INTEGER ;
        }
        if (ints && node->left->type == T_VECTOR) {
            return new IFExpression (node, fixedop) ;
        }
    }
    return new IFExpression (node, op) ;
}

// build a code sequence for getting the address of an expression
IFNode *CodeGenerator::address (Node *node) {
    IFNode *inst ;
    IFExpression *expr ;
    switch (node->op) {
    case IDENTIFIER:
        //inst = new IFVariable (node, node->var, node->value.integer) ;
        inst = variables.get(node, node->var, node->value.integer) ;
        inst = new IFExpression (node, opADDR, inst) ;
        issue (inst) ;
        break ;
    case DOT:
        expr = new IFExpression (node, opFINDA) ;
        expr->setOperand (0, expression (node->left)) ;
        //expr->setOperand (1, constants.get (node, node->right->value)) ;	
        expr->setOperand (1, issue (new IFValue (node, node->right->value))) ;	
        issue (expr) ;
        inst = expr ;
        break ;
    case LSQUARE: {
        expr = new IFExpression (node, opADDRSUB) ;
        expr->setOperand (0, address (node->left)) ;
        IFNode *lo, *hi ;
        if (node->right->op == COLON) {
            lo = expression (node->right->left) ;
            hi = expression (node->right->right) ;
            expr->setOperand (1, lo) ;
            expr->setOperand (2, hi) ;
        } else {
            lo = expression (node->right) ;
            expr->setOperand (1, lo) ;
        }
        issue (expr) ;
        inst = expr ;
        break ;
        }

    default:
        return expression (node) ;
    }
    return inst ;
}

//
// push the address of a variable onto the runtime stack
//
void CodeGenerator::pushaddress (Node *node) {
    IFNode *inst ;
    IFExpression *expr ;
    switch (node->op) {
    case IDENTIFIER:
        //inst = new IFVariable (node, node->var, node->value.integer) ;
        inst = variables.get(node, node->var, node->value.integer) ;
        inst = new IFExpression (node, opPUSHADDR, inst) ;
        issue (inst) ;
        break ;
    case DOT:
        expr = new IFExpression (node, opFINDA) ;
        expr->setOperand (0, expression (node->left)) ;
        //expr->setOperand (1, constants.get (node, node->right->value)) ;	
        expr->setOperand (1, issue (new IFValue (node, node->right->value))) ;	
        issue (new IFExpression (node, opPUSH, issue (expr))) ;
        break ;
    case LSQUARE: {
        expr = new IFExpression (node, opADDRSUB) ;
        expr->setOperand (0, address (node->left)) ;
        IFNode *lo, *hi ;
        if (node->right->op == COLON) {
            lo = expression (node->right->left) ;
            hi = expression (node->right->right) ;
            expr->setOperand (1, lo) ;
            expr->setOperand (2, hi) ;
        } else {
            lo = expression (node->right) ;
            expr->setOperand (1, lo) ;
        }
        issue (new IFExpression (node, opPUSH, issue (expr))) ;
        break ;
        }

    default:
        issue (new IFExpression (node, opPUSH, expression (node))) ;
    }
}

// the code generated always includes a PUSH for the this ptr.  This may or
// may not be valid depending on the runtime type of the function being called

IFNode *CodeGenerator::call (Node *node) {
    issue (new IFInstruction (node, opSTATEMENT)) ;
    IFNode *thisptr ;
    IFNode *func ;
    int nacts = 1 ;
    if (node->right != NULL) {
        Node *pnode = node->right;
        std::vector<Node*> &actuals = pnode->vec ;
        std::vector<Node*> actualnames;
        for (int i = actuals.size() - 1 ; i >= 0 ; i--) {
            Node *act = actuals[i] ;
            if (act->op == ACTUAL) {
                // named actual
                actualnames.push_back (act->left);
                pushaddress (act->right);
            } else {
                pushaddress (act) ;
            }
        }
        nacts = actuals.size() + 1 ;

        // now push the actual names onto the stack
        // note, these are already reversed from right to left
        for (unsigned int i = 0 ; i < actualnames.size() ; i++) {
            Node *act = actualnames[i];
            issue (new IFExpression (act, opPUSH, new IFValue (act, act->value))) ;
        }
    }

    if (node->left->op == DOT) {
        thisptr = expression (node->left->left) ;
        IFExpression *expr = new IFExpression (node, opPUSHV) ;
        expr->setOperand (0, thisptr) ;
        expr->setOperand (1, issue (new IFValue (node->left, node->left->right->value))) ;	
        func = expr ;
        issue (func) ;
    } else {
        func = expression (node->left) ;
        thisptr = issue (new IFExpression (node->left, opGETTHIS, func)) ;      // GETTHIS includes a push
    }
    IFNode *numacts = integer (node, nacts) ;
    IFNode *callinst = issue (new IFExpression (node, opCALL, numacts, func));
    if (node->flags & NODE_PASSBYNAME) {
        callinst->flags |= INST_PASSBYNAME;
    }
    return callinst;
}


//
// build the code to make a vector
//
IFNode *CodeGenerator::mkvector (Node *node) {
    for (int i = node->vec.size() - 1 ; i >= 0 ; i--) {
        Node *elem = node->vec[i] ;
        issue (new IFExpression (elem, opPUSH, expression (elem))) ;
    }
    return issue (new IFExpression (node, opMKVECTOR, integer (node, node->vec.size()))) ;
}

//
// build the code to make a map
//
IFNode *CodeGenerator::mkmap (Node *node) {
    for (int i = node->vec.size() - 1 ; i >= 0 ; i--) {
        Node *elem = node->vec[i] ;
        issue (new IFExpression (elem->right, opPUSH, expression (elem->right))) ;
        issue (new IFExpression (elem->left, opPUSH, expression (elem->left))) ;
    }
    return issue (new IFExpression (node, opMKMAP, integer (node, node->vec.size()))) ;
}


// build the code to compare a value for truth
IFNode *CodeGenerator::compareTrue (Node *node, IFNode *left) {
    switch (left->opcode) {
    case opCMPEQ:
    case opCMPNE:
    case opCMPLT:
    case opCMPLE:
    case opCMPGT:
    case opCMPGE:

    case opCMPEQ_I:
    case opCMPNE_I:
    case opCMPLT_I:
    case opCMPLE_I:
    case opCMPGT_I:
    case opCMPGE_I:
        return left ;
    }
    return issue (new IFExpression (node, opCMPNE, left, zero)) ;
}


//
// build code to construct an object
//

IFNode *CodeGenerator::construct (Node *node) {
    IFNode *thisptr ;
    IFNode *func ;
    int nacts = 1 ;
    int ntypes = 0 ;
    if (node->right != NULL) {
        Node *pnode = node->right;
        Node *deftypesnode = NULL;
        if (node->right->op == SEMICOLON) {
            // we have defined types present
            deftypesnode = node->right->left;
            pnode = node->right->right;
        }
        std::vector<Node*> &actuals = pnode->vec ;
        std::vector<Node*> actualnames;
        for (int i = actuals.size() - 1 ; i >= 0 ; i--) {
            Node *act = actuals[i] ;
            if (act->op == ACTUAL) {
                // named actual
                actualnames.push_back (act->left);
                pushaddress (act->right);
            } else {
                pushaddress (act) ;
            }
        }
        nacts = actuals.size() + 1 ;

        // now push the actual names onto the stack
        // note, these are already reversed from right to left
        for (unsigned int i = 0 ; i < actualnames.size() ; i++) {
            Node *act = actualnames[i];
            issue (new IFExpression (act, opPUSH, new IFValue (act, act->value))) ;
        }

        // now push the defined types, reversed
        if (deftypesnode != NULL) {
            std::vector<Node*> &actualtypes = deftypesnode->vec;
            ntypes = actualtypes.size();
            for (int i = actualtypes.size() - 1 ; i >= 0 ; i--) {
                Node *act = actualtypes[i] ;
                issue (new IFExpression (act, opPUSH, expression (act))) ;
            }
        }
    }
    if (node->left->op == DOT) {
        thisptr = expression (node->left->left) ;
        IFExpression *expr = new IFExpression (node, opPUSHV) ;
        expr->setOperand (0, thisptr) ;
        expr->setOperand (1, issue (new IFValue (node->left, node->left->right->value))) ;	
        func = expr ;
        issue (func) ;
    } else {
        func = expression (node->left) ;
        thisptr = issue (new IFExpression (node->left, opGETTHIS, func)) ;
    }
    IFNode *numacts = integer (node, nacts) ;
    IFNode *n;
    if (ntypes > 0) {
        IFNode *numtypes = integer (node, ntypes) ;
        n = issue (new IFExpression (node, opNEW, numacts, func, numtypes));
    } else {
        n = issue (new IFExpression (node, opNEW, numacts, func));
    }
    if (node->flags & NODE_PASSBYNAME) {
        n->flags |= INST_PASSBYNAME;
    }
    return n;

#if 0
    IFNode *func = expression (node->left) ;
    int nacts = 0 ;
    if (node->right != NULL) {
        std::vector<Node*> &actuals = node->right->vec ;
        for (int i = actuals.size() - 1 ; i >= 0 ; i--) {
            Node *act = actuals[i] ;
            pushaddress (act) ;
        }
        nacts = actuals.size() ;
    }
    IFNode *numacts = integer (node, nacts) ;
    return issue (new IFExpression (node, opNEW, numacts, func)) ;
#endif
}

//
// build the code to make a new vector
//

IFNode *CodeGenerator::newVector (Node *node) {
    int ndims = 1 ;
    Node *n = node->right ;
    std::stack<IFNode *> dimstack ;
    while (n != NULL && n->op == NEWVECTOR) {
        dimstack.push (new IFExpression (n->left, opPUSH, expression (n->left))) ;
        ndims++ ; 
        n = n->right ;
    }
    while (!dimstack.empty()) {
        issue (dimstack.top()) ;
        dimstack.pop() ;
    }
    issue (new IFExpression (node->left, opPUSH, expression (node->left))) ;
    IFNode *nd = integer (node, ndims) ;
    if (n != NULL) {
        IFNode *start = new IFLabel (node) ;
        IFNode *end = new IFLabel (node) ;
        IFNode *newvec = issue (new IFExpression (node, opNEWVECTOR, nd, start, end)) ;
        issue (start) ;
        IFNode *ct = expression (n) ;
        issue (new IFInstruction (n, opRETVAL, ct)) ;
        issue (end) ;
        return newvec ;
    } else {
        return issue (new IFExpression (node, opNEWVECTOR, nd)) ;
    }
}

IFNode *CodeGenerator::integer (Node *node, int n) {
    Value v (n) ;
    return issue (new IFValue (node, v)) ;
}

IFNode *CodeGenerator::logicalOr (Node *node) {
    IFNode *temp = issue (new IFExpression (node, opTEMP)) ;
    IFNode *endlabel = new IFLabel (node) ;
    IFNode *left = expression (node->left) ;
    issue (new IFExpression (node, opMOV, temp, left)) ;
    issue (new IFInstruction (node, opBT, left, endlabel)) ;
    IFNode *right = expression (node->right) ;
    issue (new IFExpression (node, opMOV, temp, right)) ;
    issue (endlabel) ;
    return temp ;
}

IFNode *CodeGenerator::logicalAnd (Node *node) {
    IFNode *temp = issue (new IFExpression (node, opTEMP)) ;
    IFNode *endlabel = new IFLabel (node) ;
    IFNode *left = expression (node->left) ;
    issue (new IFExpression (node, opMOV, temp, left)) ;
    issue (new IFInstruction (node, opBF, compareTrue (node, left), endlabel)) ;
    IFNode *right = expression (node->right) ;
    issue (new IFExpression (node, opMOV, temp, right)) ;
    issue (endlabel) ;
    return temp ;
}

// ?: operator
// eval cond
// CMPEQ cond, 0
// BF label1
// eval left
// mov tmp, left
// B label2
// label1:
// eval right
// mov tmp, right

IFNode *CodeGenerator::conditional (Node *node) {
    IFNode *temp = issue (new IFExpression (node, opTEMP)) ;
    IFNode *left = expression (node->left) ;
    IFNode *label1 = new IFLabel (node) ;
    IFNode *label2 = new IFLabel (node) ;
    IFNode *bra = new IFInstruction (node, opBF, compareTrue (node, left), label1) ;
    issue (bra) ;
    IFNode *right = expression (node->right->left) ;
    issue (new IFExpression (node, opMOV, temp, right)) ;
    issue (new IFInstruction (node, opB, label2)) ;
    issue (label1) ;
    right = expression (node->right->right) ;
    issue (new IFExpression (node, opMOV, temp, right)) ;
    issue (label2) ;
    return temp ;
}

// build and issue a statement
void CodeGenerator::statement (Node *node) {
    if (node == NULL) {
        return ;
    }
    switch (node->op) {
    case IF:
        buildIf (node) ;
        break ;
    case WHILE:
        buildWhile (node) ;
        break ;
    case DO:
        buildDo (node) ;
        break ;
    case SYNCHRONIZED:
        buildSynchronized (node) ;
        break ;
    case FOR:
        buildFor (node) ;
        break ;
    case COMPOUND:
        buildCompound (node) ;
        break ;
    case BREAK:
        buildBreak (node) ;
        break ;
    case CONTINUE:
        buildContinue (node) ;
        break ;
    case FOREACH:
        buildForeach (node) ;
        break ;
    case SWITCH:
        buildSwitch (node) ;
        break ;
    case TDELETE:
        buildDelete (node) ;
        break ;
    case TRY:
        buildTry (node) ;
        break ;
    case RETURN:
        buildReturn (node) ;
        break ;
    case YIELD:
        buildYield (node) ;
        break ;
    case THROW:
        buildThrow (node) ;
        break ;
    case MACRO:
        buildMacro (node) ;
        break ;
    case SUPERCALL:
        supercall (node) ;
        break ;
    case ENUMBLOCK:
        buildEnum (node) ;
        break ;
    }
}

// if...else
//
// condition
// BF elselabel
//   code for if
// B endlabel
// elselabel:
//    code for else
// endlabel:
//
// if (no else)
//
// condition
// BF label
//    code for if
// label:


void CodeGenerator::buildIf (Node *node) {
    issue (new IFInstruction (node, opSTATEMENT)) ;

    IFNode *cond = expression (node->left) ;

    if (node->right != NULL) {
        if (node->right->op == ELSE) {
            IFNode *label1 = new IFLabel (node) ;
            IFNode *label2 = new IFLabel (node) ;
            IFNode *bra = new IFInstruction (node, opBF, compareTrue (node, cond), label1) ;
            issue (bra) ;
            build (node->right->left) ;
            issue (new IFInstruction (node, opB, label2)) ;
            issue (label1) ;
            build (node->right->right) ;
            issue (label2) ;
        } else {
            IFNode *label = new IFLabel (node) ;
            IFNode *bra = new IFInstruction (node, opBF, compareTrue (node, cond), label) ;
            issue (bra) ;
            build (node->right) ;
            issue (label) ;
        }
    }
}

// looplabel:
// condition
// BF endlabel
//   code for while
// B looplabel
// endlabel:

void CodeGenerator::buildWhile (Node *node) {
    IFNode *looplabel = new IFLabel (node) ;
    IFNode *endlabel = new IFLabel (node) ;
    continueLabels.push (looplabel) ;
    breakLabels.push (endlabel) ;
    breakscopes.push (makeScopeLevel()) ;
    continuescopes.push (makeScopeLevel()) ;
    issue (looplabel) ;
    issue (new IFInstruction (node, opSTATEMENT)) ;
    IFNode *cond = expression (node->left) ;
    issue (new IFInstruction (node, opBF, compareTrue (node, cond), endlabel)) ;
    build (node->right) ;
    issue (new IFInstruction (node, opB, looplabel)) ;
    issue (endlabel) ;
    breakLabels.pop() ;
    continueLabels.pop() ;
    breakscopes.pop() ;
    continuescopes.pop() ;
}

// looplabel:
//    code for statement
//    condition
//    BT looplabel
// endlabel:

void CodeGenerator::buildDo (Node *node) {
    IFNode *looplabel = new IFLabel (node) ;
    IFNode *endlabel = new IFLabel (node) ;
    continueLabels.push (looplabel) ;
    breakLabels.push (endlabel) ;
    breakscopes.push (makeScopeLevel()) ;
    continuescopes.push (makeScopeLevel()) ;
    issue (looplabel) ;
    issue (new IFInstruction (node, opSTATEMENT)) ;
    build (node->right) ;
    IFNode *cond = expression (node->left) ;
    issue (new IFInstruction (node, opBT, compareTrue (node, cond), looplabel)) ;
    issue (endlabel) ;
    breakLabels.pop() ;
    continueLabels.pop() ;
    breakscopes.pop() ;
    continuescopes.pop() ;
}

void CodeGenerator::buildFor (Node *node) {
    IFNode *looplabel = new IFLabel (node) ;
    IFNode *endlabel = new IFLabel (node) ;
    IFNode *contlabel = new IFLabel (node) ;
    continueLabels.push (contlabel) ;
    breakLabels.push (endlabel) ;
    breakscopes.push (makeScopeLevel()) ;
    continuescopes.push (makeScopeLevel()) ;
    Node *e1 = node->left->left->left ;
    Node *e2 = node->left->left->right ;
    Node *e3 = node->left->right ;
    issue (new IFInstruction (node, opSTATEMENT)) ;
    expression (e1) ;
    issue (looplabel) ;
    IFNode *cond = NULL ;
    if (e2 != NULL) {
        cond = expression (e2) ;
        issue (new IFInstruction (node, opBF, compareTrue (node, cond), endlabel)) ;
    }
    build (node->right) ;
    issue (contlabel) ;
    if (e3 != NULL) {
        expression (e3) ;
    }
    issue (new IFInstruction (node, opB, looplabel)) ;
    issue (endlabel) ;
    breakLabels.pop() ;
    continueLabels.pop() ;
    breakscopes.pop() ;
    continuescopes.pop() ;
}

void CodeGenerator::buildSwitch (Node *node) {
    IFNode *endlabel = new IFLabel (node) ;
    breakLabels.push (endlabel) ;
    issue (new IFInstruction (node, opSTATEMENT)) ;
    IFNode *cond = expression (node->left) ;
    std::vector<IFNode *> caseLabels ;
    IFNode *defaultLabel = NULL ;
    breakscopes.push (makeScopeLevel()) ;

    // build set of comparisons for each case and finally branch to default
    Node *defnode = NULL ;
    for (int i = 0 ; i < node->vec.size() ; i++) {
        Node *n = node->vec[i] ;
        if (n->op == CASE) {
            Opcode comp ;
            switch (n->value.integer) {
            case LESS:
                comp = opCMPLT ;
                break ;
            case GREATER:
                comp = opCMPGT ;
                break ;
            case LESSEQ:
                comp = opCMPLE ;
                break ;
            case GREATEREQ:
                comp = opCMPGT ;
                break ;
            case EQUAL:
                comp = opCMPEQ ;
                break ;
            case NOTEQ:
                comp = opCMPNE ;
                break ;
            case TIN:
                comp = opIN ;   
                break ;
            }
            IFNode *caselabel = new IFLabel (n) ;
            caseLabels.push_back (caselabel) ;
            IFNode *cmp ;
            if (comp == opIN) {
                IFExpression *expr = new IFExpression (node, opIN) ;
                if (n->left->op == RANGE) {
                    expr->setOperand (0, cond) ;
                    expr->setOperand (1, expression (n->left->left)) ;
                    expr->setOperand (2, expression (n->left->right)) ;
                } else {
                    expr->setOperand (0, cond) ;
                    expr->setOperand (1, expression (n->left)) ;
                }
                cmp = issue (expr) ;
            } else {
                IFNode *casecond = expression (n->left) ;
                cmp = issue (new IFExpression (n, comp, cond, casecond)) ;
            }
            cmp->fenode = node ;
            issue (new IFInstruction (n, opBT, cmp, caselabel)) ;
        } else {
            defnode = n ;
            caseLabels.push_back (NULL) ;	// need a gap here
        }
    }
    if (defnode != NULL) {
        defaultLabel = new IFLabel (defnode) ;
        issue (new IFInstruction (defnode, opB, defaultLabel))->fenode = node ;
    } else {
        issue (new IFInstruction (node, opB, endlabel))->fenode = node ;
    }

    // now output the code for each case and default
    for (int i = 0 ; i < node->vec.size() ; i++) {
        Node *n = node->vec[i] ;
        if (n->op == CASE) {
            issue (caseLabels[i]) ;
            build (n->right) ;
        } else {
            issue (defaultLabel) ;
            build (n->left) ;
        }
    }

    issue (endlabel) ;
    breakLabels.pop() ;
    breakscopes.pop() ;

}

// the loop is generated as:
//     FOREACH var, val, endlabel [, endsrc]
//       loop body
// continuelabel:
//     NEXT		// at end of loop will go to endlabel
// breaklabel:		// break will go to here
// endlabel:

void CodeGenerator::buildForeach (Node *node) {
    IFNode *looplabel = new IFLabel (node) ;
    IFNode *endlabel = new IFLabel (node) ;
    IFNode *breaklabel = new IFLabel (node) ;
    continueLabels.push (looplabel) ;
    breakLabels.push (breaklabel) ;
    breakscopes.push (makeScopeLevel()) ;
    continuescopes.push (makeScopeLevel()) ;

    IFNode *var = variables.get (node, node->left->left->var, node->left->left->value.integer) ;
    IFNode *expr ;
    IFNode *endexpr = NULL ;
    if (node->left->right->op == RANGE) {
        expr = expression (node->left->right->left) ;
        endexpr = expression (node->left->right->right) ;
    } else {
        expr = expression (node->left->right) ;
    }
    issue (new IFInstruction (node, opSTATEMENT)) ;
    IFNode *foreach = issue (new IFInstruction (node, opFOREACH, var, expr, endlabel, endexpr)) ;

    foreachlevel++ ;
    build (node->right) ;   
    foreachlevel-- ;

    issue (looplabel) ;
    issue (new IFInstruction (node, opNEXT)) ;		// continue goes to here
    issue (breaklabel) ;				// break goes to here
    issue (endlabel) ;
    breakLabels.pop() ;
    continueLabels.pop() ;
    breakscopes.pop() ;
    continuescopes.pop() ;
}


//
// a compound statement (enclosed in {})
//
void CodeGenerator::buildCompound (Node *node) {
    scopeLevel++ ;
    Value s ;
    s.ptr = (void*)node->scope ;
    issue (new IFInstruction (node, opPUSHSCOPE, issue (new IFValue (node, s)))) ;
    Scope::VarMap::iterator v ;
    if (node->scope != NULL) {
        for (v = node->scope->variables.begin() ; v != node->scope->variables.end() ; v++) {
            Variable *var = (*v).second ;
            if (var->flags & VAR_NEEDINIT) {
                Value none ;
                issue (new IFExpression (node, opMOVF, variables.get (node, var, 0), issue (new IFValue (node, none)))) ;
            }
        }
    }
    build (node->left) ;
    issue (new IFInstruction (node, opPOPSCOPE, integer (node, 1), zero, zero)) ;
    scopeLevel-- ;
}

//
// break
//

void CodeGenerator::buildBreak (Node *node) {
    issue (new IFInstruction (node, opSTATEMENT)) ;
    if (!breakscopes.empty()) {
        int l = breakscopes.top() ;
        int n1 = scopeLevel - getScopeLevel(l) ;
        int n2 = trylevel - getTryLevel(l) ;
        int n3 = foreachlevel - getForeachLevel(l) ;
        issue (new IFInstruction (node, opPOPSCOPE, integer (node, n1), integer (node, n2), integer (node, n3))) ;
        int n4 = monitorlevel - getMonitorLevel(l) ;
        std::deque<IFNode*>::reverse_iterator r = monitorStack.rbegin() ;
        while (n4 > 0) {
             issue (new IFInstruction (node, opMONITOREXIT, *r)) ;
             n4-- ; r++ ;
        }
    }
    IFNode *label = breakLabels.top() ;
    issue (new IFInstruction (node, opB, label)) ;
}

//
// continue
//
// NB. don't pop any foreach 

void CodeGenerator::buildContinue (Node *node) {
    issue (new IFInstruction (node, opSTATEMENT)) ;
    if (!continuescopes.empty()) {
        int l = continuescopes.top() ;
        int n1 = scopeLevel - getScopeLevel(l) ;
        int n2 = trylevel - getTryLevel(l) ;
        issue (new IFInstruction (node, opPOPSCOPE, integer (node, n1), integer (node, n2), zero)) ;
        int n4 = monitorlevel - getMonitorLevel(l) ;
        std::deque<IFNode*>::reverse_iterator r = monitorStack.rbegin() ;
        while (n4 > 0) {
             issue (new IFInstruction (node, opMONITOREXIT, *r)) ;
             n4-- ; r++ ;
        }
    }
    IFNode *label = continueLabels.top() ;
    issue (new IFInstruction (node, opB, label)) ;
}

//
// return
//
void CodeGenerator::buildReturn (Node *node) {
    issue (new IFInstruction (node, opSTATEMENT)) ;
    IFNode *val = NULL ;                // value to return

    // expression must be executed inside the current scope.  It might throw an
    // exception
    if (node->left != NULL) {           // expression to return?
        val = expression (node->left) ;
        if (node->right != NULL) {		// any return type?
            IFNode *t = expression (node->right) ;
            val = issue (new IFExpression (node, opCAST, t, val)) ;
        }
    }

    if (scopeLevel > 0 || trylevel > 0 || foreachlevel > 0) {
        issue (new IFInstruction (node, opPOPSCOPE, integer (node, scopeLevel), integer (node, trylevel), integer (node, foreachlevel))) ;
    }
    if (monitorlevel > 0) {
        std::deque<IFNode*>::reverse_iterator r = monitorStack.rbegin() ;
        while (r != monitorStack.rend()) {
             issue (new IFInstruction (node, opMONITOREXIT, *r)) ;
             r++ ;
        }
    }
    if (node->left == NULL) {
        issue (new IFInstruction (node, opRET)) ;
    } else {
        if (node->left->flags & NODE_TYPEFIXED) {
            switch (node->left->type) {
            case T_INTEGER:
                issue (new IFInstruction (node, opRETVAL_I, val)) ;
                return ;
            }
        }
        issue (new IFInstruction (node, opRETVAL, val)) ;
    }
}

//
// yield
//

void CodeGenerator::buildYield (Node *node) {
    issue (new IFInstruction (node, opSTATEMENT)) ;
    IFNode *val = NULL ;                // value to return

    // expression must be executed inside the current scope.  It might throw an
    // exception
    if (node->left != NULL) {           // expression to return?
        val = expression (node->left) ;
        if (node->right != NULL) {		// any return type?
            IFNode *t = expression (node->right) ;
            val = issue (new IFExpression (node, opCAST, t, val)) ;
        }
    }

    // no POPSCOPE here because YIELD must restore to the same scope

    if (monitorlevel > 0) {
        std::deque<IFNode*>::reverse_iterator r = monitorStack.rbegin() ;
        while (r != monitorStack.rend()) {
             issue (new IFInstruction (node, opMONITOREXIT, *r)) ;
             r++ ;
        }
    }
    if (node->left == NULL) {
        issue (new IFInstruction (node, opYIELD)) ;
    } else {
        issue (new IFInstruction (node, opYIELDVAL, val)) ;
    }

    // if we in a synchronized block, enter the monitor again
    if (monitorlevel > 0) {
        std::deque<IFNode*>::iterator r = monitorStack.begin() ;
        while (r != monitorStack.end()) {
             issue (new IFInstruction (node, opMONITORENTER, *r)) ;
             r++ ;
        }
    }
}

//
// delete
//
void CodeGenerator::buildDelete (Node *node) {
    issue (new IFInstruction (node, opSTATEMENT)) ;
    if (node->left->op == LSQUARE) {
        IFNode *lo, *hi ;
        if (node->left->right->op == COLON) {
            lo = expression (node->left->right->left) ;
            hi = expression (node->left->right->right) ;
            issue (new IFExpression (node, opDELSUB, address (node->left->left), lo, hi)) ;
        } else {
            lo = expression (node->left->right) ;
            issue (new IFExpression (node, opDELSUB, address (node->left->left), lo)) ;
        }
    } else {
        issue (new IFInstruction (node, opDELETE, address (node->left))) ;
    }
}

// 
// throw <expression>
//

void CodeGenerator::buildThrow (Node *node) {
    issue (new IFInstruction (node, opSTATEMENT)) ;
    int n = monitorlevel - trylevel ;
    if (n > 0) {
        std::deque<IFNode*>::reverse_iterator r = monitorStack.rbegin() ;
        while (n > 0) {
             issue (new IFInstruction (node, opMONITOREXIT, *r)) ;
             n-- ; r++ ;
        }
    }
    IFNode *val = expression (node->left) ;
    issue (new IFInstruction (node, opTHROW, val)) ;
}

//
// TRY catchlabel, endlabel, finallylabel
//    try block
// END 
// catchlabel:
// CATCH catchvar
//    catch code
// endlabel:
// 

void CodeGenerator::buildTry (Node *node) {
    IFNode *catchlabel = new IFLabel (node) ;
    IFNode *finallylabel = NULL;
    IFNode *endlabel = new IFLabel (node) ;
    if (node->right->op != CATCH) {
        // finally present
        finallylabel = new IFLabel (node) ;
        issue (new IFInstruction (node, opTRY, catchlabel, endlabel, finallylabel)) ;
    } else {
        issue (new IFInstruction (node, opTRY, catchlabel, endlabel)) ;
    }
    trylevel++ ;
    build (node->left) ;
    issue (new IFInstruction (node, opEND)) ;
    trylevel-- ;
    if (node->right->op == CATCH) {
        // no finally
        issue (catchlabel) ;
        Node *catchnode = node->right ;
        //IFNode *catchvar = issue (new IFVariable (node, catchnode->left->var, catchnode->left->value.integer)) ;
        IFNode *catchvar = variables.get (node, catchnode->left->var, catchnode->left->value.integer) ;
        issue (new IFInstruction (node, opCATCH, catchvar)) ;
        build (catchnode->right) ;
    } else if (node->right->op == FINALLY) {
        // no catch
        Node *finallynode = node->right->left ;
        issue (finallylabel);
        build (finallynode);
        issue (new IFInstruction (node, opEND)) ;
        issue (catchlabel) ;
    } else {
        // both catch and finally
        issue (catchlabel) ;
        Node *catchnode = node->right->left ;
        Node *finallynode = node->right->right ;
        //IFNode *catchvar = issue (new IFVariable (node, catchnode->left->var, catchnode->left->value.integer)) ;
        IFNode *catchvar = variables.get (node, catchnode->left->var, catchnode->left->value.integer) ;
        issue (new IFInstruction (node, opCATCH, catchvar)) ;
        build (catchnode->right) ;
        issue (new IFInstruction (node, opEND)) ;

        issue (finallylabel);   
        build (finallynode);
        issue (new IFInstruction (node, opEND)) ;
    }
    issue (endlabel) ;
}

// because enums can be extended they need to be in separate block.  This block
// is executed in the same stack frame as the ENUMBLOCK node
//
// The left node is an identifier node containing the variable whose value is the Enum

void CodeGenerator::buildEnum (Node *node) {
    IFNode *en = expression (node->left) ;
    issue (new IFInstruction (node, opENUM, en)) ;
}


// MACRO macro, endlabel
//    code for macro
//    END
// endlabel:

void CodeGenerator::buildMacro (Node *node) {
    Value m ;
    m.ptr = node->value.ptr ;		// macro pointer
    m.type = T_NONE ;
    IFNode *endlabel = new IFLabel (node) ;
    issue (new IFInstruction (node, opMACRO, issue (new IFValue (node, m)), endlabel)) ;
    
    build (node->left) ;
    issue (new IFInstruction (node, opEND)) ;
    issue (endlabel) ;
}


void CodeGenerator::buildSynchronized (Node *node) {
    IFNode *expr = expression (node->left) ;
    monitorStack.push_back (expr) ;
    issue (new IFInstruction (node->right, opMONITORENTER, expr)) ;
    monitorlevel++ ;
    build (node->right) ;
    monitorlevel-- ;
    issue (new IFInstruction (node->right, opMONITOREXIT, expr)) ;
    monitorStack.pop_back() ;
}

void CodeGenerator::foreign (Node *node) {
    Value s (new StdStream (node->stream)) ;
    IFNode *str = issue (new IFValue (node, s)) ;
    issue (new IFInstruction (node, opFOREIGN, str)) ;
}

// supercall is not passed this pointer as it is already set by NEW operator in stack[0]

void CodeGenerator::supercall (Node *node) {
    IFNode *block = expression (node->left) ;
    int nacts = 0 ;
    int ntypes = 0 ;
    if (node->right != NULL) {
        Node *pnode = node->right;
        Node *deftypesnode = NULL;
        if (node->right->op == SEMICOLON) {
            // we have defined types present
            deftypesnode = node->right->left;
            pnode = node->right->right;
        }
        std::vector<Node*> &actuals = pnode->vec ;

        for (int i = actuals.size() - 1 ; i >= 0 ; i--) {
            Node *act = actuals[i] ;
            pushaddress (act) ;
        }
        nacts = actuals.size() ;

        // now push the defined types, reversed
        if (deftypesnode != NULL) {
            std::vector<Node*> &actualtypes = deftypesnode->vec;
            ntypes = actualtypes.size();
            for (int i = actualtypes.size() - 1 ; i >= 0 ; i--) {
                Node *act = actualtypes[i] ;
                issue (new IFExpression (act, opPUSH, expression (act))) ;
            }
        }

    }
    IFNode *numacts = integer (node, nacts) ;
    if (ntypes > 0) {
        IFNode *numtypes = integer (node, ntypes) ;
        issue (new IFInstruction (node, opSUPERCALL, numacts, block, numtypes)) ;
    } else {
        issue (new IFInstruction (node, opSUPERCALL, numacts, block)) ;
    }
}


//
// main instruction issuer.  Simply adds to the end of the instruction list
//

IFNode *CodeGenerator::issue (IFNode *i) {
    if (i->next != NULL || i->prev != NULL) {
        throw "Node has already been issued" ;
    }
    if (code == NULL) {
        code = lastInstruction = i ;
    } else {
        lastInstruction->next = i ;
        i->prev = lastInstruction ;
        lastInstruction = i ;
    }
    return i ;
}


//
// allocate a register for an IF instruction
//
Register *CodeGenerator::allocateReg (IFNode *node) {
    Register *r ;
    if (regFreeList == NULL) {
        r = new Register (nregs++) ;
    } else {
        r = regFreeList ;
        regFreeList = r->next ;
        r->next = NULL ;
    }
    return r ;
}

//
// free all registers
//

void CodeGenerator::resetRegisters() {
    Register *r, *next ;
    r = regFreeList ;
    while (r != NULL) {
        next = r->next ;
        delete r ;
        r = next ;
    }
    regFreeList = NULL ;
    nregs = 0 ;
}

//
// free a single register
//

void CodeGenerator::freeReg (Register *r) {
    r->next = regFreeList ;
    regFreeList = r ;
}


//
// register allocation pass
//
void CodeGenerator::allocateRegisters() {
    IFNode *n = code ;
    while (n != NULL) {
       n->regalloc (this) ;
       n = n->next ;
    }
    
}

// code emission

CodeSequence *CodeGenerator::emit() {
    codesequence = new CodeSequence() ;
    codesequence->nregs = nregs ;
    IFNode *n = code ;
    currentAddr = 0 ;
    while (n != NULL) {
       emit (n) ;
       n = n->next ;
    }
    return codesequence ;
}

// emit a node
void CodeGenerator::emit (IFNode *node) {

    if (node->opcode == opRETVAL_I) {
        statement (dynamic_cast<IFInstruction*>(node)) ;
        return ;
    }
    if (node->opcode >= opFIRST_FIXED_TYPE && node->opcode <= opLAST_FIXED_TYPE) {
        expression (dynamic_cast<IFExpression*>(node)) ;
        return ;
    }
    switch (node->opcode) {
    case opVALUE:
    case opVARIABLE:
    case opTEMP:
        break ;
    case opLABEL:
        dynamic_cast<IFLabel*>(node)->define (codesequence, currentAddr) ;
        break ;

    case opMOV:
    case opMOVC:
    case opMOVF:
    case opMOVO:
    case opPACKAGEASSIGN:
        move (static_cast<IFExpression *>(node)) ;
        break ;

    case opLD:
    case opST:
        loadstore (dynamic_cast<IFExpression*>(node)) ;
        break ;

    case opMKVECTOR:
    case opMKMAP:
    case opCOPY:
    case opADD:
    case opSUB:
    case opMUL:
    case opDIV:
    case opMOD:
    case opSLL:
    case opSRL:
    case opSRA:
    case opOR:
    case opXOR:
    case opAND:
    case opCOMP:
    case opNOT:
    case opUMINUS:
    case opSIZEOF:
    case opTYPEOF:
    case opCAST:
    case opCMPEQ:
    case opCMPNE:
    case opCMPLT:
    case opCMPLE:
    case opCMPGT:
    case opCMPGE:
    case opCALL:
    case opNEW:
    case opNEWVECTOR:
    case opSTREAM:
    case opFINDV:
    case opPUSHV:
    case opSTV:
    case opFINDA:
    case opADDR:
    case opADDRSUB:
    case opMUX:
    case opSETSUB:
    case opDELSUB:
    case opGETSUB:
    case opGETOBJ:
    case opINLINE:
    case opINSTANCEOF:
    case opIN:
    case opANNOTATE:
        expression (dynamic_cast<IFExpression*>(node)) ;
        break ;

    case opB:
    case opBT:
    case opBF:
    case opRET:
    case opRETVAL:
    case opYIELD:
    case opYIELDVAL:
    case opTHROW:
    case opTRY:
    case opCATCH:
    case opDELETE:
    case opFOREACH:
    case opSUPERCALL:
    case opPUSH:
    case opPUSHADDR:
    case opPUSHSCOPE:
    case opPOPSCOPE:
    case opEND:
    case opFOREIGN:
    case opNEXT:
    case opPOP:
    case opMACRO:
    case opENUM:
    case opNOP: 
    case opMONITORENTER:
    case opMONITOREXIT:
    case opGETTHIS:
        statement (dynamic_cast<IFInstruction*>(node)) ;
        break ;

    case opSTATEMENT:
        flags |= INST_STATEMENT ;
        break ;

    default:
        throw "Unknown instruction opcode" ;
    }
}

//
// emit code for an expression
//

void CodeGenerator::expression (IFExpression *ex) {
    Instruction inst (ex->fenode, ex->opcode) ;
    setDest (inst, ex) ;

    // handle fixed type instructions
    if (ex->opcode >= opFIRST_FIXED_TYPE && ex->opcode <= opLAST_FIXED_TYPE) {
        inst.setOperand (0, operand (ex->ops[0])) ;
        if (ex->ops[1] != NULL) {
            inst.setOperand (1, operand (ex->ops[1])) ;
        }
        issue (inst) ;
        return ;
    }
    switch (ex->opcode) {
    // no operand instructions
    case opTEMP:
        break ;

    // unary instructions
    case opMKVECTOR:
    case opMKMAP:
    case opCOPY:
    case opCOMP:
    case opNOT:
    case opUMINUS:
    case opSIZEOF:
    case opTYPEOF:
    case opGETOBJ:
    case opADDR:
        inst.setOperand (0, operand (ex->ops[0])) ;
        if (ex->flags & INST_REPLACEVARS) {
            flags |= INST_REPLACEVARS;
        }
        break ;


    // binary instructions
    case opNEW:
    case opCALL:
        if (ex->flags & INST_PASSBYNAME) {
            flags |= INST_PASSBYNAME;
        }
        inst.setOperand (0, operand (ex->ops[0])) ;
        inst.setOperand (1, operand (ex->ops[1])) ;
        // set defined types if they exist (only for opNEW)
        if (ex->ops[2] != NULL) {
            inst.setOperand (2, operand (ex->ops[2])) ;
        }
        break;

    case opADD:
    case opSUB:
    case opMUL:
    case opDIV:
    case opMOD:
    case opSLL:
    case opSRL:
    case opSRA:
    case opOR:
    case opXOR:
    case opAND:
    case opCMPEQ:
    case opCMPNE:
    case opCMPLT:
    case opCMPLE:
    case opCMPGT:
    case opCMPGE:
    case opSTREAM:
    case opFINDV:
    case opFINDA:
    case opPUSHV:
    case opINSTANCEOF:
        inst.setOperand (0, operand (ex->ops[0])) ;
        inst.setOperand (1, operand (ex->ops[1])) ;
        break ;

    // possible 3rd argument
    case opCAST:
    case opIN:
        inst.setOperand (0, operand (ex->ops[0])) ;
        inst.setOperand (1, operand (ex->ops[1])) ;
        if (ex->ops[2] != NULL) {
            inst.setOperand (2, operand (ex->ops[2])) ;
        }
        break ;

    // ternary instructions
    case opMUX:
    case opSTV :
        inst.setOperand (0, operand (ex->ops[0])) ;
        inst.setOperand (1, operand (ex->ops[1])) ;
        inst.setOperand (2, operand (ex->ops[2])) ;
        break ;

    case opGETSUB:
    case opDELSUB:
    case opADDRSUB:
        inst.setOperand (0, operand (ex->ops[0])) ;
        inst.setOperand (1, operand (ex->ops[1])) ;
        if (ex->ops[2] != NULL) {
            inst.setOperand (2, operand (ex->ops[2])) ;
        }
        break ;

    case opSETSUB:
        inst.setOperand (0, operand (ex->ops[0])) ;
        inst.setOperand (1, operand (ex->ops[1])) ;
        inst.setOperand (2, operand (ex->ops[2])) ;
        if (ex->ops[3] != NULL) {
             inst.setOperand (3, operand (ex->ops[3])) ;
        }
        break ;

    case opINLINE: {
        IFLabel *label = dynamic_cast<IFLabel*>(ex->ops[0]) ;
        int labeladdr = label->reference(currentAddr, 0) ;
        inst.setOperand (0,  new Operand (labeladdr)) ;
        break ;
        }

    case opANNOTATE:
        inst.setOperand (0, operand (ex->ops[0])) ;
        break;

    // special needing fixup for ops 1 and 2
    case opNEWVECTOR: {
        inst.setOperand (0, operand (ex->ops[0])) ;
        if (ex->ops[1] == NULL) {				// no constructor?
            inst.setOperand (1, new Operand (0)) ;
            inst.setOperand (2, new Operand (0)) ;
        } else {
            IFLabel *ctstartlabel = dynamic_cast<IFLabel*>(ex->ops[1]) ;
            IFLabel *ctendlabel = dynamic_cast<IFLabel*>(ex->ops[2]) ;
            int labeladdr = ctstartlabel->reference(currentAddr, 1) ;
            inst.setOperand (1,  new Operand (labeladdr)) ;
            labeladdr = ctendlabel->reference(currentAddr, 2) ;
            inst.setOperand (2,  new Operand (labeladdr)) ;
        }
        break ;
        }
    }
    issue (inst) ;
}

//
// issue code for a statement
//

void CodeGenerator::statement (IFInstruction *i) {
    Instruction inst (i->fenode, i->opcode) ;
    switch (i->opcode) {
    case opB: {
        IFLabel *label = dynamic_cast<IFLabel*>(i->ops[0]) ;
        int labeladdr = label->reference(currentAddr, 0) ;
        inst.setOperand (0,  new Operand (labeladdr)) ;
        break ;
        }

    case opBT: 
    case opBF: {
        inst.setOperand (0, operand (i->ops[0])) ;
        IFLabel *label = dynamic_cast<IFLabel*>(i->ops[1]) ;
        int labeladdr = label->reference(currentAddr, 1) ;
        inst.setOperand (1,  new Operand (labeladdr)) ;
        break ;
        }

    case opMACRO: {
        inst.setOperand (0, operand (i->ops[0])) ;
        IFLabel *label = dynamic_cast<IFLabel*>(i->ops[1]) ;
        int labeladdr = label->reference(currentAddr, 1) ;
        inst.setOperand (1,  new Operand (labeladdr)) ;
        break ;
        }
        
    case opRET:
    case opYIELD:
        break ;

    case opGETTHIS:
    case opRETVAL:
    case opYIELDVAL:
    case opRETVAL_I:
    case opTHROW:
    case opDELETE:
    case opCATCH:
    case opPUSH:
    case opPUSHADDR:
    case opENUM:
    case opMONITORENTER:
    case opMONITOREXIT:
        inst.setOperand (0, operand (i->ops[0])) ;
        break ;

    case opTRY: {
        IFLabel *catchlabel = dynamic_cast<IFLabel*>(i->ops[0]) ;
        IFLabel *endlabel = dynamic_cast<IFLabel*>(i->ops[1]) ;
        int labeladdr = catchlabel->reference(currentAddr, 0) ;
        inst.setOperand (0,  new Operand (labeladdr)) ;
        labeladdr = endlabel->reference(currentAddr, 1) ;
        inst.setOperand (1,  new Operand (labeladdr)) ;
        if (i->ops[2] != NULL) {
            IFLabel *finallylabel = dynamic_cast<IFLabel*>(i->ops[2]) ;
            labeladdr = finallylabel->reference(currentAddr, 2) ;
            inst.setOperand (2,  new Operand (labeladdr)) ;
        }
        break ;
        }

    case opFOREACH: {
        inst.setOperand (0, operand (i->ops[0])) ;
        inst.setOperand (1, operand (i->ops[1])) ;
        IFLabel *endlabel = dynamic_cast<IFLabel*>(i->ops[2]) ;
        int labeladdr = endlabel->reference(currentAddr, 2) ;
        inst.setOperand (2,  new Operand (labeladdr)) ;
        if (i->ops[3] != NULL) {		// optional endexpr
            inst.setOperand (3, operand (i->ops[3])) ;
        }
        break ;
        }

    case opSUPERCALL:
        inst.setOperand (0, operand(i->ops[0])) ;
        inst.setOperand (1, operand(i->ops[1])) ;
        if (i->ops[2] != NULL) {
            // defined types
            inst.setOperand (2, operand(i->ops[2])) ;
        }
        break ;

    case opPUSHSCOPE:
    case opFOREIGN:
        inst.setOperand (0, operand (i->ops[0])) ;
        break ;

    case opPOPSCOPE:
        inst.setOperand (0, operand (i->ops[0])) ;		// scope level
        inst.setOperand (1, operand (i->ops[1])) ;		// try level
        inst.setOperand (2, operand (i->ops[2])) ;		// foreach level
        break ;

    case opNEXT:
    case opEND:
    case opPOP:
    case opNOP:
        break ;
    }
    issue (inst) ;
}

void CodeGenerator::loadstore (IFExpression *ex) {
    Instruction inst (ex->fenode, ex->opcode) ;
    setDest (inst, ex) ;
    inst.setOperand (0, operand (ex->ops[0])) ;
    if (ex->ops[1] != NULL) {
        inst.setOperand (1, operand (ex->ops[1])) ;
    }
    issue (inst) ;
}

void CodeGenerator::move (IFExpression *ex) {
    Instruction inst (ex->fenode, ex->opcode) ;
    inst.setDest (operand (ex->ops[0])) ;
    inst.setOperand (0, operand (ex->ops[1])) ;
    issue (inst) ;
}

void CodeGenerator::issue (Instruction &inst) {
    inst.flags = flags ;			// assign current flags
    flags = 0 ;					// reset current flags
    codesequence->code.push_back (inst) ;
    currentAddr++ ;
}

void Operand::print (std::ostream &os) {
    switch (type) {
    case tVALUE:
        switch (val.type) {
        case T_INTEGER:
            os << "#" << val.integer ;
            break ;
        case T_BOOL:
            os << "#" << (val.integer ? "true" : "false");
            break ;
        case T_REAL:
            os << "#" << val.real ;
            break ;
        case T_CHAR:
            os << "#'" << (char)val.integer << "'" ;
            break ;
        case T_BYTE:
            os << "#0x" << std::hex << val.integer << std::dec ;
            break ;
        case T_STRING:
            os << "#\"" << *val.str << "\"" ;
            break ;
        case T_VECTOR:
            os << "VECTOR" ;
            break ;
        case T_MAP:
            os << "MAP" ;
            break ;
        case T_STREAM:
            os << "STREAM" ;
            break ;
        case T_FUNCTION:
            os << "FUNCTION" ;
            break ;
        case T_CLASS:
            os << "CLASS" ;
            break ;
        case T_INTERFACE:
            os << "INTERFACE" ;
            break ;
        case T_PACKAGE:
            os << "PACKAGE" ;
            break ;
        case T_MONITOR:
            os << "MONITOR" ;
            break ;
        case T_THREAD:
            os << "THREAD" ;
            break ;
        case T_ENUM:
            os << "ENUM" ;
            break ;
        case T_ENUMCONST:
            os << "#" << val.ec->name ;
            break ;
        case T_NONE:
            os << "NONE" ;
            break ;
        case T_OBJECT:
            os << "OBJECT" ;
            break ;
        case T_ADDRESS:
            os << "ADDRESS" ;
            break ;
        }
        break ;
    case tVARIABLE: {
        Variable *var = static_cast<Variable*>(val.ptr) ;
        int level = (int)val.type - 100 ;
        os << var->name << "(" << level << ")" ;
        break ;
        }
    case tLOCALVAR: {
        Variable *var = static_cast<Variable*>(val.ptr) ;
        os << var->name ;
        break ;
        }
    case tREGISTER:
        os << "R" << val.integer ;
        break ;

    }
}

// copy constructor.  Can be called on uninitialized memory

Instruction::Instruction (const Instruction &inst) {
    // first increment refs for instruction being copied
    if (inst.dest != NULL) {
       inst.dest->ref++ ;
    }
    for (int i = 0 ; i < 4 ; i++) {
        if (inst.src[i] != NULL) {
            inst.src[i]->ref++ ;
        }
    }

    // now copy to this instruction
    dest = inst.dest ;
    for (int i = 0 ; i < 4 ; i++) {
        src[i] = inst.src[i] ;
    }
    opcode = inst.opcode ;
    flags = inst.flags ;
    source = inst.source ;
}


Instruction::~Instruction() {
    if (dest != NULL) {
        dest->ref-- ;
        if (dest->ref == 0) {
            delete dest ;
        }
        dest = NULL ;
    }
    for (int i = 0 ; i < 4 ; i++) {
        if (src[i] != NULL) {
            src[i]->ref-- ;
            if (src[i]->ref == 0) {
                delete src[i] ;
            }
        }
        src[i] = NULL ;
    }
    
}

Instruction &Instruction::operator= (const Instruction &inst) {
    // first increment refs for instruction being copied
    if (inst.dest != NULL) {
       inst.dest->ref++ ;
    }
    for (int i = 0 ; i < 4 ; i++) {
        if (inst.src[i] != NULL) {
            inst.src[i]->ref++ ;
        }
    }

    this->Instruction::~Instruction() ;		// destruct current contents

    // now copy in the operands
    dest = inst.dest ;
    for (int i = 0 ; i < 4 ; i++) {
        src[i] = inst.src[i] ;
    }
    opcode = inst.opcode ;
    flags = inst.flags ;
    source = inst.source ;
    return *this ;
}

void Instruction::setDest (Operand *d) {
    if (dest != NULL) {
        dest->ref-- ;
        if (dest->ref == 0) {
            delete dest ;
        }
    }
    dest = d ;
    d->ref++ ;
    if (d->ref > 1) {
        std::cout << "operand " << d << " has ref of " << d->ref << "\n" ;
    }
}

void Instruction::setOperand (int i, Operand *op) {
    if (src[i] != NULL) {
        src[i]->ref-- ;
        if (src[i]->ref == 0) {
            delete src[i] ;
        }
    }
    src[i] = op ;
    op->ref++ ;
}



void Instruction::print (std::ostream &os) {
    if (flags & INST_STATEMENT) {
        os << "* " ;
    } else {
        os << "  " ;
    }
    os << opnames[opcode] << "\t" ;
    if (dest != NULL) {
        dest->print (os) ;
        os << ", " ;
    }
    switch (opcode) {
    case opB:
    case opINLINE:
        os << src[0]->val.integer ;
        break ;
    case opBT:
    case opBF:
    case opMACRO:
        src[0]->print (os) ;
        os << ", " << src[1]->val.integer ;
        break ;
    case opJMPT:
        src[0]->print (os) ;
        os << ", " << src[1]->val.integer << ", " << src[2]->val.integer ;
        break ;

    case opTRY:
        os << src[0]->val.integer ;
        os << ", " << src[1]->val.integer ;
        if (src[2] != NULL) {
            os << ", " << src[2]->val.integer ;
        }
        break ;
    case opNEWVECTOR:
        src[0]->print (os) ;
        os << ", " << src[1]->val.integer ;
        os << ", " << src[2]->val.integer ;
        break ;
    case opFOREACH:
        src[0]->print (os) ;
        os << ", " ;
        src[1]->print (os) ;
        os << ", " << src[2]->val.integer ;
        if (src[3] != NULL) {
           os << ", "  ;
           src[3]->print (os) ;
        }
        break ;
    case opFINDV:
    case opPUSHV:
    case opFINDA: {
        src[0]->print (os) ;
        os << ", " ;
        VariableName *var = static_cast<VariableName*>(src[1]->val.ptr) ;
        os << var->name ;
        break ;
        }
    case opSTV: {
        src[0]->print (os) ;
        os << ", " ;
        VariableName *var = static_cast<VariableName*>(src[1]->val.ptr) ;
        os << var->name ;
        os << ", " ;
        src[2]->print (os) ;
        break ;
        }
    case opCALLJNI: {
#ifdef JAVA
        src[0]->print (os) ;
        os << ", " ;
        java::Method *method = static_cast<java::Method*>(src[1]->val.ptr) ;
        os << method->name->bytes ;
#endif
        break ;
        }
    default:
        for (int i = 0 ; i < 4 ; i++) {
            if (src[i] != NULL) {
                src[i]->print (os) ;
                if (i < 3 && src[i+1] != NULL) {
                    os << ", " ;
                }
            }
        }
    }
    os << "\n" ;
}

void Instruction::fixup (int addr, int oper) {
    src[oper]->val.integer = addr ;
}


CodeSequence::CodeSequence() : code (10), nregs(0) {
   code.clear() ;
}

void CodeSequence::print (std::ostream &os) {
    for (int i = 0 ; i < code.size() ; i++) {
        os << "\t" << std::dec << i << "\t"  ;
        code[i].print(os) ;
    }
}


// relocate all the code starting at offset start and moving it forward by
// 'offset' locations.  Thus creating a hold at the 'start' location
// we assume the we have exclusive access to the code sequence

void CodeSequence::relocate (int start, int offset, bool movecode) {
    int index = code.size() - 1 ;				// current index
    if (movecode) {
        code.resize (code.size() + offset) ;			// expand the space for the code
    }
    while (index >= start) {
        Instruction *from, *to ;
        from = &code[index] ;
        if (movecode) {
            to = &code[index + offset] ;
            *to = *from ;
        } else {
            to = from ;
        }

        // relocate the instructions that take an address as an operand
        switch (to->opcode) {
        case opB:
            to->src[0]->val.integer += offset ;
            break ;
        case opBT:
        case opBF:
        case opMACRO:
            to->src[1]->val.integer += offset ;
            break ;
        case opTRY:
            to->src[0]->val.integer += offset ;
            to->src[1]->val.integer += offset ;
            if (to->src[2] != NULL) {
                to->src[2]->val.integer += offset ;
            }
            break ;
        case opNEWVECTOR:
            to->src[1]->val.integer += offset ;
            to->src[2]->val.integer += offset ;
            break ;
        case opFOREACH:
            to->src[2]->val.integer += offset ;
            break ;
        }
        index-- ;
    }

    if (movecode) {
        // fill hole with NOP instructions
        for (index = start ; index < (start + offset) ; index++) {
            code[index].opcode = opNOP ;
        }
    }
}

// append the extension to the code sequence
void CodeSequence::append (Block *myblock, Debugger *debugger, Extension *x) {
    int paralen = x->paracode->code.size() ;		// length of parameter code
    int nparas = x->paraaddrs.size() ;
    if (paralen > 0) {					// any code to insert?
#if 0
        int addroffset = codestart + nparas ;		// offset to add to para addresses
        relocate (1, nparas, true) ;			// make room for branch table
        if (debugger != NULL) {
            debugger->relocate (myblock, this, 1, code.size() - 1, nparas) ;
        }
        codestart += nparas ;				// codestart has moved

        // insert branches to new para defaults
        Instruction *s = &code[1] ;
        for (int i = nparas - 1 ; i >= 0 ; i--) {
            Instruction inst ((SourceLocation*)NULL, opB) ;
            Operand *addr = new Operand (Value (x->paraaddrs[i] + addroffset)) ;
            inst.setOperand (0, addr) ;
            *s++ = inst ;
        }

        relocate (codestart, paralen, true) ;		// make a gap for the new para defs
        if (debugger != NULL) {
            debugger->relocate (myblock, this, codestart, code.size() - 1, paralen) ;
        }
        s = &code[codestart] ;
        Instruction *p = &x->paracode->code[0] ;
        for (int i = 0 ; i < paralen ; i++) {
            *s++ = *p++ ;
        }
        codestart += x->caststart ;

        Instruction *i = &code[0] ;			// get first instruction in old code
        if (i->opcode == opNOP) {			// NOP?
            i->opcode = opB ;
        } else {
            delete i->src[0] ;				// going to change the operand
        }
        i->setOperand (0, new Operand (Value (codestart))) ;
#else
        // new code to handle pass-by-name call semantics
        // there is no branch table in this case.

        relocate (codestart, paralen, true) ;		// make a gap for the new para defs
        if (debugger != NULL) {
            debugger->relocate (myblock, this, codestart, code.size() - 1, paralen) ;
        }
        Instruction *s = &code[codestart] ;
        Instruction *p = &x->paracode->code[0] ;
        for (int i = 0 ; i < paralen ; i++) {
            *s++ = *p++ ;
        }
        codestart += x->caststart ;

#endif
    }

    // now append the extension code to the end of the old code, overwriting the
    // END at the end of the old code
    if (debugger != NULL && myblock->type != T_ENUM) {
        debugger->setFile (myblock) ;
    }
    int codesize = x->code->code.size() ;
    if (codesize > 0) {
        int oldsize = code.size() ;
        x->code->relocate (0, oldsize - 1, false) ;		// relocate the extension
        code.resize (oldsize + codesize - 1) ;			// make room for the new code
        Instruction *s = &code[oldsize - 1] ;		// last instruction in old code
        Instruction *p = &x->code->code[0] ;
        int inst = oldsize - 1 ;
        for (int i = 0 ; i < codesize ; i++) {
            *s++ = *p++ ;
            if (debugger != NULL && myblock->type != T_ENUM) {
                debugger->registerInstruction (this, inst) ;		// register instruction with debugger
            }
            inst++ ;
        }
    }
    if (x->nregs > nregs) {
        nregs = x->nregs ;
    }

    if (disassemble) {
        std::cout << "Extended code:\n" ;
        print (std::cout) ;
    }
}


void CodeSequence::registerInstructions (Debugger *debugger) {
    for (int i = 0 ; i < code.size() ; i++) {
        if (code[i].flags & INST_STATEMENT) {
            debugger->registerInstruction (this, i) ;
        }
    }
}


InstructionPtr CodeSequence::firstStatement() {
    for (int i = 0 ; i < code.size() ; i++) {
        if (code[i].flags & INST_STATEMENT) {
            return InstructionPtr (this, i) ;
        }
    }
    return InstructionPtr (this, 0) ;
}

Operand *CodeGenerator::operand (IFNode *node) {
    if (node == NULL) {
        return NULL ;
    }
    if (node->dest != NULL) {
        return operand (node->dest) ;
    } else if (node->reg != NULL) {
        return new Operand (node->reg->num) ;
    } else if (node->isA (tVALUE)) {
        return new Operand ((static_cast<IFValue*>(node))->value) ;
    } else if (node->isA (tVARIABLE)) {
        IFVariable *var = static_cast<IFVariable*>(node) ;
        if (var->level == 0) {
            return new Operand (var->var) ;		// local variable
        } else {
            return new Operand (var->var, var->level) ;
        }
    } else {
        throw "Unknown operand type" ;
    }
}

void CodeGenerator::setDest (Instruction &inst, IFExpression *ex) {
    if (ex->dest != NULL) {
        inst.setDest (operand (ex->dest)) ;
    } else {
        inst.setDest (new Operand (ex->reg->num)) ;
    }
}

Extension::~Extension() {
    delete paracode ;
    delete code ;
}


void CodeSequence::dump (Aikido *aikido, std::ostream &os) {
    //aikido->dumpWord (0x45362718, os) ;
    aikido->dumpWord (nregs, os) ;
    aikido->dumpWord (argstart, os) ;
    aikido->dumpWord (codestart, os) ;
    aikido->dumpWord (code.size(), os) ;
    for (int i = 0 ; i < code.size() ; i++) {
        code[i].dump (aikido, os) ;
    }
    //aikido->dumpWord (0x14253678, os) ;
}

void Instruction::dump (Aikido *aikido, std::ostream &os) {
    //aikido->dumpWord (0x87654321, os) ;
    aikido->dumpByte (opcode, os) ;
    if (dest == NULL) {
        aikido->dumpByte (0xff, os) ;
    } else {
        dest->dump (aikido, os) ;
    }
    switch (opcode) {
    case opB:
    case opINLINE:
        aikido->dumpWord (src[0]->val.integer, os) ;
        break ;
    case opBT:
    case opBF:
    case opMACRO:
        src[0]->dump (aikido, os) ;
        aikido->dumpWord (src[1]->val.integer, os) ;
        break ;
    case opJMPT:
        src[0]->dump (aikido, os) ;
        aikido->dumpWord (src[1]->val.integer, os) ;
        aikido->dumpWord (src[2]->val.integer, os) ;
        break ;

    case opTRY:
        aikido->dumpWord (src[0]->val.integer, os) ;
        aikido->dumpWord (src[1]->val.integer, os) ;
        if (src[2] != NULL) {
            aikido->dumpWord (src[2]->val.integer, os) ;
        }
        break ;
    case opNEWVECTOR:
        src[0]->dump (aikido, os) ;
        aikido->dumpWord (src[1]->val.integer, os) ;
        aikido->dumpWord (src[2]->val.integer, os) ;
        break ;
    case opFOREACH:
        src[0]->dump (aikido, os) ;
        src[1]->dump (aikido, os) ;
        aikido->dumpWord (src[2]->val.integer, os) ;
        if (src[3] != NULL) {
            src[3]->dump (aikido, os) ;
        } else {
            aikido->dumpByte (0xff, os) ;
        }
        break ;
    case opFINDV:
    case opPUSHV:
    case opFINDA: {
        src[0]->dump (aikido, os) ;
        VariableName *var = static_cast<VariableName*>(src[1]->val.ptr) ;
        aikido->dumpString (var->name.str, os) ;
        break ;
        }
    case opSTV: {
        src[0]->dump (aikido, os) ;
        VariableName *var = static_cast<VariableName*>(src[1]->val.ptr) ;
        aikido->dumpString (var->name.str, os) ;
        src[2]->dump (aikido, os) ;
        break ;
        }
    case opPUSHSCOPE: 
        if (src[0]->val.ptr == NULL) {
            aikido->dumpWord (0, os) ;
        } else {
            aikido->dumpWord (((Scope*)src[0]->val.ptr)->index, os) ;
        }
        break ;

    case opCALLJNI: {
        break ;
        }
    default:
        for (int i = 0 ; i < 4 ; i++) {
            if (src[i] == NULL) {
                aikido->dumpByte (0xff, os) ;
            } else {
                src[i]->dump (aikido, os) ;
            }
        }
    }
    if (source != NULL) {
        aikido->dumpWord (source->lineno, os) ;
        aikido->dumpString (source->filename, os) ;
    } else {
        aikido->dumpWord (0, os) ;
    }
    aikido->dumpWord (flags, os) ;
    //aikido->dumpWord (0x97654321, os) ;
}

void Operand::dump (Aikido *aikido, std::ostream &os) {
    aikido->dumpByte (type, os) ;
    switch (type) {
    case tUNKNOWN:
        break ;
    case tVALUE:
        aikido->dumpValue (val, os) ;
        break ;
    case tREGISTER:
        aikido->dumpWord (val.integer, os) ;
        break ;
    case tVARIABLE:
        aikido->dumpVariableRef ((Variable*)val.ptr, os) ;
        aikido->dumpWord ((int)val.type, os) ;
        break ;
    case tLOCALVAR:
        aikido->dumpVariableRef ((Variable*)val.ptr, os) ;
        break ;
    }
}

void CodeSequence::load (Aikido *aikido, std::istream &is) {
    //int magic = aikido->readWord (is) ;
    //if (magic != 0x45362718) {
       //std::cerr << "invalid code sequence magic number: " << magic << "\n" ;
    //}
    nregs = aikido->readWord (is) ;
    argstart = aikido->readWord (is) ;
    codestart = aikido->readWord (is) ;
    int ni = aikido->readWord (is) ;
    if (aikido->loadpass == 1) {
        code.resize(ni) ;
    }
    for (int i = 0 ; i < ni ; i++) {
        code[i].load (aikido, is) ;
    }
    //magic = aikido->readWord (is) ;
    //if (magic != 0x14253678) {
       //std::cerr << "invalid end code sequence magic number: " << magic << "\n" ;
    //}
}

static void readIntOperand (Operand *&op, Aikido *aikido, std::istream &is) {
    int i = aikido->readWord (is) ;
    if (aikido->loadpass == 1) {
        op = new Operand() ;
    }
    op->val.integer = i ;
}


static void readOperand (Operand *&op, Aikido *aikido, std::istream &is) {
    int opt = aikido->peekByte (is) ;
    if (opt == 0xff) {
        aikido->readByte (is) ;
        op = NULL ;
    } else {
        if (aikido->loadpass == 1) {
            op = new Operand() ;
        }
        op->load (aikido, is) ;
    }
}

void checkMagic (Aikido *aikido, const char *desc, int m, std::istream &is) {
    int magic = aikido->readWord (is) ;
    if (magic != m) {
       std::cerr << "invalid " << desc << " magic number: " << magic << " at file offset " << is.tellg() << "\n" ;
       abort() ;
    }
    
}

void Instruction::load (Aikido *aikido, std::istream &is) {
    int istart = is.tellg() ;
    //checkMagic (aikido, "instruction", 0x87654321, is) ;
    opcode = (Opcode)aikido->readByte (is) ;
    readOperand (dest, aikido, is) ;
    switch (opcode) {
    case opB:
    case opINLINE:
        readIntOperand (src[0], aikido, is) ;
        break ;
    case opBT:
    case opBF:
    case opMACRO:
        readOperand (src[0], aikido, is) ;
        readIntOperand (src[1], aikido, is) ;
        break ;
    case opJMPT:
        readOperand (src[0], aikido, is) ;
        readIntOperand (src[1], aikido, is) ;
        readIntOperand (src[2], aikido, is) ;
        break ;

    case opTRY:
        readIntOperand (src[0], aikido, is) ;
        readIntOperand (src[1], aikido, is) ;
        break ;
    case opNEWVECTOR:
        readOperand (src[0], aikido, is) ;
        readIntOperand (src[1], aikido, is) ;
        readIntOperand (src[2], aikido, is) ;
        break ;
    case opFOREACH:
        readOperand (src[0], aikido, is) ;
        readOperand (src[1], aikido, is) ;
        readIntOperand (src[2], aikido, is) ;
        readOperand (src[3], aikido, is) ;
        break ;
    case opFINDV:
    case opPUSHV:
    case opFINDA: {
        readOperand (src[0], aikido, is) ;
        std::string name = aikido->readString (is) ;
        VariableName *var = aikido->findVariableName (name) ;
        if (aikido->loadpass == 1) {
            src[1] = new Operand() ;
        }
        src[1]->val.ptr = (void*)var ;
        break ;
        }
    case opSTV: {
        readOperand (src[0], aikido, is) ;
        std::string name = aikido->readString (is) ;
        VariableName *var = aikido->findVariableName (name) ;
        if (aikido->loadpass == 1) {
            src[1] = new Operand() ;
        }
        src[1]->val.ptr = (void*)var ;
        readOperand (src[2], aikido, is) ;
        break ;
        }
    case opCALLJNI: {
        break ;
        }
    case opPUSHSCOPE: {
        int index = aikido->readWord (is) ;
        
        if (index != 0 && aikido->loadpass == 2) {
            src[0] = new Operand() ;
            src[0]->val.ptr = (void*)aikido->scopevec[index-1] ;
        }
        break ;
        }
    default:
        for (int i = 0 ; i < 4 ; i++) {
            readOperand (src[i], aikido, is) ;
        }
    }
    int l = aikido->readWord (is) ;
    if (l != 0) {
        std::string fn = aikido->readString (is) ;
        source = sourceRegistry.newSourceLocation (fn, l) ;
    }
    flags = aikido->readWord (is) ;
    //checkMagic (aikido, "end instruction", 0x97654321, is) ;
}

void Operand::load (Aikido *aikido, std::istream &is) {
    type = (OpType)aikido->readByte (is) ;
    switch (type) {
    case tUNKNOWN:
        break ;
    case tVALUE:
        val = aikido->readValue (is) ;
        if (aikido->loadpass == 1 && val.type != T_NONE) {
            std::cerr << "not NONE in val\n" ;
            abort() ;
        }
        break ;
    case tREGISTER:
        val.integer = aikido->readWord (is) ;
        break ;
    case tVARIABLE:
        val.ptr = aikido->readVariableRef (is) ;
        val.type = (Type)aikido->readWord (is) ;
        break ;
    case tLOCALVAR:
        val.ptr = aikido->readVariableRef (is) ;
        break ;
    }
}

}		// namespace aikido
}		// namespace codegen
