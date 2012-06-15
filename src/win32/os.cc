/*
 * os.cc
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
 * Version:  1.9
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/01/24
 */



//
// Implemenation of Windows OS specific code
//
// Author: David Allison
// Version: 1.9 11/12/03
//
#include "os.h"
#include <sys/stat.h>
#include <ctype.h>
#include <iostream>
#include <list>

namespace aikido {

extern std::string programname ;

namespace OS {

    const char *libraryExt = ".dll" ;
   
    std::vector <HMODULE> modules ;
    std::vector<std::string> builtinlibs ;

    void init() {
        builtinlibs.push_back("libnetwork.dll") ;
        builtinlibs.push_back("libmath.dll") ;
        builtinlibs.push_back("libregistry.dll") ;

        // initialize the network
        WORD v ;
        WSADATA data ;
        v = MAKEWORD(2,2) ;
        if (WSAStartup (v, &data) != 0) {
            throw "Unable to initialize winsocks" ;
        }
        if (LOBYTE (data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
            WSACleanup() ;
            throw "Bad winsocks version" ;
        }
    }

    bool libraryLoaded (const std::string &name) {
        for (int i = 0 ; i < builtinlibs.size() ; i++) {
            if (name.find (builtinlibs[i]) >= 0) {
                return true ;
            }
        }
        return false ;
    }

    void *openLibrary (const char *name) {
        HMODULE handle = LoadLibraryA (name) ;
        if (handle != NULL) {
            modules.push_back (handle) ;
            return (void*)handle ;
        }
        return NULL ;
    }


    void *findSymbol (const char *name, void *libhandle) {
        char fullname[256] ;
        fullname[0] = '_' ;
        strcpy (fullname+1,name) ;
        if (modules.size() == 0) {
            //std::string dll = programname.substr (0, programname.size() - 4) + ".dll" ;
			std::string dll = programname;
			if (dll.find(".exe") == std::string::npos) {
				dll += ".exe";
			}
            HMODULE handle = GetModuleHandleA (dll.c_str()) ;
            if (handle == NULL) {
				// try as a dll
				//printf ("programname: %s\n", programname.c_str());
				dll = programname;
				if (dll.find(".dll") == std::string::npos) {
					dll = programname + ".dll";
				}
				
				handle = GetModuleHandleA (dll.c_str()) ;
			    if (handle == NULL) {
					throw "Unable to get handle for main program" ;
				}
            }
            modules.push_back (handle) ;
            HINSTANCE m = LoadLibraryA ("msvcrt.dll") ;
            if (m != NULL) {
                modules.push_back (m) ;
            }

        }
        // first try with _ prepended
        for (int i = 0 ; i < modules.size() ; i++) {
            FARPROC procaddr = GetProcAddress (modules[i], fullname) ;
            if (procaddr != NULL) {
                return (void*)procaddr ;
            }
        }
        // now try raw name
        for (int i = 0 ; i < modules.size() ; i++) {
            FARPROC procaddr = GetProcAddress (modules[i], name) ;
            if (procaddr != NULL) {
                return (void*)procaddr ;
            }
        }
        return NULL ;
    }

    char *libraryError() {
        return "unknown" ;
    }

    int fork() {
        return 0 ;
    }

    bool fileStats (const char *name, void *stats) {
        return stat (name, (struct stat*)stats) == 0 ;
        return true ;
    }

    __int64 uniqueId (void *stats) {
        struct stat *s = (struct stat*)stats ;
        //std::cout << "dev: " << s->st_dev << "\n" ;
        //std::cout << "ino: " << s->st_ino << "\n" ;
        //std::cout << "mode: " << s->st_mode << "\n" ;
        //std::cout << "nlink: " << s->st_nlink << "\n" ;
        //std::cout << "uid: " << s->st_uid << "\n" ;
        //std::cout << "gid: " << s->st_gid << "\n" ;
        //std::cout << "rdev: " << s->st_rdev << "\n" ;
        //std::cout << "size: " << s->st_size << "\n" ;
        //std::cout << "atime: " << s->st_atime << "\n" ;
        //std::cout << "mtime: " << s->st_mtime << "\n" ;
        //std::cout << "ctime: " << s->st_ctime << "\n" ;
        return ((__int64)s->st_atime << 32) | s->st_size ;
    }



	//
	//  The FileGlobBase is from the net.  There is no license associated with it.
	//

	/**
	The base class of all file glob matching classes.  Derived classes should
	provide an implementation for FoundMatch().

	\sa MatchPattern
**/
class FileGlobBase
{
public:
	FileGlobBase();

	void MatchPattern( const char* inPattern );
	void AddExclusivePattern( const char* name );
	void AddIgnorePattern( const char* name );

	virtual void FoundMatch( const char* name ) = 0;

protected:
	bool MatchExclusivePattern( const char* name );
	bool MatchIgnorePattern( const char* name );
	void GlobHelper( const char* inPattern );

private:
	typedef std::list< std::string > StringList;

	StringList m_exclusiveFilePatterns;
	StringList m_ignorePatterns;
};



// Forward declares.
bool WildMatch( const char* pattern, const char *string, bool caseSensitive );


/**
	Constructor.
**/
FileGlobBase::FileGlobBase()
{
	AddIgnorePattern( "./" );
	AddIgnorePattern( "../" );
}							


/**
	Finds all files matching [inPattern].  The wildcard expansion understands
	the following pattern constructs:

	- ?
		- Any single character.
	- *
		- Any character of which there are zero or more of them.
	- **
		- All subdirectories recursively.

	Additionally, if the pattern closes in a single slash, only directories
	are processed.  Forward slashes and backslashes may be used
	interchangeably.

	- *\
		- List all the directories in the current directory.  Can also be
		  *(forwardslash), but it turns off the C++ comment in this message.
	- **\
		- List all directories recursively.

	Wildcards may appear anywhere in the pattern, including directories.

	- *\*\*\*.c

	Note that *.* only matches files that have an extension.  This is different
	than standard DOS behavior.  Use * all by itself to match files, extension
	or not.

	Recursive wildcards can be used anywhere:

	c:/Dir1/**\A*\**\FileDirs*\**.mp3

	This matches all directories under c:\Dir1\ that start with A.  Under all
	of the directories that start with A, directories starting with FileDirs
	are matched recursively.  Finally, all files ending with an mp3 extension
	are matched.

	Any place that has more than two .. for going up a subdirectory is expanded
	a la 4DOS.

	...../StartSearchHere/**

	Expands to:

	../../../../StartSearchHere/**
		
	\param inPattern The pattern to use for matching.
**/
void FileGlobBase::MatchPattern( const char* inPattern )
{
	char pattern[ MAX_PATH ];

	// Give ourselves a local copy of the inPattern with all \ characters
	// changed to / characters and more than two periods expanded.
	const char* srcPtr = inPattern;

	// Is it a Windows network path?  If so, don't convert the opening \\.
	if ( srcPtr[ 0 ] == '\\'  &&  srcPtr[ 1 ] == '\\' )
		srcPtr += 2;

	const char* lastSlashPtr = srcPtr - 1;
	char* destPtr = pattern;
	int numPeriods = 0;
	while ( *srcPtr != '\0' )
	{
		char ch = *srcPtr;
		
		///////////////////////////////////////////////////////////////////////
		// Check for slashes or backslashes.
		if ( ch == '\\'  ||  ch == '/' )
		{
			*destPtr++ = '/';

			lastSlashPtr = srcPtr;
			numPeriods = 0;
		}

		///////////////////////////////////////////////////////////////////////
		// Check for .
		else if ( ch == '.' )
		{
			if ( srcPtr - numPeriods - 1 == lastSlashPtr )
			{
				numPeriods++;
				if ( numPeriods > 2 )
				{
					*destPtr++ = '/';
					*destPtr++ = '.';
					*destPtr++ = '.';
				}
				else
				{
					*destPtr++ = '.';
				}
			}
			else
			{
				*destPtr++ = '.';
			}
		}

		///////////////////////////////////////////////////////////////////////
		// Check for **
		else if ( ch == '*'  &&  srcPtr[ 1 ] == '*' )
		{
			if ( srcPtr - 1 != lastSlashPtr )
			{
				// Something like this:
				//
				// /Dir**/
				//
				// needs to be translated to:
				//
				// /Dir*/**/
				*destPtr++ = '*';
				*destPtr++ = '/';
			}

			srcPtr += 2;

			*destPtr++ = '*';
			*destPtr++ = '*';

			// Did we get a double star this round?
			if ( srcPtr[ 0 ] != '/'  &&  srcPtr[ 0 ] != '\\' )
			{
				// Handle the case that looks like this:
				//
				// /**Text
				//
				// Translate to:
				//
				// /**/*Text
				*destPtr++ = '/';
				*destPtr++ = '*';
			}
			else if ( srcPtr[ 1 ] == '\0'  ||  srcPtr[ 1 ] == '@' )
			{
				srcPtr++;

				*destPtr++ = '/';
				*destPtr++ = '*';
				*destPtr++ = '/';
			}

			// We added one too many in here... the compiler will optimize.
			srcPtr--;
		}

		///////////////////////////////////////////////////////////////////////
		// Check for @
		else if ( ch == '@' )
		{
			// Gonna finish this processing in another loop.
			break;
		}
			
		///////////////////////////////////////////////////////////////////////
		// Everything else.
		else
		{
			*destPtr++ = *srcPtr;
		}

		srcPtr++;
	}

	*destPtr = 0;

	// Check for the @.
	if ( *srcPtr == '@' )
	{
		// Clear out function registered entries.
		m_ignorePatterns.clear();
		AddIgnorePattern( "./" );
		AddIgnorePattern( "../" );

		m_exclusiveFilePatterns.clear();
	}

	while ( *srcPtr == '@' )
	{
		srcPtr++;

		char ch = *srcPtr++;
		if ( ch == '-'  ||  ch == '=' )
		{
			char buffer[ _MAX_PATH ];
			char* ptr = buffer;
			while ( *srcPtr != '@'  &&  *srcPtr != '\0' )
			{
				*ptr++ = *srcPtr++;
			}

			*ptr = 0;

			if ( ch == '-' )
				AddIgnorePattern( buffer );
			else if ( ch == '=' )
				AddExclusivePattern( buffer );
		}
		else
			break;		// Don't know what it is.
	}

	// Start globbing!
	GlobHelper( pattern );
}


/**
	Adds a pattern to the file glob database of exclusive patterns.  If any
	exclusive patterns are registered, the ignore database is completely
	ignored.  Only patterns matching the exclusive patterns will be
	candidates for matching.

	\param name The exclusive pattern.
**/
void FileGlobBase::AddExclusivePattern( const char* pattern )
{
	for ( StringList::iterator it = m_exclusiveFilePatterns.begin();
			it != m_exclusiveFilePatterns.end(); ++it )
		if ( stricmp( (*it).c_str(), pattern ) == 0 )
			return;

	m_exclusiveFilePatterns.push_back( pattern );
}


/**
	Adds a pattern to ignore to the file glob database.  If a pattern of
	the given name is found, its contents will not be considered for further
	matching.  The result is as if the pattern did not exist for the search
	in the first place.

	\param name The pattern to ignore.
**/
void FileGlobBase::AddIgnorePattern( const char* pattern )
{
	for ( StringList::iterator it = m_ignorePatterns.begin();
			it != m_ignorePatterns.end(); ++it )
		if ( stricmp( (*it).c_str(), pattern ) == 0 )
			return;

	m_ignorePatterns.push_back( pattern );
}


/**
	Match an exclusive pattern.

	\param text The text to match an ignore pattern against.
	\return Returns true if the directory should be ignored, false otherwise.
**/
bool FileGlobBase::MatchExclusivePattern( const char* text )
{
	for ( StringList::iterator it = m_exclusiveFilePatterns.begin();
			it != m_exclusiveFilePatterns.end(); ++it )
	{
		if ( WildMatch( (*it).c_str(), text, false  ) )
			return true;
	}

	return false;
}


/**
	Do a case insensitive find for the pattern.

	\param text The text to match an ignore pattern against.
	\return Returns true if the directory should be ignored, false otherwise.
**/
bool FileGlobBase::MatchIgnorePattern( const char* text )
{
	for ( StringList::iterator it = m_ignorePatterns.begin();
			it != m_ignorePatterns.end(); ++it )
	{
		if ( WildMatch( (*it).c_str(), text, false  ) )
			return true;
	}

	return false;
}


/**
	\internal
	\author Jack Handy

	Borrowed from http://www.codeproject.com/string/wildcmp.asp.
	Modified by Joshua Jensen.
**/
bool WildMatch( const char* pattern, const char *string, bool caseSensitive )
{
	// Handle all the letters of the pattern and the string.
	while ( *string != 0  &&  *pattern != '*' )
	{
		if ( *pattern != '?' )
		{
			if ( caseSensitive )
			{
				if ( *pattern != *string )
					return false;
			}
			else
			{
				if ( toupper( *pattern ) != toupper( *string ) )
					return false;
			}
		}

		pattern++;
		string++;
	}

	const char* mp = NULL;
	const char* cp = NULL;
	while ( *string != 0 )
	{
		if (*pattern == '*')
		{
			// It's a match if the wildcard is at the end.
			if ( *++pattern == 0 )
			{
				return true;
			}

			mp = pattern;
			cp = string + 1;
		}
		else
		{
			if ( caseSensitive )
			{
				if ( *pattern == *string  ||  *pattern == '?' )
				{
					pattern++;
					string++;
				}
				else 
				{
					pattern = mp;
					string = cp++;
				}
			}
			else
			{
				if ( toupper( *pattern ) == toupper( *string )  ||  *pattern == '?' )
				{
					pattern++;
					string++;
				}
				else
				{
					pattern = mp;
					string = cp++;
				}
			}
		}
	}

	// Collapse remaining wildcards.
	while ( *pattern == '*' )
		pattern++;

	return !*pattern;
}


/**
	\internal Simple path splicing (assumes no '/' in either part)
	\author Matthias Wandel (MWandel@rim.net) http://http://www.sentex.net/~mwandel/
**/
static void CatPath(char * dest, const char * p1, const char * p2)
{
	size_t len = strlen( p1 );
	if ( len == 0 )
	{
		strcpy( dest, p2 );
	}
	else
	{
		if ( len + strlen( p2 ) > 200 )
		{
			// Path too long.
			return;
		}
		memcpy( dest, p1, len + 1 );
		if ( dest[ len - 1 ] != '/'  &&  dest[ len - 1 ] != ':' )
		{
			dest[ len++ ] = '/';
		}
		strcpy( dest + len, p2 );
	}
}


/**
	\internal Does all the actual globbing.
	\author Matthias Wandel (MWandel@rim.net) http://http://www.sentex.net/~mwandel/
	\author Joshua Jensen (jjensen@workspacewhiz.com)

	Matthias Wandel wrote the original C algorithm, which is contained in
	his Exif Jpeg header parser at http://www.sentex.net/~mwandel/jhead/ under
	the filename MyGlob.c.  It should be noted that the MAJORITY of this
	function is his, albeit rebranded style-wise.

	I have made the following extensions:

	-	Support for ignoring directories.
	-	Perforce-style (and DJGPP-style) ... for recursion, instead of **.
	-	Automatic conversion from ...Stuff to .../*Stuff.  Allows lookup of
		files by extension, too: '....h' translates to '.../*.h'.
	-	Ability to handle forward slashes and backslashes.
	-	A minimal C++ class design.
	-	Wildcard matching not based on FindFirstFile().  Should allow greater
		control in the future and patching in of the POSIX fnmatch() function
		on systems that support it.
**/
void FileGlobBase::GlobHelper( const char* inPattern )
{
	char patternBuf[ _MAX_PATH * 2 ];
	strcpy( patternBuf, inPattern );

DoRecursion:
	char basePath[ _MAX_PATH ];
	char* basePathEndPtr = basePath;
	char* recurseAtPtr = NULL;

	// Split the path into base path and pattern to match against.
	bool hasWildcard = false;
	char *pattern;

	for ( pattern = patternBuf; *pattern != '\0'; ++pattern )
	{
		char ch = *pattern;

		// Is it a '?' ?
		if ( ch == '?' )
			hasWildcard = true;

		// Is it a '*' ?
		else if ( ch == '*' )
		{
			hasWildcard = true;

			// Is there a '**'?
			if ( pattern[ 1 ] == '*' )
			{
				// If we're just starting the pattern or the characters immediately
				// preceding the pattern are a drive letter ':' or a directory path
				// '/', then set up the internals for later recursion.
				if ( pattern == patternBuf  ||  pattern[ -1 ] == '/'  ||
					pattern[ -1 ] == ':')
				{
					char ch2 = pattern[ 2 ];
					if ( ch2 == '/' )
					{
						recurseAtPtr = pattern;
						memcpy(pattern, pattern + 3, strlen( pattern ) - 2 );
					}
					else if ( ch2 == '\0' )
					{
						recurseAtPtr = pattern;
						*pattern = '\0';
					}
				}
			}
		}

		// Is there a '/' or ':' in the pattern at this location?
		if ( ch == '/'  ||  ch == ':' )
		{
			if ( hasWildcard )
				break;
			basePathEndPtr = &basePath[ pattern - patternBuf + 1 ];
		}
	}

	// If there is no wildcard this time, then just add the current file and
	// get out of here.
	if ( !hasWildcard )
	{
		// This should refer to a file.
		FoundMatch( patternBuf );
		return;
	}

	// Did we make it to the end of the pattern?  If so, we should match files,
	// since there were no slashes encountered.
	bool matchFiles = *pattern == '\0';

	// Copy the directory down.
	size_t basePathLen = basePathEndPtr - basePath;
	strncpy( basePath, patternBuf, basePathLen );

	// Copy the wildcard matching string.
	char matchPattern[ _MAX_PATH ];
	size_t matchLen = ( pattern - patternBuf ) - basePathLen;
	strncpy( matchPattern, patternBuf + basePathLen, matchLen + 1 );
	if ( matchPattern[ matchLen ] == '/' )
		matchPattern[ matchLen ] = 0;

	StringList fileList;

	// Do the file search with *.* in the directory specified in basePattern.
	strcpy( basePathEndPtr, "*.*" );

	// Start the find.
	WIN32_FIND_DATA fd;
	HANDLE handle = FindFirstFile( basePath, &fd );

	// Clear out the *.* so we can use the original basePattern string.
	*basePathEndPtr = 0;

	// Any files found?
	if ( handle != INVALID_HANDLE_VALUE )
	{
		for ( ;; )
		{
			// Is the file a directory?
			if ( ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )  &&  !matchFiles )
			{
				// Do a wildcard match.
				if ( WildMatch( matchPattern, fd.cFileName, false ) )
				{
					// It matched.  Let's see if the file should be ignored.
					bool ignore = false;

					// Knock out "." or ".." if they haven't already been.
					int len = strlen( fd.cFileName );
					fd.cFileName[ len ] = '/';
					fd.cFileName[ len + 1 ] = '\0';

					// See if this is a directory to ignore.
					ignore = MatchIgnorePattern( fd.cFileName );

					fd.cFileName[ len ] = 0;

					// Should this file be ignored?
					if ( !ignore )
					{
						// Nope.  Add it to the linked list.
						fileList.push_back( fd.cFileName );
					}
				}
			}
			else if ( !( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )  &&  matchFiles )
			{
				// Do a wildcard match.
				if ( WildMatch( matchPattern, fd.cFileName, false ) )
				{
					// It matched.  Let's see if the file should be ignored.
					bool ignore = MatchIgnorePattern( fd.cFileName );

					// Is this pattern exclusive?
					if ( !ignore  &&  m_exclusiveFilePatterns.begin() != m_exclusiveFilePatterns.end() )
					{
						ignore = !MatchExclusivePattern( fd.cFileName );
					}

					// Should this file be ignored?
					if ( !ignore )
					{
						// Nope.  Add it to the linked list.
						fileList.push_back( fd.cFileName );
					}
				}
			}

			// Look up the next file.
			if ( !FindNextFile( handle, &fd ) )
				break;
		}

		// Close down the file find handle.
		FindClose( handle );
	}

	// Sort the list.
	fileList.sort();

	// Iterate the file list and either recurse or add the file as a found
	// file.
	if ( !matchFiles )
	{
		for ( StringList::iterator it = fileList.begin(); it != fileList.end(); ++it )
		{
			char combinedName[ _MAX_PATH * 2 ];

			// Need more directories.
			CatPath( combinedName, basePath, (*it).c_str() );
			strcat( combinedName, pattern );
			GlobHelper( combinedName );
		}
	}
	else // if ( !matchFiles )
	{
		for ( StringList::iterator it = fileList.begin(); it != fileList.end(); ++it )
		{
			char combinedName[ _MAX_PATH * 2 ];
			CatPath( combinedName, basePath, (*it).c_str());
			FoundMatch( combinedName );
		}
	}

	// Clear out the file list, so the goto statement below can recurse
	// internally.
	fileList.clear();

	// Do we need to recurse?
	if ( !recurseAtPtr )
		return;

	// Copy in the new recursive pattern to match.
	strcpy( matchPattern, recurseAtPtr );
	strcpy( recurseAtPtr, "*/**/" );
	strcat( patternBuf, matchPattern );

	// As this function context is no longer needed, we can just go back
	// to the top of it to avoid adding another context on the stack.
	goto DoRecursion;
}

class FileGlobList : public FileGlobBase, public std::list< std::string >
{
public:
	typedef std::list< std::string >::iterator Iterator;

