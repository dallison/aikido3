/*
 * connect.cc
 *
 * Aikido Language System,
 * export version: 1.00
 * Copyright (c) 2002 Sun Microsystems, Inc.
 *
 * Sun Public License Notice
 *
 * The contents of this file are subject to the Sun Public License Version
 * 1.0 (the "License"). You may not use this file except in compliance with
 * the License. A copy of the License is included as the file "license.terms",
 * and also available at http://www.sun.com/
 *
 * The Original Code is from:
 *    Aikido Language System release 1.00
 * The Initial Developer of the Original Code is: David Allison
 * Portions created by David Allison are Copyright (C) Sun Microsystems, Inc.
 * All Rights Reserved.
 *
 * Contributor(s): dallison
 *
 * Version:  1.4
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/01/24
 */

// this file is included in value.cc

#include <io.h>

void ThreadStream::connect() {

    HANDLE inpipes[2], outpipes[2] ;

   SECURITY_ATTRIBUTES saAttr; 
   BOOL fSuccess; 
 
// Set the bInheritHandle flag so pipe handles are inherited. 
 
   saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
   saAttr.bInheritHandle = TRUE; 
   saAttr.lpSecurityDescriptor = NULL; 
 
 
   if (! CreatePipe(&inpipes[0], &inpipes[1], &saAttr, 0)) 
      throw Exception ("input pipe creation failed"); 
 
 
   if (! CreatePipe(&outpipes[0], &outpipes[1], &saAttr, 0)) 
      throw Exception ("output pipe creation failed\n"); 
 
    input = new fd_ifstream (_open_osfhandle ((DWORD)inpipes[0], O_RDONLY)) ;
    output = new fd_ofstream (_open_osfhandle ((DWORD)outpipes[1], O_APPEND)) ;

    peer->lock.lock() ;
    peer->input = new fd_ifstream (_open_osfhandle((DWORD)outpipes[0], O_RDONLY)) ;
    peer->output = new fd_ofstream (_open_osfhandle ((DWORD)inpipes[1], O_APPEND)) ;


    if (thread != NULL) {
        thread->input->setValue ((Stream*)this, stack) ;
        thread->output->setValue ((Stream*)this, stack) ;
    } else if (peer->thread != NULL) {
        peer->thread->input->setValue ((Stream*)peer, peer->stack) ;
        peer->thread->output->setValue ((Stream*)peer, peer->stack) ;
    } else {
        peer->lock.unlock() ;
        throw Exception ("Thread stream is not connected to a thread") ;
    }

    peer->connected = true ;
    peer->lock.unlock() ;
    connected = true ;
}

