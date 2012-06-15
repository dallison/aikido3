/*
 * network.cc
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
 * Version:  1.16
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */

// CHANGE LOG
//	1/21/2004	David Allison of PathScale Inc.
//		Close the socket on failure
//		Check for host lookup failure on linux



#if defined(_OS_MACOSX)
#include <sys/types.h>
#endif

#include "aikido.h"

#ifndef _OS_WINDOWS
#include <sys/types.h>

#if defined(_OS_MACOSX)
#include <netinet/in.h>
#endif

#include <sys/socket.h>
#include <errno.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

#include <iostream>
#include <fstream>

#if defined(_OS_WINDOWS)
//#include <winsock2.h>
#include <io.h>
#else
typedef int SOCKET ;
#endif


namespace aikido {

#if 0 && defined(_OS_WINDOWS)

//
// on windows it is not possible to use the regular read/write
// functions on a socket.   Prior to WinNT, a SOCKET was not
// a file handle, so the _open_osfhandle doesn't work.  To
// get round this we need to create a special streambuffer
// that writes and reads sockets instead of files
//

// output net buffer
class onetbuf : public std::streambuf {
public:
   onetbuf(SOCKET f) : s(f) {
       size = 512 ;
   }

   void allocbuf() {
       buffer = new char[size] ;
	   std::streambuf::setbuf (buffer, size) ;
       setp (buffer, buffer+size) ;
   }

   int writebuf() {
      int n = send (s, pbase(), pptr() - pbase(), 0) ;
      if (n == -1) {
          return EOF ;
      }
      setp (pbase(), epptr()) ;
      return 0 ;
   }

   virtual int overflow (int c = EOF) {
      if (pbase() == NULL) {
          allocbuf() ;
      }
      if (c == EOF) {
          return writebuf() ;
      }
      if (pptr() == epptr()) {
          if (writebuf() == EOF) {
              return EOF ;
          }
      }
      sputc (c) ;
      return c ;
   }

   virtual int sync() {
       return writebuf() ;
   }

   virtual std::streambuf *setbuf (char *p, int l) {
      if (p != NULL) {
          throw "Bad use of onetbuf::setbuf\n" ;
      }
	  return std::streambuf::setbuf (p, l) ;
   }

   void close() {
       ::close (s) ;
   }

   SOCKET s ;
   char *buffer ;
   int size ;
} ;

// input net buffer
class inetbuf : public std::streambuf {
public:
   inetbuf(SOCKET f) : s(f) {
       size = 512 ;
   }

   void allocbuf() {
       buffer = new char[size] ;
	   std::streambuf::setbuf (buffer, size) ;
   }

   int underflow() {
       if (gptr() < egptr()) {
            return *gptr() ;
       }
       if (base() == NULL) {
          allocbuf() ;
       }
       int n = recv (s, base(), egptr() - base(), 0) ;
       if (n <= 0) {
           if (n < 0) {
               xsetflags (ios::eofbit) ;
           } else {
               xsetflags (ios::failbit) ;
           }
       }
       setg (base(), base(), base() + n) ;
       if (n == 0) {
           return EOF ;
       }
       return *gptr() ;
    }

    int uflow() {
        char buf[10] ;
        int n = read (s, buf, 1) ;
       if (n <= 0) {
           if (n < 0) {
               xsetflags (ios::eofbit) ;
           } else {
               xsetflags (ios::failbit) ;
           }
       }
       if (n == 0) {
           return EOF  ;
       }
       return buf[0] ;
    }

   virtual int sync() {
       return 0 ;
   }

   virtual std::streambuf *setbuf (char *p, int l) {
      if (p != NULL) {
          throw "Bad use of inetbuf::setbuf\n" ;
      }
	  return std::streambuf::setbuf (p, l) ;
   }

   void close() {
       ::close (s) ;
   }

