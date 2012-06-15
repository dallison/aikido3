/*
 * driver.cc
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
 * Version:  1.3
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/01/24
 */

#pragma ident "@(#)driver.cc	1.3 03/01/24 Aikido - SMI"


#include <sys/types.h>
#include <windows.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <iostream>
#include <string>
#include <stdio.h>


//
// extract the directory portion of a path name
//

std::string extractDirectory (const std::string &path) {
    const char *p = path.c_str() + path.size() - 1 ;
    while (*p != '\\' && p != path.c_str()) {
        p-- ;
    }
    if (p == path.c_str()) {
        return "." ;
    } else {
        int len = p - path.c_str() ;
        return path.substr (0, len) ;
    }
}

//
// find the directory from which we are running.  This is used to
// locate the system files.
//

std::string findRunDir(const char *exename) {
    struct stat st ;
    if (stat (exename, &st) == 0) {
        return extractDirectory (exename) ;
    }
    char *path = getenv("PATH") ;
    char buf[256] ;
    char pathname [256] ;
    char *p, *s ;
    p = path ;
    while (*p != 0) {
        s = buf ;
        while (*p != 0 && *p != ';') {
            *s++ = *p++ ;
        }
        *s = 0 ;
        sprintf (pathname, "%s\\%s", buf, exename) ;
        if (stat (pathname, &st) == 0) {
            return extractDirectory (pathname) ;
        }
        if (*p != 0) {
            p++ ;
        }
    }
    return "" ;
}

int __declspec(dllexport) main (int argc, char **argv) {
    try {
        bool debug = false ;
        bool tracecode = false ;
        bool processargs = true ;

        std::string args ;

        for (int i = 1 ; i < argc ; i++) {
            args += std::string (" ") + argv[i] ;		// collect all the args
            if (argv[i][0] == '-') {
                switch (argv[i][1]) {
                case '-':			// -- toggles further arg processing 
                    processargs = !processargs ;
                    break ;
                default:
                    if (processargs && strcmp (argv[i], "-debug") == 0) {
                        debug = true ;
                        continue ;
                    } else if (processargs && strcmp (argv[i], "-tracecode") == 0) {
                        tracecode = true ;
                        continue ;
                    }
                }
            }
        }

	std::string cmd_name = argv[0];
	std::string cmd_directory = findRunDir (argv[0]) ; 

        std::string exename = "\\aikido_" ;
        if (debug || tracecode) {
            exename += "g.exe" ;
        } else {
            exename += "o.exe" ;
        }

        std::string exepath = cmd_directory + exename ;
        struct stat st ;

        //std::cout << "exe path= " << exepath << "\n" ;
        if (stat (exepath.c_str(), &st) != 0) {
            std::cerr << "*** Unable to find aikido executable: " << exepath << "\n" ;
            exit (1) ;
        }

        PROCESS_INFORMATION piProcInfo;
        STARTUPINFOA siStartInfo;

// Set up members of the PROCESS_INFORMATION structure. 

        ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );

// Set up members of the STARTUPINFO structure. 

        ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
        siStartInfo.cb = sizeof(STARTUPINFO);

// Create the child process. 

        std::string command = exepath + args ;

        BOOL ok = CreateProcessA(NULL,
           (char*)command.c_str(),       // command line 
           NULL,          // process security attributes 
           NULL,          // primary thread security attributes 
           TRUE,          // handles are inherited 
           0,             // creation flags 
           NULL,          // use parent's environment 
           NULL,          // use parent's current directory 
           &siStartInfo,  // STARTUPINFO pointer 
           &piProcInfo);  // receives PROCESS_INFORMATION 

        if (!ok) {
            std::cerr << "*** Unable to execute aikido executable: " << exepath << "\n" ;
            exit (2) ;
        } else {
            WaitForSingleObject (piProcInfo.hProcess, INFINITE) ;
            CloseHandle (piProcInfo.hProcess) ;
            CloseHandle (piProcInfo.hThread) ;
            exit (0) ;
        }
    } catch (char *s) {
        fprintf (stderr, "aikido: uncaught C++ exception %s\n", s) ;
        exit (1) ;
    } catch (const char *s) {
        fprintf (stderr, "aikido: uncaught C++ exception %s\n", s) ;
        exit (1) ;
    }
    exit (0) ;
}




