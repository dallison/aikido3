/*
 * main.cc
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
 * Portions created by David Allison of PathScale Inc. are Copyright (C) PathScale Inc. 2004.  All
 * Rights Reserved.
 * 
 * 
 * Contributor(s): dallison
 *
 * Version:  1.10
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/08/11
 */

// CHANGE LOG
//  	1/21/2004	David Allison of PathScale Inc.
//		Added object code loading and dumping
//		Added standalone ability
//		Exit with status 1 if compilation failed


#pragma ident "@(#)main.cc	1.10 03/08/11 Aikido - SMI"



#include <sys/types.h>
#if defined(_OS_MACOSX)
#include <sys/stat.h>
#endif
#include "../aikido.h"
#include <unistd.h>
#if defined(_OS_MACOSX)
#include <sys/wait.h>

#elif defined (_OS_CYGWIN)
#include <sys/wait.h>

#else
#include <wait.h>
#endif
#include <stdlib.h>
#if !defined(_OS_MACOSX)
#include <sys/stat.h>
#endif
#include <sys/param.h>
#include <errno.h>

using namespace aikido ;

const char *legal_notice = "Copyright (C) 2003 Sun Microsystems, Inc., 4150 Network Circle, Santa Clara, California 95054, U.S.A. "
        "All rights reserved.  U.S. Government Rights - Commercial software.  Government users are subject to the Sun Microsystems, "
        "Inc. standard license agreement and applicable provisions of the FAR and its supplements.  Use is subject to "
        "license terms.  Sun,  Sun Microsystems,  the Sun logo and  Java are trademarks or registered trademarks of "
        "Sun Microsystems, Inc. in the U.S. and other countries.This product is covered and controlled by U.S. Export "
        "Control laws and may be subject to the export or import laws in other countries.  Nuclear, missile, chemical "
        "biological weapons or nuclear maritime end uses or end users, whether direct or indirect, are strictly "
        "prohibited.  Export or reexport to countries subject to U.S. embargo or to entities identified on U.S. export "
        "exclusion lists, including, but not limited to, the denied persons and specially designated nationals lists "
        "is strictly prohibited." ;


extern bool disassemble ;
extern bool tracecode ;

static const char *findCPP() {
    struct stat s ;
    
    if (stat ("/usr/lib/cpp", &s) == 0) {
        return "/usr/lib/cpp" ;
    }
    if (stat ("/usr/ccs/lib/cpp", &s) == 0) {
        return "/usr/ccs/lib/cpp" ;
    }
    return NULL ;
}


//
// preprocess the given file and write to standard output
//

void preprocess (char *filename, int nargs, char *args[]) {
    const char *cpp = findCPP() ;
    if (cpp == NULL) {
        std::cerr << "aikido: can't find cpp" << std::endl ;
        exit (1) ;
    }
    char **argv ;
    argv = (char**)calloc (nargs + 4, sizeof (char*)) ;
    if (argv == NULL) {
        fprintf (stderr, "aikido: out of memory\n") ;
        exit (2) ;
    }
    argv[0] = (char *)"cpp" ;
    argv[1] = (char *)"-BR" ;
    int i ;
    for (i = 0 ; i < nargs ; i++) {
        argv[i + 2] = args[i] ;
    }
    i = nargs + 2 ;
    argv[i++] = filename ;
    //std::cout << "cpp filename is " << filename << '\n' ;

    argv[i++] = NULL ;
    execv (cpp, argv) ;
    std::cerr << "aikido: execv failed" << std::endl ;
    exit (3) ;       
}


std::string expand_path (const std::string& wdir, const std::string& path) ;



/* Parse ":" separated lists such in environment variables. */
std::string part (const std::string& s, int i) {
    int l = -1;
    int f = 0;
    int j = 0;
    while (j <= i) {
	f = l+1;
	l = s.find (':', f);
	if (l < 0) {
	    l = f;
	    break;
	}
	j += 1;
    }
    if (l == f) {
        return s.substr (f);
    }
    return s.substr (f, l-f);
}

