/*
 * aikidozip.h
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
 * Version:  1.12
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */



#ifndef __aikidozip_h
#define __aikidozip_h

#include <map>
#include <vector>
#include <stdio.h>
#include <string>
#if defined(_CC_GCC) && __GNUC__ == 2
#include <strstream>
#else
#include <sstream>
#endif

namespace zip {

using std::string ;

class ZipEntry ;
class ZipFile ;

class ZipDirEntry {
public:
    ZipDirEntry(string name, int index, int length, int clength, bool compressed) ;
    ~ZipDirEntry() ;
    
    string name ;
    int index ;    
    int length ;
    int clength ;
    int compressed ;
    int datastart ;
} ;


//
// this class is the zip file itself
//

class ZipFile {
    friend class ZipEntry ;
    friend class ZipDirEntry ;
public:
    ZipFile (string filename) ;
    ~ZipFile() ;
    
    ZipEntry *open (string filename) ;
    void list (std::vector<std::string> &files);
    
private:
    void readDirectory() ;
    typedef std::map<string, ZipDirEntry*> ZipDirectory ;
    ZipDirectory directory ;
    int read() ;
    int read2() ;
    int read4() ;
    int read (char *buf, int len) ;
    int seek (int offset, int whence) ;
    FILE *fp ;
} ;

//
// This is a file within the zip file and exists only when the file
// is being read or otherwise accessed
//

class ZipEntry {
public:
    ZipEntry (ZipFile *file, ZipDirEntry *entry) ;
#if defined(_OS_WINDOWS)
    __int64 uniqueId() ;
#else
    long long uniqueId() ;
#endif

    int seek (int offset, int whence) ;
    int read() ;
    int read (char *buf, int len) ;
#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream *getStream() ;
#else
    std::stringstream *getStream() ;
#endif
private:
    ZipFile *file ;
    ZipDirEntry *entry ;
    int index ;                         // current index for file
} ;

}

#endif