   int fd() { return s ; }
   SOCKET s ;
   char *buffer ;
   int size ;
} ;
 


class NetStream : public Stream {
public:
    NetStream (SOCKET is, SOCKET os) {
        output = new std::ostream (new onetbuf (os)) ;
        input = new std::istream (new inetbuf (is)) ;
    }
    std::ostream *output ;
    std::istream *input ;
    bool eof() { return input->eof() ; }
    char get() { return input->get() ; }
    int get (char *buf, int n) {  input->read (buf, n) ; return input->gcount() ; }
    void put (char c) { output->put(c) ; }
    void put (char *buf, int n) {  output->write (buf, n) ; }
    void close() { ((inetbuf*)(input->rdbuf()))->close() ; ((onetbuf*)(output->rdbuf()))->close() ; }
    void flush() { output->flush() ; }
    int seek (int offset, int whence) { throw "Illegal operation on network stream" ; }
    void rewind()  { throw "Illegal operation on network stream" ; }

    int availableChars() { return (int)input->rdbuf()->in_avail() ; }
    void readValue (Value &v) { v >> *input ; }
    void writeValue (Value &v) { *output >> v ; }

    int getInputFD() { return static_cast<std::inetbuf*>(input->rdbuf())->fd() ; }
    void setBufferSize (int size) {
        input->rdbuf()->setbuf (NULL, size) ;
        output->rdbuf()->setbuf (NULL, size) ;
    }
    std::istream &getInStream() { return *input ; }

} ;

#endif

extern "C" {

//
// open a stream to another machine over the network
//
// args:
//    1: ip address in host byte order
//    2: port number
//

AIKIDO_NATIVE (opennet) {
    int one = 1 ;
    int zero = 0 ;
    SOCKET s = socket (PF_INET, SOCK_STREAM, 0) ;		// open TCP socket
    setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, 4) ;
    setsockopt (s, SOL_SOCKET, SO_LINGER, (const char *)&zero, 4) ;

    struct sockaddr_in dest ;
    memset (&dest, 0, sizeof (dest)) ;
#if defined (_OS_MACOSX)
    dest.sin_len = sizeof (dest) ;
#endif
    dest.sin_family = AF_INET ;
    dest.sin_port = htons (paras[2].integer) ;
    int ipaddr = htonl (paras[1].integer) ;
    memcpy (&dest.sin_addr, &ipaddr, sizeof (int)) ;
    int r = connect (s, (struct sockaddr*)&dest, sizeof (dest)) ;
#if defined(_OS_WINDOWS)
    if (r == SOCKET_ERROR) {
#else
    if (r != 0) {
		close (s) ;
#endif
        throw newException (vm, stack, strerror (errno)) ;
    } 
#if 0 && defined(_OS_WINDOWS)
    return new NetStream (s, s) ;
#else
    return new PipeStream (s, dup(s)) ;
#endif
}

//
// create a server by listening on a socket
//
// args:
//    1: ip address in host byte order
//    2: port number
//    3: connection type (stream or datagram)
//
// returns:
//   integer for socket

AIKIDO_NATIVE (openserver) {
    int type ;
    switch (paras[3].integer) {
    case 2:	
       type = SOCK_STREAM ;
       break ;
    case 1:	
       type = SOCK_DGRAM ;
       break ;
    default:
        throw Exception ("No such connection type") ;
    }
    SOCKET s = socket (PF_INET, type, 0) ;
    int one = 1 ;
    int zero = 0 ;
    setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, 4) ;
    setsockopt (s, SOL_SOCKET, SO_LINGER, (const char *)&zero, 4) ;

    struct sockaddr_in dest ;
    memset (&dest, 0, sizeof (dest)) ;
#if defined (_OS_MACOSX)
    dest.sin_len = sizeof (dest) ;
#endif
    dest.sin_family = AF_INET ;
    dest.sin_port = htons (paras[2].integer) ;
    int ipaddr = htonl (paras[1].integer) ;
    memcpy (&dest.sin_addr, &ipaddr, sizeof (int)) ;
    int r = bind (s, (struct sockaddr*)&dest, sizeof (dest)) ;
    if (r != 0) {
        close (s) ;
        throw newException (vm, stack, strerror (errno)) ;
    }
    if (paras[3].integer == 2) {		// TCP?
        r = listen (s, 1) ;
        if (r != 0) {
            close (s) ;
            throw newException (vm, stack, strerror (errno)) ;
        }
    }
    return (int)s ;
}

//
// accept a connection on a socket
//
// args:
//    1: socket
//
// returns:
//   stream for new file descriptor

AIKIDO_NATIVE (accept) {
    struct sockaddr_in src ;
#if defined(_OS_LINUX)
    socklen_t len = 0 ;
    int fd = accept (paras[1].integer, (struct sockaddr*)&src, &len) ;
    if (fd == -1) {
        throw newException (vm, stack, strerror (errno)) ;
    }
#elif defined(_OS_SOLARIS) && defined(_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    socklen_t len = 0 ;
    int fd = accept (paras[1].integer, (struct sockaddr*)&src, &len) ;
    if (fd == -1) {
        throw newException (vm, stack, strerror (errno)) ;
    }
#elif defined(_OS_WINDOWS)
    int len = sizeof(src) ;
    SOCKET fd = accept (paras[1].integer, (struct sockaddr*)&src, &len) ;
    if (fd == INVALID_SOCKET) {
        throw newException (vm, stack, strerror (errno)) ;
    }
#else
    socklen_t len = 0 ;
    int fd = accept (paras[1].integer, (struct sockaddr*)&src, &len) ;
    if (fd == -1) {
        throw newException (vm, stack, strerror (errno)) ;
    }
#endif
#if 0 && defined(_OS_WINDOWS)
    return new NetStream (fd, fd) ;
#else
    return new PipeStream (fd, dup(fd)) ;
#endif
}

//
// lookup a name in the host database
//
// args:
//  1: name to look up
//
// returns:
//   ip address in network byte order
//

AIKIDO_NATIVE (lookupName) {
    struct hostent *host ;
    struct hostent result ;
    char buffer[1024] ;
    int errno ;

    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "lookupName", "Bad host name type") ;
    }

    const char *name = paras[1].str->c_str() ;