/* Return a file name suffix at the end of a string. */
std::string suffix (const std::string& s) {
    int i;
    for (i = s.length()-1; i >= 0; i -= 1) {
	if (s[i] == '.')
	    break;           // found a suffix
	if (s[i] == '/') {   // found a directory
	    i = -1;
	    break;
	}
    }
    if (i < 0) {
	return "";
    } else {
	return s.substr (i, s.length()-i);
    }
}


/* Return a file name with any suffix stripped */
std::string strippedname (const std::string& s) {
    int i;
    for (i = s.length()-1; i >= 0; i -= 1) {
	if (s[i] == '.')
	    break;           // found a suffix
	if (s[i] == '/') {   // found a directory
	    i = -1;
	    break;
	}
    }
    if (i < 0) {
	return s;
    } else {
	return s.substr (0, i);
    }
}


/* Return the directory portion of a pathname */
std::string directory (const std::string& s) {
    int i;
    for (i = s.length()-1; i >= 0; i -= 1) {
	if (s[i] == '/')   // found a directory
	    break;
    }
    if (i < 0) {
	return "";
    } else if (i == 0) {   // root directory
	return "/";
    } else {
	return s.substr (0, i);
    }
}


/* Return the file portion of a pathname */
std::string file (const std::string& s) {
    int i;
    for (i = s.length()-1; i >= 0; i -= 1) {
	if (s[i] == '/')   // found a directory
	    break;
    }
    if (i < 0) {
	return "";
    } else {
	return s.substr (i+1, s.length()-(i+1));
    }
}

/* Get the name of the working directory. */
std::string get_working_dir () {
    char cwd [MAXPATHLEN+1];
    if (getcwd (cwd, MAXPATHLEN)) {
        return cwd;
    } else {
        return ".";
    }
}


/* Try to get the target of a link if it is one. */
std::string resolve_link (const std::string& in_path) {
    std::string path = in_path;
    char linkpath [MAXPATHLEN+1];
    int len;

    /* Search through any links that it may point to. */
    while ((len = readlink (path.c_str(), linkpath, MAXPATHLEN)) >= 0) {
	linkpath[len] = 0;               // insert a null
	path = expand_path (directory (path), std::string (linkpath));
    }

    /* Contine pass here if the errno is EINVAL, which means a regular file,
       otherwise just return the original path */
    if (errno != EINVAL) return in_path;

    return path;
}

/* Test whether a file, etc. is present */

bool is_valid_path (const std::string& fname) {
    struct stat stb;
    return (stat(fname.c_str(), &stb) == 0);
}

bool is_valid_file (const std::string& fname) {
    struct stat stb;
    return ((stat(fname.c_str(), &stb) == 0)
	      && (! S_ISDIR(stb.st_mode))
	      && ((stb.st_mode & S_IREAD) != 0));
}

bool is_valid_output_file (const std::string& fname) {
    struct stat stb;
    return ((stat(fname.c_str(), &stb) == 0)
	      && (! S_ISDIR(stb.st_mode))
	      && ((stb.st_mode & S_IWRITE) != 0));
}

bool is_valid_exe (const std::string& fname) {
    struct stat stb;
    return ((stat(fname.c_str(), &stb) == 0)
	      && (! S_ISDIR(stb.st_mode))
	      && ((stb.st_mode & S_IEXEC) != 0));
}

bool is_valid_dir (const std::string& fname) {
    struct stat stb;
    return ((stat(fname.c_str(), &stb) == 0)
	      && (S_ISDIR(stb.st_mode))
	      && ((stb.st_mode & S_IREAD) != 0));
}


/* Make a full pathname out of a working directory and a path. */
std::string expand_path (const std::string& wdir, const std::string& path) {
    int slash = path.find ('/');
    if (slash == 0) {
	/* Full pathname */
	return path;
    } else if (slash > 0) {
	/* Partial path, expand the directory */
	return expand_path (expand_path (wdir, directory (path)), 
			     file (path));
    } else if (wdir == "") {
	/* Expand relative to working directory */
	return expand_path (get_working_dir(), path);
    } else if (path == ".") {
	/* Path is to working directory */
	return wdir;
    } else if (path == "..") {
	/* If the wdir is a symbolic link, we need to find where it
	   points, or naively discarding the last directory name may
	   produce an invalid path.   We don't call resolve_path, 
	   because we want to preserve the higher level symbolic
	   names.  The user can always fully resolve the result. */
	return directory (resolve_link (wdir));
    } else if (wdir == "/") {     // root, avoid double slash
	return wdir + path;
    } else {
	/* Form pathname */
	return wdir + "/" + path;
    }
}