	virtual void FoundMatch( const char* fileName )
	{
		// Only add the found match if it doesn't already exist in the list.
		// This could be slow for large file lists.
		for ( Iterator it = begin(); it != end(); ++it )
			if ( stricmp( (*it).c_str(), fileName ) == 0 )
				return;
		push_back( fileName );
	}

protected:

private:
};
#if 0
    bool expandFileName (const char *fn, Expansion &res) {
        WIN32_FIND_DATAA finddata;
        char buf[4096];
        strcpy (buf, fn);
        char *p = buf + strlen (buf) - 1;
        while (p != buf && *p != '\\') {
            p--;
        }
        *p = '\0';
        bool includedot = false;
        if (p[1] == '.') {
            includedot = true;
        }

        HANDLE h = FindFirstFileA (fn, &finddata);
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }
        res.handle = h;
        do {
            std::string filename = finddata.cFileName;
            if (filename[0] == '.') {
                if (!includedot) {
                    continue;
                }
            }
            std::string name = buf;
            if (name != "") {
                name += "\\";
            }
            name += filename;

            res.names.push_back (name);
        } while (FindNextFileA (h, &finddata) != 0);
        return true ;
    }


#endif
	bool expandFileName (const char *fn, Expansion &res) {
	    FileGlobList glob;

		glob.MatchPattern (fn);
	    for ( FileGlobList::Iterator it = glob.begin(); it != glob.end(); ++it ) {
		    const char* str = (*it).c_str();
		    res.names.push_back (str);
	    }
		return true;
	}

