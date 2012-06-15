/*
 * main.cc
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
 * Version:  1.8
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/01/24
 */



#include <sys/types.h>
#include "../aikido.h"
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

using namespace aikido ;

extern bool disassemble ;
extern bool tracecode ;

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
	char procpathname[1024];
	if (GetModuleFileName (NULL, procpathname, 1024)) {
		exename = procpathname;
	}

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

bool fileExists (const aikido::string &filename) {
    std::ifstream f (filename.c_str()) ;
    if (!f) {
        return false ;
    }
    return true ;
}

// find a file by searching:
// 1. current directory
// 2. AIKIDOPATh elements
// 3. run directory
//
// It searches for the file followed by file.aikido
// Then looks for file.dwn for legacy reasons

aikido::string findFile (std::string dir, char *filename) {
    if (filename == NULL) {
        return filename ;
    }
    struct stat st ;
    aikido::string path ;

    path = filename ;
    if (fileExists (path)) {
        return path ;
    }
    path += ".aikido" ;
    if (fileExists (path)) {
        return path ;
    }
    path = aikido::string(filename) + ".dwn" ;
    if (fileExists (path)) {
        return path ;
    }
    char *aikidopath = getenv ("AIKIDOPATH") ;
    if (aikidopath == NULL) {
        aikidopath = getenv ("DARWINPATH") ;                // legacy
    }
    if (aikidopath != NULL) {
        char buf[256] ;
        char *s, *p ;
        p = aikidopath ;
        while (*p != 0) {
            s = buf ;
            while (*p != 0 && *p != ';') {
                *s++ = *p++ ;
            }
            *s = 0 ;
            path = aikido::string (buf) + "\\" + aikido::string (filename) ;
            if (fileExists (path)) {
                return path ;
            }
            path += ".aikido" ;
            if (fileExists (path)) {
                return path ;
            }
            path = aikido::string (buf) + "\\" + aikido::string (filename) + ".dwn";
            if (fileExists (path)) {
                return path ;
            }
            if (*p == ';') p++ ;
        }
    }
    path = dir + "\\" + std::string (filename) ;
    if (fileExists (path)) {
        return path ;
    }
    path += ".aikido" ;
    if (fileExists (path)) {
        return path ;
    }
    path = dir + "\\" + std::string (filename) + ".dwn" ;
    if (fileExists (path)) {
        return path ;
    }
    return "" ;
}

static char aikidoMacro[32] ;