/* Try to get the actual full pathname of a file. */
std::string resolve_path (const std::string& in_path) {
    std::string path = in_path;
    char linkpath [MAXPATHLEN+1];
    int len;

    /* Search through any links that it may point to. */
    while ((len = readlink (path.c_str(), linkpath, MAXPATHLEN)) >= 0) {
	linkpath[len] = 0;               // insert a null
	path = expand_path (directory (path), std::string (linkpath));
    }

    /* Contine pass here if the errno is EINVAL, which means a regular file,
       otherwise just return the original path */
    if (errno != EINVAL) return in_path;

    /* Resolve the directory portion of the pathname */
    std::string dn = directory (path);
    if (dn != "/") {   
	std::string fn = file (path);
	if (fn == "..") {
	    path = directory (resolve_path (dn));
	} else {
	    path = expand_path (resolve_path (dn), fn);
	}
    }

    return path;
}

/* Try to find where this program is located. */
std::string get_command_path (char *argv0) {
    std::string dn = directory (argv0);
    if (dn != "") {
	/* Executed with an explicit directory name */
	std::string name = expand_path (get_working_dir(), argv0);
	return resolve_path (name);
    } else {
	/* Otherwise search the PATH environment variable for its
	   location. */
        char *p = getenv("PATH");
        if (p != NULL) { 
            std::string path = p ;
            int i = 0;
            for (;;) {
                std::string dir = part(path, i);
                if (dir == "") break;
		dir = expand_path (get_working_dir(), dir);
		std::string tryname = expand_path (dir, argv0);
                if (is_valid_exe (tryname)) {
                    return resolve_path (tryname); 
                }
                i += 1;
	    }
	}
	return "";
    }
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
    if (stat (path.c_str(), &st) == 0) {
        return path ;
    }
    path += ".aikido" ;
    if (stat (path.c_str(), &st) == 0) {
        return path ;
    }
    path = aikido::string (filename) + ".dwn" ;
    if (stat (path.c_str(), &st) == 0) {
        return path ;
    }
    path = aikido::string (filename) + ".ki" ;
    if (stat (path.c_str(), &st) == 0) {
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
            while (*p != 0 && *p != ':') {
                *s++ = *p++ ;
            }
            *s = 0 ;
            path = aikido::string (buf) + "/" + aikido::string (filename) ;
            if (stat (path.c_str(), &st) == 0) {
                return path ;
            }
            path += ".aikido" ;
            if (stat (path.c_str(), &st) == 0) {
                return path ;
            }
            path = aikido::string (buf) + "/" + aikido::string (filename) + ".dwn";
            if (stat (path.c_str(), &st) == 0) {
                return path ;
            }
            path = aikido::string (buf) + "/" + aikido::string (filename) + ".ki";
            if (stat (path.c_str(), &st) == 0) {
                return path ;
            }
            if (*p == ':') p++ ;
        }
    }
    path = dir + "/" + std::string (filename) ;
    if (stat (path.c_str(), &st) == 0) {
        return path ;
    }
    path += ".aikido" ;
    if (stat (path.c_str(), &st) == 0) {
        return path ;
    }
    path += ".ako" ;
    if (stat (path.c_str(), &st) == 0) {
        return path ;
    }
    path = dir + "/" + std::string (filename) + ".dwn" ;
    if (stat (path.c_str(), &st) == 0) {
        return path ;
    }
    path = dir + "/" + std::string (filename) + ".ki" ;
    if (stat (path.c_str(), &st) == 0) {
        return path ;
    }
    return "" ;
}

static char aikidoMacro[32] ;

