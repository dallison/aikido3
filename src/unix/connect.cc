/*
 * connect.cc
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
 * Version:  1.5
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */

// this file is included from value.cc

void ThreadStream::connect() {
    int inpipes[2] ;
    if (pipe (inpipes) < 0) {
        throw Exception ("Unable to create communications pipe for thread") ;
    }
    int outpipes[2] ;
    if (pipe (outpipes) < 0) {
        throw Exception ("Unable to create communications pipe for thread") ;
    }
    input = new fd_ifstream (outpipes[0]) ;
    output = new fd_ofstream (inpipes[1]) ;

    peer->lock.lock() ;
    peer->input = new fd_ifstream (inpipes[0]) ;
    peer->output = new fd_ofstream (outpipes[1]) ;


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