    void freeExpansion (Expansion &x) {
        //FindClose (x.handle);
    }


    int regexCompile (Regex *r, const char *str, int flags) {
        return aikido_regcomp (r, str, flags) ;
    }

    int regexExec (Regex *r, const char *str, int i, RegexMatch *match, int flags) {
       return aikido_regexec (r, str, i, (regmatch_t*)match, flags) ;
    }

    void regexError (int n, Regex *r, char *buffer, int size) {
        aikido_regerror (n, r, buffer, size) ;
    }

    void regexFree (Regex *r) {
        aikido_regfree (r) ;
    }

    __int64 strtoll (const char *str, char **ptr, int base) {
        __int64 val = 0 ;
        switch (base) {
        case 2:
            while (*str == '1' || *str == '0') {
                val = (val << 1) | (*str++ - '0') ;
            }
            break ;
        case 8:
            while (*str >= '0' && *str <= '7') {
                val = (val << 3) | (*str++ - '0') ;
            }
            break ;
        case 10:
        default:
            while (isdigit (*str)) {
                val = (val * 10) + (*str++ - '0') ;
            }
            break ;
        case 16:
            while (isxdigit (*str)) {
                int ch = toupper(*str++) ;
                if (ch >= 'A') {
                    ch -= 'A';
                    ch += 10;
                } else {
                    ch -= '0' ;
                }
                val = (val << 4) | ch ;
            }
            break ;
        }
        if (ptr != NULL) {
            *ptr = (char*)str ;
        }
        return val ;
    } 