int main (int argc, char *argv[]) {
    try {
        char *cppargs[100] ;
        char *args[100] ;
        char *files[100] ;
        char *includes[100] ;
        int nfiles = 0 ;
        int ncppargs = 0 ;
        int nargs = 0 ;
        int nincs = 0 ;
        char *infile = NULL ;
        bool processargs = true ;
        bool nocpp = true ;
        bool debug = false ;
        bool warn = false ;
        int tracelevel = 0 ;
        bool traceback = true ;

        sprintf (aikidoMacro, "-D__DARWIN=%d", version_number) ;
        cppargs[ncppargs++] = aikidoMacro ;

        for (int i = 1 ; i < argc ; i++) {
            if (argv[i][0] == '-') {
                switch (argv[i][1]) {
                case '-':			// -- toggles further arg processing 
                    processargs = !processargs ;
                    break ;
                case 'I':
                    if (processargs) {
                        includes[nincs++] = argv[i] ;
                    }
                    // fall through
                    break ;
                default:
                    if (processargs && strcmp (argv[i], "-help") == 0) {
                       char version[32] ;
                        sprintf (version, "%.2f", (float)version_number / 100) ;
                        std::cout << "Aikido interpreter version " << version << '\n' ;
                        std::cout << "Copyright (C) 2000-2003 Sun Microsystems Inc.\n" ;
                        std::cout << "Portions created by PathScale Inc. are Copyright (C) 2004 PathScale Inc.\n" ;
                        std::cout << "Closure support is Copyright (C) 2004 David Allison.\n" ;
                        std::cout << "Portions created by Xsigo Systems Inc are Copyright (C) 2006-2012 Xsigo Systems Inc.\n" ;

                        std::cout << "\nusage: aikido [option...] [program.aikido|program.ki ...] [program arg ...]\n" ;
                        std::cout << "    options:\n" ;
                        std::cout << "        --                toggle processing of options\n" ;
                        std::cout << "        -help             this message\n" ;
                        std::cout << "        -debug            run debugger\n" ;
                        std::cout << "        -[no]cpp          switch on/off the C preprocessor (default off)\n" ;
                        std::cout << "        -nogc             switch off garbage collection\n" ;
                        std::cout << "        -verbosegc        switch on printing of garbage collection actions\n" ;
                        //std::cout << "        -trace <level>    trace execution\n" ;
                        std::cout << "        -Ipath            add path to the C preprocessor search path and import path\n" ;
                        std::cout << "        -Dmacro[=value]   define C preprocessor macro\n" ;
                        std::cout << "        -Umacro           undefine C preprocessor macro\n" ;
                        std::cout << "        -w                warn about invented variables\n" ;
                        std::cout << "        -dis              disassemble code\n" ;
                        std::cout << "        -notraceback      don't show stack trace on exception\n" ;
                        std::cout << "        -tracecode        trace code as it is executed\n" ;
                        std::cout << "        -n                check code only, do not execute\n" ;
                        std::cout << "        -c                compile code to object file\n" ;
                        std::cout << "        -l                load object file\n" ;
                        std::cout << "        -o objfile        specify object file name for -c option\n" ;
                        exit (0) ;
                    } else if (processargs && strcmp (argv[i], "-version") == 0) {
                        char version[32] ;
                        sprintf (version, "%.2f", (float)version_number / 100) ;
                        std::cout << "Aikido interpreter version " << version << '\n' ;
                        std::cout << "Copyright (C) 2000-2003 Sun Microsystems Inc.\n" ;
                        std::cout << "Portions created by PathScale Inc. are Copyright (C) 2004 PathScale Inc.\n" ;
                        std::cout << "Closure support is Copyright (C) 2004 David Allison.\n" ;
                        std::cout << "Portions created by Xsigo Systems Inc are Copyright (C) 2006-2012 Xsigo Systems Inc.";
                        exit (0) ;
                    } else if (processargs && strcmp (argv[i], "-verbosegc") == 0) {
                        verbosegc = 1 ;
                        continue ;
                    } else if (processargs && strcmp (argv[i], "-nogc") == 0) {
                        nogc = 1 ;
                        continue ;
                    } else if (processargs && strcmp (argv[i], "-debug") == 0) {
                        debug = true ;
                        continue ;
                    //} else if (processargs && strcmp (argv[i], "-trace") == 0) {
                        //tracelevel = atoi (argv[i+1]) ;
                        //i++ ;
                        //continue ;
                    } else if (processargs && strcmp (argv[i], "-w") == 0) {
                        warn = true ;
                        continue ;
                    } else if (processargs && strcmp (argv[i], "-dis") == 0) {
                        disassemble = true ;
                        continue ;
                    } else if (processargs && strcmp (argv[i], "-notraceback") == 0) {
                        traceback = false ;
                        continue ;
                    } else if (processargs && strcmp (argv[i], "-tracecode") == 0) {
                        tracecode = true ;
                        continue ;
                    }
                    args[nargs++] = argv[i] ;
                }
            } else {
                if (processargs && strstr (argv[i], ".aikido") != NULL) {
                    files[nfiles++] = argv[i] ;
                } else {
                    if (nfiles == 0) {
                        files[nfiles++] = argv[i] ;			// first file is always a program
                    } else {
                        args[nargs++] = argv[i] ;
                    }
                }
            }
        }

	std::string cmd_name = argv[0];
	if (cmd_name.find(".exe") == std::string::npos) {
		cmd_name += ".exe";
	}
	std::string cmd_directory = findRunDir (cmd_name.c_str()) ; 

        Aikido aikido (cmd_directory, cmd_name, nargs, args, std::cin, std::cout, std::cerr,
                       MULTITHREADED | (nocpp ? NOCPP : 0) | (debug ? DEBUG : 0) | (warn?INVENT_WARNING:0) | (traceback ? TRACEBACK:0), nincs, includes) ;
        aikido.setTraceLevel (tracelevel) ;

        if (nfiles == 0) {
             files[nfiles++] = NULL ;
        }

        
        for (int i = 0 ; i < nfiles ; i++) {
            char *file = files[i] ;
            aikido::string foundfile ;
            if (file != NULL) {
                foundfile = findFile (cmd_directory, files[i]) ;
                if (foundfile == "") {
                    std::cerr << "aikido: can't find input file " << file << '\n' ;
                    ::exit (1) ;
                }
                file = (char *)foundfile.c_str() ;
                if (strstr (file, ".zip") != NULL) {
                    zip::ZipFile *z = new zip::ZipFile (file);
                    std::vector<std::string> contents;
                    z->list (contents);
                    for (unsigned int j = 0 ; j < contents.size() ; j++) {
                        zip::ZipEntry *entry = z->open (contents[j]);
                        if (entry != NULL) {
                            std::stringstream *zs = entry->getStream() ;
                            aikido::string fn = contents[j];
                            aikido.parse (fn, *zs);
                            delete zs ;
                        }
                    }
                    continue;
                } else {
                    std::fstream in ;
                    in.open (file, std::ios_base::in) ;
                    if (!in) {
                        std::cerr << "aikido: failed to open input file " << file << '\n' ;
                        ::exit (1) ;
                    }
                    aikido.parse (file, in) ;
                    in.close() ;
                    continue ;
                }
            } else {
                aikido.parse ("stdin", std::cin) ;
            }
        }

		aikido.generate();
        aikido.execute() ;
        aikido.print(std::cout) ;
        aikido.finalize() ;
        exit (0) ;
    } catch (char *s) {
        fprintf (stderr, "aikido: uncaught C++ exception %s\n", s) ;
    } catch (const char *s) {
        fprintf (stderr, "aikido: uncaught C++ exception %s\n", s) ;
    }
}




