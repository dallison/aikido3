/*
 * driver.cc
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
 * Version:  1.6
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/08/11
 */

#pragma ident "@(#)driver.cc	1.6 03/08/11 Aikido - SMI"



#include <sys/param.h>
#include <errno.h>
#include <string>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#if defined(_OS_MACOSX)
#include <sys/stat.h>
#endif
#include <unistd.h>
#include <stdlib.h>
#if !defined(_OS_MACOSX)
#include <sys/stat.h>
#endif
#include <iostream>
#include <stdio.h>

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


int main (int argc, char **argv) {
    try {
        bool processargs = true ;
        bool debug = false ;
        bool tracecode = false ;

        for (int i = 1 ; i < argc ; i++) {
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

        std::string exename = "/aikido_" ;
        if (debug || tracecode) {    
            exename += "g" ;
        } else {
            exename += "o" ;
        }

        std::string exepath = cmd_directory + exename ;
        struct stat st ;

        //std::cout << "exe path= " << exepath << "\n" ;
        if (stat (exepath.c_str(), &st) != 0) {
            std::cerr << "*** Unable to find aikido executable: " << exepath << "\n" ;
            exit (1) ;
        }
        
        execv (exepath.c_str(), argv) ;
        std::cerr << "*** Unable to execute aikido executable: " << exepath << "\n" ;
        exit (2) ;
    } catch (char *s) {
        fprintf (stderr, "aikido: uncaught C++ exception %s\n", s) ;
    } catch (const char *s) {
        fprintf (stderr, "aikido: uncaught C++ exception %s\n", s) ;
    }
    exit (3) ;
}