    __int64 time1970 ;

    inline __int64 ft2int (const FILETIME &ft) {
        return (__int64)ft.dwHighDateTime << 32 | (__int64)ft.dwLowDateTime ;
    }

    void make1970() {
        SYSTEMTIME stime ;
        FILETIME ftime ;

        if (time1970 == 0) {
            memset (&stime, 0, sizeof (stime)) ;
            stime.wYear = 1970 ;
            stime.wMonth = 1 ;
            stime.wDay = 1 ;
            SystemTimeToFileTime (&stime, &ftime) ;
            time1970 = ft2int (ftime) ;
        }
    }

    void gettimeofday (struct timeval *t) {
        SYSTEMTIME stime ;
        FILETIME ftime ;
        
        if (time1970 == 0) {
            make1970() ;
        }
        GetSystemTime (&stime) ;
        SystemTimeToFileTime (&stime, &ftime) ;
        __int64 intervals = (ft2int(ftime) - time1970) / 10 ;	// microseconds
        t->tv_sec = intervals / 1000000 ;
        t->tv_usec = intervals % 1000000 ;
    }

}

//
// condition variable implementation
//

int OSCondvar::wait (OSMutex &mutex) {
    waiters++ ;
    int c = counter ;
    bool retry = false ;
    for (;;) {
        mutex.unlock() ;
        if (retry) {
            Sleep (1) ;
        } else {
            retry = true ;
        }
        WaitForSingleObject (event, INFINITE) ;
        mutex.lock() ;
        if (tickets != 0 && counter != c) {
            break ;
        }
    }
    waiters-- ;
    tickets-- ;
    if (tickets == 0) {
        ResetEvent (event) ;
    }
	return 0 ;
}