#if defined(_OS_LINUX)
    if (gethostbyname_r (name, &result, buffer, 1024, &host, &errno) < 0) {
        throw newException (vm, stack, "no such network name") ;
    }
    if (host == NULL) {
        throw newException (vm, stack, "no such network name") ;
    }
#elif defined(_OS_WINDOWS)
    host = gethostbyname (name) ;
    if (host == NULL) {
        throw newException (vm, stack, "no such network name") ;
    }
#elif defined(_OS_MACOSX)
    host = gethostbyname (name) ;
    if (host == NULL) {
        throw newException (vm, stack, "no such network name") ;
    }
#else	
    host = gethostbyname_r (name, &result, buffer, 1024, &errno) ;
    if (host == NULL) {
        throw newException (vm, stack, "no such network name") ;
    }
#endif

    char *addr = host->h_addr ;
    return htonl (*(int*)addr) ;
}

AIKIDO_NATIVE (lookupAddress) {
    struct hostent *host ;
    struct hostent result ;
    char buffer[1024] ;
    int errno ;
    int ipaddr = htonl (paras[1].integer) ;

    if (paras[1].type != T_INTEGER) {
        throw newParameterException (vm, stack, "lookupAddress", "Bad host address type") ;
    }
#if defined(_OS_LINUX)
    if (gethostbyaddr_r ((const char *)&ipaddr, sizeof (ipaddr), AF_INET, &result, buffer, 1024, &host, &errno) < 0) { 
        throw newException (vm, stack, "no such network address") ;
    }
#elif defined(_OS_WINDOWS)
    host = gethostbyaddr ((const char *)&ipaddr, sizeof (ipaddr), AF_INET) ;

    if (host == NULL) {
        throw newException (vm, stack, "no such network address") ;
    }

#elif defined(_OS_MACOSX)
    host = gethostbyaddr ((const char *)&ipaddr, sizeof (ipaddr), AF_INET) ;

    if (host == NULL) {
        throw newException (vm, stack, "no such network address") ;
    }

#else
    host = gethostbyaddr_r ((const char *)&ipaddr, sizeof (ipaddr), AF_INET, &result, buffer, 1024, &errno) ;

    if (host == NULL) {
        throw newException (vm, stack, "no such network address") ;
    }
#endif
    char *name = host->h_name ;
    return new string (name) ;
}


