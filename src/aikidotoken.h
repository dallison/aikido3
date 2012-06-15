/*
 * aikidotoken.h
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
 * Version:  1.35
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */



#ifndef __TOKEN_H
#define __TOKEN_H

namespace aikido {

// initial recognized tokens.  These start at -1000 to allow extensions
// of the +ve integers

enum Token {
    BAD = -1000,
    EOL,
    SEMICOLON,
    STAR,
    RBRACK,
    LBRACK,
    DOT,
    LSQUARE,
    RSQUARE,
    IDENTIFIER,
    NUMBER,
    FNUMBER,
    CHAR,
    STRING,
    PLUS,
    MINUS,
    SLASH,
    PERCENT,
    QUESTION,
    COLON,
    COMMA,
    LBRACE,
    RBRACE,
    TILDE,
    CARET,
    BANG,
    AMPERSAND,
    TTRUE,
    TFALSE,
    BITOR,
    LOGAND,
    LOGOR,
    LSHIFT,
    RSHIFT,
    ZRSHIFT,
    LESS,
    GREATER,
    LESSEQ,
    GREATEREQ,
    EQUAL,
    NOTEQ,
    PLUSPLUS,
    MINUSMINUS,
    ASSIGN, 
    CONSTASSIGN,
    PLUSEQ,
    MINUSEQ,
    STAREQ,
    SLASHEQ,
    PERCENTEQ,
    LSHIFTEQ,
    RSHIFTEQ,
    ZRSHIFTEQ,
    ANDEQ,
    OREQ,
    XOREQ,
    BACKTICK,
    ARROW,
    COMPOUND,		// compound statement
    INLINE, 		// inlined code block
    ANNOTATE,       // @ symbol
    
//
// reserved words
//

    MACRO,
    IF,
    ELSE,
    ELIF,
    FOR,
    FOREACH,
    WHILE,
    FUNCTION,
    SWITCH,
    CASE,
    DEFAULT,
    BREAK,
    CONTINUE,
    RETURN,
    VAR,
    SIZEOF,
    TYPEOF,
    THREAD,
    ELLIPSIS,
    TCONST,
    TRY,
    CATCH,
    FINALLY,
    THROW,
    CLASS,
    NEW,
    TDELETE,
    IMPORT,
    PACKAGE,
    PUBLIC,
    PRIVATE,
    PROTECTED,
    ENUM,
    STATIC,
    FOREIGN,		
    JAVA,
    USING,
    EXTEND,
    OPERATOR,
    KNULL,
    GENERIC,
    NATIVE,
    MONITOR,
    EXTENDS,
    CAST,
    TINTERFACE,
    IMPLEMENTS,
    DO,
    INSTANCEOF,
    SYNCHRONIZED,
    TIN,
    YIELD,

// not really tokens
    
    VECTOR,		// vector literal
    POSTINC,		// x++
    POSTDEC,		// x--
    UMINUS,		//-x
    MAP,		// map literal
    SUPERCALL,		// construct super block
    STREAM,		// stream literal
    PACKAGEASSIGN,	// assign to package default instance
    NEWVECTOR,		// new multidimensional vector
    IMPLICITPACKAGE,
    FUNCPARAS,
    DOTASSIGN,
    ARRAYASSIGN,
    REGEX,
    MEMBER,
    OVERASSIGN,		// overriding assignment (saves old value)
    BREAKPOINT,
    ENUMBLOCK,
    RANGE,		// range of expressions
    ACTUAL      // named actual
   
    
} ;

}

#endif