int main (int argc, char **argv) {
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
        bool checkonly = false ;
        bool compilecode = false ;
        bool loadobjectfile = false ;
        bool allargs = false ;
        std::string objectfilename = "" ;
        bool removetemp = false ;

        sprintf (aikidoMacro, "-D__DARWIN=%d", version_number) ;
        cppargs[ncppargs++] = aikidoMacro ;

        void *osym = OS::findSymbol ("__aikido_obj_file") ;
        if (osym != NULL) {
           int size = *(int*)osym ;
           char tmpfile[256] ;
           strcpy (tmpfile, "aikidoXXXXXX") ;
           int fd = mkstemp (tmpfile) ;
           if (fd == -1) {
              std::cerr << "Unable to open temp file " << tmpfile << "\n" ;
              exit (3) ;
           }
           close (fd) ;
           std::ofstream out (tmpfile) ;
           if (!out) {
              std::cerr << "Unable to open temp file " << tmpfile << "\n" ;
              exit (3) ;
           }
           unsigned char *ch = ((unsigned char *)osym) + 4 ;
           for (int i = 0 ; i < size ; i++) {
               out.put (*ch++) ;
           }
           out.close() ;
           files[0] = tmpfile ;
           nfiles = 1 ;
           loadobjectfile = true ;
           removetemp = true ;
           processargs = false ;		// default to switch off args
           allargs = true ;			// all paras are args 
        }

        for (int i = 1 ; i < argc ; i++) {
            if (argv[i][0] == '-') {
                switch (argv[i][1]) {
                case 'I':
                    if (processargs) {
                        includes[nincs++] = argv[i] ;
                    }
                    // fall through
                case 'U':
                case 'D':
                    if (processargs) {
                        cppargs[ncppargs++] = argv[i] ;
                    } else {
                        args[nargs++] = argv[i] ;
                    }
                    break ;
                default:
                    if (strcmp (argv[i], "--") == 0) {      // toggle arg processing
                        processargs = !processargs ;
                        continue ;
                    }
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
                        std::cout << "Portions created by Xsigo Systems Inc are Copyright (C) 2006-2012 Xsigo Systems Inc.\n" ;
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
                    } else if (processargs && strcmp (argv[i], "-cpp") == 0) {
                        nocpp = false ;
                        continue ;
                    } else if (processargs && strcmp (argv[i], "-nocpp") == 0) {
                        nocpp = true ;
                        continue ;
                    } else if (processargs && strcmp (argv[i], "-n") == 0) {
                        checkonly = true ;
                        continue ;
                    } else if (processargs && strcmp (argv[i], "-c") == 0) {
                        compilecode = true ;
                        continue ;
                    } else if (processargs && strcmp (argv[i], "-l") == 0) {
                        loadobjectfile = true ;
                        continue ;
                    } else if (processargs && compilecode && strcmp (argv[i], "-o") == 0) {
                        if (i == argc-1) {
                            std::cerr << "Expected a filename for the -o option\n" ;
                            exit (1); 
                        }
                        i++ ;
                        objectfilename = argv[i] ;
                        continue ;
                    }
                    args[nargs++] = argv[i] ;
                }
            } else {
                if (!allargs && processargs && (strstr (argv[i], ".aikido") != NULL || strstr (argv[i], ".dwn") != NULL)
                     || strstr (argv[i], ".ako") != NULL) {
                    if (strstr (argv[i], ".ako") != NULL) {		// a .ako file is only automatic as first arg
                        if (nfiles == 0) {
                            files[nfiles++] = argv[i] ;
                        } else {
                            args[nargs++] = argv[i] ;
                        }
                    } else {
                        files[nfiles++] = argv[i] ;
                    }
                } else {
                    if (!allargs && nfiles == 0) {
                        files[nfiles++] = argv[i] ;			// first file is always a program
                    } else {
                        args[nargs++] = argv[i] ;
                    }
                }
            }
        }

        std::string cmd_name = get_command_path (argv[0]);
        std::string cmd_directory ;

        if (is_valid_exe (cmd_name)) {
	    std::string dir = directory (cmd_name);
	    if (is_valid_dir (dir)) {
	        cmd_directory = dir;
	    }
        } else {
	    cmd_name = argv[0];
	    cmd_directory = "";
        }

        int pid ;
        int pipes[2] ;

        // running a .ako file implies loading an object file
        if (nfiles == 1 && strstr (files[0], ".ako") != NULL) {
            loadobjectfile = true ;
        }
        if (loadobjectfile) {
            if (nfiles != 1) {
                std::cerr << "Can only specify one file with the -l option\n" ;
                exit(2) ;
            }
            Aikido aikido (files[0], cmd_directory, cmd_name,std::cin, std::cout, std::cerr,  MULTITHREADED | (nocpp ? NOCPP : 0) | (debug ? DEBUG : 0) | (warn?INVENT_WARNING:0) | (traceback?TRACEBACK:0), nincs, includes) ;
            aikido.execute (1, nargs, args) ;
            aikido.finalize() ;
            if (removetemp) {
                remove (files[0]) ;
            }
            exit (aikido.numErrors != 0) ;
        }

        // work out what to call the output file
        if (compilecode) {
            if (objectfilename == "") {
                if (nfiles == 0) {
                    objectfilename = "aikido.out" ;
                } else {
                    std::string f = files[0] ;
                    int j = f.size() - 1 ;
                    while (j >= 0) {
                        if (f[j] == '.' || f[j] == '/') {
                            break ;
                        }
                        j-- ;
                    }
                    if (f[j] == '.') {
                        objectfilename = f.substr (0, j) + ".ako" ;
                    } else {
                        objectfilename = f + ".ako" ;
                    }
                }
            }
        }

        // make instance of the aikido interpreter
        Aikido aikido (cmd_directory, cmd_name, nargs, args, std::cin, std::cout, std::cerr,
                       MULTITHREADED | (nocpp ? NOCPP : 0) | (debug ? DEBUG : 0) | (warn?INVENT_WARNING:0) | (traceback?TRACEBACK:0) | (compilecode?COMPILE:0), 
                       nincs, includes) ;

        aikido.setTraceLevel (tracelevel) ;

        if (nfiles == 0) {
             files[nfiles++] = NULL ;
        }
        int saved_stdin = dup (0) ;
        
        for (int i = 0 ; i < nfiles ; i++) {
            if (nocpp) {
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
#if defined(_CC_GCC) && __GNUC__ == 2
                                std::strstream *zs = entry->getStream() ;
#else
                                std::stringstream *zs = entry->getStream() ;
#endif
                                aikido::string fn = contents[j];
                                aikido.parse (fn, *zs);
                                delete zs ;
                            }
                        }
                        continue;
                    } else {
                        std::fstream in ;
#if defined(__GNUG__)
                        in.open (file, std::ios::in) ;
#else
                        in.open (file, std::ios_base::in) ;
#endif
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
                    continue ;
                }
            }
    
            if (pipe (pipes) != 0) {
                std::cerr << "aikido: unable to create pipe" << std::endl ;
                exit (1) ;
            }
            pid = fork() ;
            if (pid != 0) {		// parent
                dup2 (pipes[0], 0) ;		// make pipes[0] the same as stdin
                close (pipes[1]) ;			// close the other pipe bore
                std::cin.clear() ;
                aikido.parse (files[i] == NULL ? "stdin" : files[i], std::cin) ;
                close (pipes[0]) ;		// close old in bore
            } else {			// child
                dup2 (pipes[1], 1) ;		// make pipes[1] the same as stdout
                close (pipes[0]) ;			// close the other pipe bore
                char *file = files[i] ;
                aikido::string foundfile ;
                if (file != NULL) {
                    foundfile = findFile (cmd_directory, files[i]) ;
                    if (foundfile == "") {
                        std::cerr << "aikido: can't find input file " << file << '\n' ;
                        ::exit (1) ;
                    }
                    file = (char *)foundfile.c_str() ;
                }
                preprocess (file, ncppargs, cppargs) ;
            }
        }
        dup2 (saved_stdin, 0) ;		// reset stdin
        std::cin.clear() ;
        aikido.generate() ;		// generate code
        if (compilecode) {
            aikido.dumpObjectCode (objectfilename) ;			
        } else {
            if (!checkonly) {
                aikido.execute() ;
            }
        }
        aikido.print(std::cout) ;
        aikido.finalize() ;
        exit (aikido.numErrors != 0) ;
    } catch (char *s) {
        fprintf (stderr, "aikido: uncaught C++ exception %s\n", s) ;
        exit (1) ;
    } catch (const char *s) {
        fprintf (stderr, "aikido: uncaught C++ exception %s\n", s) ;
        exit (1) ;
    }
}