AIKIDO_NATIVE (openSocket) {
    int one = 1 ;
    int zero = 0 ;
    SOCKET s = socket (PF_INET, 1, 0) ;		// open UDP socket
    setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, 4) ;
    setsockopt (s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, 4) ;
    return (INTEGER)s ;
    }

AIKIDO_NATIVE (sendto) {
    SOCKET socket = (SOCKET)paras[1].integer ;
    if (paras[4].type != T_BYTEVECTOR && paras[4].type != T_STRING) {
        throw newParameterException (vm, stack, "sendto", "Illegal buffer type") ;
    }
    
    const void *msg = NULL ;
    int len = 0 ;
    if (paras[4].type == T_STRING) {
        msg = (const void*)paras[4].str->data() ;
        len = paras[4].str->size() ;
    } else {
        msg = (const void*)&(*(paras[4].bytevec->begin())) ;
        len = paras[4].bytevec->size() ;
    }
    struct sockaddr_in dest ;
    memset (&dest, 0, sizeof (dest)) ;
#if defined (_OS_MACOSX)
    dest.sin_len = sizeof (dest) ;
#endif
    dest.sin_family = AF_INET ;
    dest.sin_port = htons (paras[3].integer) ;
    int ipaddr = htonl (paras[2].integer) ;
    memcpy (&dest.sin_addr, &ipaddr, sizeof (int)) ;

#if defined(_OS_WINDOWS)
    int r = sendto (socket, (const char*)msg, len, 0, (struct sockaddr*)&dest, sizeof (dest)) ;
#else
    int r = sendto (socket, msg, len, 0, (struct sockaddr*)&dest, sizeof (dest)) ;
#endif
    if (r < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    return r ;
}

AIKIDO_NATIVE (recvfrom) {
    if (paras[2].type != T_ADDRESS) {
        throw newParameterException (vm, stack, "recvfrom", "Illegal reference type for address parameter") ;
    }
    if (paras[3].type != T_ADDRESS) {
        throw newParameterException (vm, stack, "recvfrom", "Illegal reference type for port parameter") ;
    }
    int maxb = paras[5].integer ;
    SOCKET socket = (SOCKET)paras[1].integer ;
    char *buffer = new char[maxb] ;
    struct sockaddr_in dest ;
    memset (&dest, 0, sizeof (dest)) ;
#if defined(_OS_LINUX)
    socklen_t addrlen ;

    int len = recvfrom (socket, buffer, maxb, paras[4].integer ? MSG_PEEK : 0, (struct sockaddr*)&dest, &addrlen) ;
#elif defined(_OS_SOLARIS) && defined(_CC_GCC) && __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1)
    socklen_t addrlen ;

    int len = recvfrom (socket, buffer, maxb, paras[4].integer ? MSG_PEEK : 0, (struct sockaddr*)&dest, &addrlen) ;
#elif defined(_OS_WINDOWS)
    int addrlen ;

    int len = recvfrom (socket, buffer, maxb, paras[4].integer ? MSG_PEEK : 0, (struct sockaddr*)&dest, &addrlen) ;

#else
    socklen_t addrlen ;

    int len = recvfrom (socket, buffer, maxb, paras[4].integer ? MSG_PEEK : 0, (struct sockaddr*)&dest, &addrlen) ;
#endif
    if (len < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    Value::bytevector *vec = new Value::bytevector (len) ;
    for (int i = 0 ; i < len ; i++) {
        vec->vec[i] = buffer[i] ;
    }
    delete [] buffer ;
    Value *addrptr = paras[2].addr ;
    Value *portptr = paras[3].addr ;
    int ipaddr ;
    memcpy (&ipaddr, &dest.sin_addr, sizeof (int)) ;
    *addrptr = (INTEGER)htonl (ipaddr) ;
    *portptr = (INTEGER)htons (dest.sin_port) ;
    return vec ;
}



}


}
