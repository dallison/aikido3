/*
 * zip.cc
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
 * Version:  1.13
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */



#include "aikidozip.h"
#include <stdlib.h>

#include <fcntl.h>
#include <string.h>
#include <fstream>
#include <sstream>

namespace zip {

//
// this class is an entry in the directory of a zip file
//

ZipDirEntry::ZipDirEntry( string nm, int idx, int len, int clen, bool comp) {
    this->name = nm ;
    this->index = idx ;
    datastart = 0 ;
    this->length = len ;
    this->clength = clen ;
    this->compressed = comp ;
}

ZipDirEntry::~ZipDirEntry() {
}

ZipFile::ZipFile (string filename) {
    int flags = 0 ;
    in = new std::ifstream (filename.c_str());
    if (!(*in)) {
        throw "Unable to open file" ;
    }
    readDirectory() ;
}

ZipFile::ZipFile (void *data, int length) {
    int flags = 0 ;
    in = new std::stringstream ();
    std::stringstream *sin = (std::stringstream*)in;
    char *cdata = (char*)data;
    for (int i = 0 ; i < length ; i++) {
        sin->put (cdata[i]);
    }
    readDirectory() ;
}

ZipFile::~ZipFile() {
   delete in;
}


ZipEntry *ZipFile::open (string filename) {
    ZipDirectory::iterator f ;
    f = directory.find (filename) ;
    if (f == directory.end()) {
        return NULL ;
    }
    ZipDirEntry *entry = (*f).second ;
    seek (entry->index) ;
    int localsig = read4() ;
    if (localsig != 0x04034b50) {
        throw "Invalid local file signature" ;
    }
    int v = read2() ;
    int gp = read2() ;
    int comp = read2() ;			// compression
    read4() ;				// skip times
    int crc = read4() ;                     // crc
    int csize = read4() ;                   // comp size
    int usize = read4() ;                   // uncomp size
    int flen = read2() ;                    // file name len
    int elen = read2() ;                    // extra len

    entry->datastart = entry->index + 30 + flen + elen ;			// calc real index of data

    return new ZipEntry (this, entry) ;
}


void ZipFile::list (std::vector<std::string> &files) {
    ZipDirectory::iterator f ;
    for (f = directory.begin() ; f != directory.end() ; f++) {
        ZipDirEntry *entry = f->second;
        files.push_back (entry->name);
    }
}

//
// read the zip file directory.  First we have to find it.  This means
// we have to start at the end of the file and look backwards for
// the 'end of central directory' record.  This record is variable length
// and has a min of 22 bytes.  So, we allocate a buffer of size twice we
// need and read 22 bytes from the end of the file until we can find
// a word that starts with the correct signature.
//
// The end record contains the start offset for the central directory so
// once we can find this we can seek to the start of the central directory
// and read each of the elements.
//

void ZipFile::readDirectory() {
    char buf[256] ;			// space for 2 end records
    memset (buf, 0, sizeof (buf)) ;
    seek (0, std::ios_base::end) ;	// go to end of file
    int pos, len ;
    len = pos = in->tellg() ; 			// length of file
    unsigned char *end = NULL ;
    while (end == NULL && len - pos < 0xffff) {        // end header must be in final 64K
        int count = 0xffff - (len - pos) ;
        if (count > 22) {
            count = 22 ;
        }
        memcpy (buf + count, buf, count) ;		// shift previous block
        pos -= count ;
        seek (pos) ;
        read (buf, count) ;
        for (unsigned char *bp = (unsigned char *)buf ; bp < (unsigned char*)buf + count ; bp++) {
            if (bp[0] == 0x50 && bp[1] == 0x4b && bp[2] == 0x05 && bp[3] == 0x06) {
                end = bp ;
                break ;
            }
        }
    }
    if (end == NULL) {
        throw "Unable to find end of central directory record\n" ;
    }
    int cenpos = end[16] | end[17] << 8 | end[18] << 16 | end[19] << 24 ;
    int censize = end[10] | end[11] << 8 ;
    seek (cenpos) ;
    for (int i = 0 ; i < censize ; i++) {
        int sig = read4() ;
        if (sig != 0x02014b50) {
            break ;
        }
        int v = read2() ;       		// version made by
        int v2 = read2() ;			// version needed
        int gp = read2() ;			// gp bits
        int comp = read2() ;			// compression
        read4() ;				// skip times
        int crc = read4() ;                     // crc
        int csize = read4() ;                   // comp size
        int usize = read4() ;                   // uncomp size
        int flen = read2() ;                    // file name len
        int elen = read2() ;                    // extra len
        int clen = read2() ;                    // comment len
        int disk = read2() ;                    // disk num
        int iatt = read2() ;                    // internal attr
        int eatt = read4() ;			// external attr
        int index = read4() ;                   // index of file
        char *name = new char[flen + 1] ;
        read (name, flen) ;
        name[flen] = 0 ;
        read (buf, elen) ;
        read (buf, clen) ;
        if (comp != 0 && comp != 8) {
            char buf[256] ;
            sprintf (buf, "%s: unsupported compression mode %d\n", name, comp) ;
            delete [] name ;
            throw buf ;
        } else {
            ZipDirEntry *entry = new ZipDirEntry (name, index, usize, csize, comp != 0) ;
            directory[string (name)] = entry ;
            delete [] name ;
        }
    }
}

int ZipFile::read() {
    int c = in->get() & 0xff ;
    return c ;
}

int ZipFile::read2() {
    int w ;
    w = read() ;
    w |= read() << 8 ;
    return w ;
}

int ZipFile::read4() {
    int w ;
    w = read() ;
    w |= read() << 8 ;
    w |= read() << 16 ;
    w |= read() << 24 ;
    return w ;
}

int ZipFile::read (char *buf, int len) {
    int n = 0 ;
    while (len-- > 0) {
        *buf++ = read() ;
        n++ ;
    }
    return n ;
}

int ZipFile::seek (int offset, const std::ios_base::seekdir &whence) {
    in->seekg (offset, whence) ;
    return in->tellg() ;
}

int ZipFile::seek (int offset) {
    in->seekg (offset) ;
    return in->tellg() ;
}


ZipEntry::ZipEntry (ZipFile *f, ZipDirEntry *e) {
    this->file = f ;
    this->entry = e ;
    index = entry->datastart ;
}

#if defined(_OS_WINDOWS)
__int64 ZipEntry::uniqueId() {
    return (__int64)entry ;
}
#else
long long ZipEntry::uniqueId() {
    return reinterpret_cast<long long>(entry) ;
}
#endif

//
// read a single byte
//

int ZipEntry::read() {
    if (index == entry->length) {
        return -1 ;
    }
    file->seek (index++) ;
    int c = file->read() ;
    return c ;
}

int ZipEntry::read (char *buf, int len) {
    int bytesleft = (entry->length + entry->datastart) - index ;
    if (len > bytesleft) {
        len = bytesleft ;
    }
    file->seek (index) ;
    int c = file->read (buf, len) ;
    index += len ;
    return c ;
}

#if 0
int ZipEntry::seek (int offset) {
    switch (whence) {
        case SEEK_SET:
            index = entry->datastart + offset ;
            break ;
        case SEEK_CUR:
            index += offset ;
            break ;
        case SEEK_END:
            index = entry->datastart + entry->length - offset ;
            break ;
    }    
    return index ;
}
#endif

// read the entry into a strstream
#if defined(_CC_GCC) && __GNUC__ == 2
std::strstream *ZipEntry::getStream() {
#else
std::stringstream *ZipEntry::getStream() {
#endif

    char buffer[16384] ;
#if defined(_CC_GCC) && __GNUC__ == 2
    std::strstream *s = new std::strstream ;
#else
    std::stringstream *s = new std::stringstream ;
#endif
    file->seek (entry->datastart) ;
    int remaining = entry->length ;
    while (remaining > 0) {
        int n = file->read (buffer, remaining < sizeof (buffer) ? remaining : sizeof(buffer)) ;
        if (n == 0) {
            break ;
        }
        s->write (buffer, n) ;
        remaining -= n ;
    }
    return s ;
}

}

#if 0

using namespace zip ;

main(int argc, char **argv) {
    printf ("main\n") ;
    if (argc < 2) {
        printf ("usage: zip <file>\n") ;
        exit (1) ;
    }
    ZipFile *z = new ZipFile (argv[1]) ;
    for (;;) {
        char f[256] ;
        printf ("open file: ") ;
        fflush (stdout) ;
        gets (f) ;
        ZipEntry *e = z->open (f) ;
    }
}


#endif