int OSCondvar::timedwait (OSMutex &mutex, const struct timespec *abstime) {
    waiters++ ;
    int c = counter ;
    bool retry = false ;
    unsigned int absmillis = abstime->tv_sec * 1000 + (abstime->tv_nsec / 1000000) ;
	struct timeval now;


    for (;;) {
	    OS::gettimeofday (&now);
        unsigned int nowmillis = now.tv_sec * 1000 + (now.tv_usec / 1000);

        mutex.unlock() ;
        if (retry) {
            Sleep (1) ;
        } else {
            retry = true ;
        }
	    int millis = absmillis - nowmillis;
		if (millis > 0) {
			WaitForSingleObject (event, millis) ;
		}
        mutex.lock() ;
        if (tickets > 0 && counter != c) {
            waiters-- ;
            tickets-- ;
            if (tickets == 0) {
                ResetEvent (event) ;
            }
            return 1;
        }
		if (millis <= 0) {
			waiters--;
			return 0;
		}
    }
}

int OSCondvar::signal() {
    if (waiters > tickets) {
        if (!SetEvent (event)) {
            return 0 ;
        }
        tickets++ ;
        counter++ ;
    }
    return 1 ;
}

int OSCondvar::broadcast() {
    if (waiters > 0) {
        if (!SetEvent (event)) {
            return 0 ;
        }
        tickets = waiters ;
        counter++ ;
    }
    return 1 ;
}




}
