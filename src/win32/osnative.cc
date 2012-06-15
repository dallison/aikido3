/*
 * osnative.cc
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



// this file is included in the middle of native.cc.  

// Change Log:

#include <windows.h>
#include <sys/stat.h>
#include <time.h>
//#include <winsock2.h>
#include <psapi.h>
#include <signal.h>

#undef UNICODE

#include <io.h>
#include "winconsole.h"


namespace aikido {


void initSystemVars(StaticLink *staticLink) {
    char buffer[1024] ;
    DWORD len = 1024 ;

    GetUserNameA (buffer, &len) ;
    setSysVar ("username", buffer, staticLink) ;
    
    setSysVar ("fileSeparator", "\\", staticLink) ;
    setSysVar ("extensionSeparator", ".", staticLink) ;
    setSysVar ("clocksPerSec", (int)CLOCKS_PER_SEC, staticLink) ; // for some reason CLOCKS_PER_SEC is floating point on windows!


    gethostname (buffer, 1024) ;
    char *ch = strchr (buffer, '.') ;
    if (ch != NULL) {
        *ch = 0 ;
        setSysVar ("hostname", buffer, staticLink) ;
        setSysVar ("domainname", ch + 1, staticLink) ;
    } else {
        setSysVar ("hostname", buffer, staticLink) ;
        HKEY key ;
        DWORD len = 256 ;
        DWORD type ;
        LONG r = RegOpenKeyExA (HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Services\\VxD\\MSTCP", 0, KEY_QUERY_VALUE, &key) ;
        if (r != ERROR_SUCCESS) {
            strcpy (buffer, "unknown (can't open key)") ;
            goto baddomain ;
        }
        r = RegQueryValueExA (key, "Domain", 0, &type, (LPBYTE)buffer, &len) ;
        if (r != ERROR_SUCCESS) {
            strcpy (buffer, "unknown (can't read value)") ;
            goto baddomain ;
        }
        if (type != REG_SZ) {
            strcpy (buffer, "unknown (bad type)") ;
            goto baddomain ;
        }
    baddomain:
        RegCloseKey (key) ;
        setSysVar ("domainname", buffer, staticLink) ;
    }

    setSysVar ("pid", GetCurrentProcessId(), staticLink) ;
    setSysVar ("uid", 0, staticLink) ;
    setSysVar ("gid", 0, staticLink) ;
    setSysVar ("ppid", 0, staticLink) ;
    setSysVar ("pgrp", 0, staticLink) ;
    setSysVar ("ppgrp", 0, staticLink) ;

    SYSTEM_INFO sinfo ;
    GetSystemInfo (&sinfo) ;

    OSVERSIONINFO vinfo ;
    vinfo.dwOSVersionInfoSize = sizeof (OSVERSIONINFO) ;
    GetVersionEx (&vinfo) ;

    char os[256] ;
    char build[256] ;
    char release[256] ;
    os[0] = 0 ; build[0] = 0 ; release[0] = 0 ;

    sprintf (os, "Unknown windows") ;
    switch (vinfo.dwMajorVersion) {
    case 4:
        switch (vinfo.dwMinorVersion) {
        case 0:
            sprintf (os, "Windows 95") ;
            break ;
        case 10:
            sprintf (os, "Windows 98") ;
            break ;
        case 90:
            sprintf (os, "Windows Me") ;
            break ;
        }
        break ;
    case 3:
        sprintf (os, "Windows NT 3.51") ;
        break ;
    case 5:
        switch (vinfo.dwMinorVersion) {
        case 0:
            sprintf (os, "Windows 2000") ;
            break ;
        case 1:
            sprintf (os, "Windows XP") ;
            break ;
	    case 2:
            sprintf (os, "Windows 2003") ;
            break ;
        }
    case 6:
        switch (vinfo.dwMinorVersion) {
        case 0:
            sprintf (os, "Windows 2008 or Vista") ;
            break ;
        case 1:
            sprintf (os, "Windows 2008 R2 or Windows 7") ;
            break ;
        }

    }
    
    setSysVar ("operatingsystem",  "Windows", staticLink) ;

    sprintf (buffer, "%s build %d %s", os, vinfo.dwBuildNumber & 0xffff, vinfo.szCSDVersion) ;
    setSysVar ("osinfo",  buffer, staticLink) ;

    sprintf (buffer, "unknown") ;
    setSysVar ("machine",  buffer, staticLink) ;

    setSysVar ("architecture",  buffer, staticLink) ;

    setSysVar ("platform",  buffer, staticLink) ;

    sprintf (buffer, "OEM %d", sinfo.dwOemId) ;
    setSysVar ("manufacturer",  buffer, staticLink) ;

    setSysVar ("serialnumber",  buffer, staticLink) ;


    setSysVar ("hostid", 0, staticLink) ;

    setSysVar ("pagesize", (INTEGER)sinfo.dwPageSize, staticLink) ;

    setSysVar ("numpages", (INTEGER)(((char*)sinfo.lpMaximumApplicationAddress - (char*)sinfo.lpMinimumApplicationAddress) / sinfo.dwPageSize), staticLink) ;

    setSysVar ("numprocessors", (INTEGER)sinfo.dwNumberOfProcessors, staticLink) ;
}

#if 0
HANDLE hChildStdinRd, hChildStdinWr, hChildStdinWrDup, 
   hChildStdoutRd, hChildStdoutWr, hChildStdoutRdDup, 
   hInputFile, hSaveStdin, hSaveStdout; 
 
BOOL CreateChildProcess(VOID); 
VOID WriteToPipe(VOID); 
VOID ReadFromPipe(VOID); 
VOID ErrorExit(LPTSTR); 
VOID ErrMsg(LPTSTR, BOOL); 
 
DWORD main(int argc, char *argv[]) 
{ 
   SECURITY_ATTRIBUTES saAttr; 
   BOOL fSuccess; 
 
// Set the bInheritHandle flag so pipe handles are inherited. 
 
   saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
   saAttr.bInheritHandle = TRUE; 
   saAttr.lpSecurityDescriptor = NULL; 
 
   // The steps for redirecting child process's STDOUT: 
   //     1. Save current STDOUT, to be restored later. 
   //     2. Create anonymous pipe to be STDOUT for child process. 
   //     3. Set STDOUT of the parent process to be write handle to 
   //        the pipe, so it is inherited by the child process. 
   //     4. Create a noninheritable duplicate of the read handle and
   //        close the inheritable read handle. 
 
// Save the handle to the current STDOUT. 
 
   hSaveStdout = GetStdHandle(STD_OUTPUT_HANDLE); 
 
// Create a pipe for the child process's STDOUT. 
 
   if (! CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0)) 
      ErrorExit("Stdout pipe creation failed\n"); 
 
// Set a write handle to the pipe to be STDOUT. 
 
   if (! SetStdHandle(STD_OUTPUT_HANDLE, hChildStdoutWr)) 
      ErrorExit("Redirecting STDOUT failed"); 
 
// Create noninheritable read handle and close the inheritable read 
// handle. 

    fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdoutRd,
        GetCurrentProcess(), &hChildStdoutRdDup , 0,
        FALSE,
        DUPLICATE_SAME_ACCESS);
    if( !fSuccess )
        ErrorExit("DuplicateHandle failed");
    CloseHandle(hChildStdoutRd);

   // The steps for redirecting child process's STDIN: 
   //     1.  Save current STDIN, to be restored later. 
   //     2.  Create anonymous pipe to be STDIN for child process. 
   //     3.  Set STDIN of the parent to be the read handle to the 
   //         pipe, so it is inherited by the child process. 
   //     4.  Create a noninheritable duplicate of the write handle, 
   //         and close the inheritable write handle. 
 
// Save the handle to the current STDIN. 
 
   hSaveStdin = GetStdHandle(STD_INPUT_HANDLE); 
 
// Create a pipe for the child process's STDIN. 
 
   if (! CreatePipe(&hChildStdinRd, &hChildStdinWr, &saAttr, 0)) 
      ErrorExit("Stdin pipe creation failed\n"); 
 
// Set a read handle to the pipe to be STDIN. 
 
   if (! SetStdHandle(STD_INPUT_HANDLE, hChildStdinRd)) 
      ErrorExit("Redirecting Stdin failed"); 
 
// Duplicate the write handle to the pipe so it is not inherited. 
 
   fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdinWr, 
      GetCurrentProcess(), &hChildStdinWrDup, 0, 
      FALSE,                  // not inherited 
      DUPLICATE_SAME_ACCESS); 
   if (! fSuccess) 
      ErrorExit("DuplicateHandle failed"); 
 
   CloseHandle(hChildStdinWr); 
 
// Now create the child process. 
 
   if (! CreateChildProcess()) 
      ErrorExit("Create process failed"); 
 
// After process creation, restore the saved STDIN and STDOUT. 
 
   if (! SetStdHandle(STD_INPUT_HANDLE, hSaveStdin)) 
      ErrorExit("Re-redirecting Stdin failed\n"); 
 
   if (! SetStdHandle(STD_OUTPUT_HANDLE, hSaveStdout)) 
      ErrorExit("Re-redirecting Stdout failed\n"); 
 
   return 0; 
} 
 
BOOL CreateChildProcess() 
{ 
   PROCESS_INFORMATION piProcInfo; 
   STARTUPINFO siStartInfo; 
 
// Set up members of the PROCESS_INFORMATION structure. 
 
   ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );
 
// Set up members of the STARTUPINFO structure. 
 
   ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
   siStartInfo.cb = sizeof(STARTUPINFO); 
 
// Create the child process. 
 
   return CreateProcess(NULL, 
      "child",       // command line 
      NULL,          // process security attributes 
      NULL,          // primary thread security attributes 
      TRUE,          // handles are inherited 
      0,             // creation flags 
      NULL,          // use parent's environment 
      NULL,          // use parent's current directory 
      &siStartInfo,  // STARTUPINFO pointer 
      &piProcInfo);  // receives PROCESS_INFORMATION 
}
 
VOID WriteToPipe(VOID) 
{ 
   DWORD dwRead, dwWritten; 
   CHAR chBuf[BUFSIZE]; 
 
// Read from a file and write its contents to a pipe. 
 
   for (;;) 
   { 
      if (! ReadFile(hInputFile, chBuf, BUFSIZE, &dwRead, NULL) || 
         dwRead == 0) break; 
      if (! WriteFile(hChildStdinWrDup, chBuf, dwRead, 
         &dwWritten, NULL)) break; 
   } 
 
// Close the pipe handle so the child process stops reading. 
 
   if (! CloseHandle(hChildStdinWrDup)) 
      ErrorExit("Close pipe failed\n"); 
} 
 
VOID ReadFromPipe(VOID) 
{ 
   DWORD dwRead, dwWritten; 
   CHAR chBuf[BUFSIZE]; 
   HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE); 

// Close the write end of the pipe before reading from the 
// read end of the pipe. 
 
   if (!CloseHandle(hChildStdoutWr)) 
      ErrorExit("Closing handle failed"); 
 
// Read output from the child process, and write to parent's STDOUT. 
 
   for (;;) 
   { 
      if( !ReadFile( hChildStdoutRdDup, chBuf, BUFSIZE, &dwRead, 
         NULL) || dwRead == 0) break; 
      if (! WriteFile(hSaveStdout, chBuf, dwRead, &dwWritten, NULL)) 
         break; 
   } 
} 
 
VOID ErrorExit (LPTSTR lpszMessage) 
{ 
    throw lpszMessage ;
} 
#endif
 
extern "C" {			// these are located using dlsym, so we need their unmangled name


//
// copy the existing environment into an array of strings and prepend
// new variable definitions.  The OS seems to search these linearly
// to the new definitions will override the old ones
//

char *copyenv (char *strings, int newlen) {
    char *oldenv = (char *)GetEnvironmentStrings() ;
    int len = 0 ;
    char *p = oldenv ;
    while (*p != 0) {
        while (*p != 0) {
           len++ ;
           p++ ;
        }
        p++ ;			// skip 0 at end of string
        len++ ;
    }

    char *newenv = new char [len + newlen + 1] ;
    memcpy (newenv, strings, newlen) ;		// new environment vars first
    p = newenv + newlen ;
    memcpy (p, oldenv, len) ;			// now original env
    newenv[len + newlen] = 0 ;			// terminate with 0
    return newenv ;
}


//
// utility function to format a system error message into
// something (more) meaningful.
//

void formaterror (const char *message, char *str, int len) {
    strcpy (str, message) ;
    strcat (str, ": ") ;
    DWORD err = GetLastError() ;
    char *s = str + strlen (str) ;
    int rem = len - (s - str) ;

    FormatMessageA (FORMAT_MESSAGE_FROM_SYSTEM,
                   0,
                   err,
                   0,
                   s, rem, NULL) ;
}


//
// execute a system call and return the output in a vector of strings
//

AIKIDO_NATIVE(systemEnv) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "system", "Bad command parameter type") ;
        return 0 ;
    }
    if (paras[2].type != T_VECTOR) {
        throw newParameterException (vm, stack, "system", "Bad environment parameter type") ;
        return 0 ;
    }
    if (paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "system", "Bad dir parameter type") ;
        return 0 ;
    }
   

    Value::vector *vec = new Value::vector ;
    const char *command = paras[1].str->c_str() ;
    Value::vector *env = paras[2].vec ;

    // check the environment variables
    int stringsize = 0 ;

    for (Value::vector::iterator i = env->begin() ; i != env->end() ; i++) {
        Value &val = *i ;
        if (val.type != T_STRING) {
	    throw newException (vm, stack,"Illegal environment variable string") ;
        }
        stringsize += val.str->size() + 1 ;
    }

    char *newenv = (char*)GetEnvironmentStrings() ;
    bool freeenv = false ;
    char *envstrings = NULL ;
    if (env->size() != 0) {
        envstrings = new char [stringsize] ;
        char *p = envstrings ;
        for (Value::vector::iterator i = env->begin() ; i != env->end() ; i++) {
            Value &val = *i ;
            strcpy (p, val.str->c_str()) ;
            p += val.str->size() ;
            *p++ = 0 ;
        }

        newenv = copyenv (envstrings, stringsize) ;
        freeenv = true ;
        delete [] envstrings ;
    }

    const char *dir = paras[3].str->c_str() ;
    
    HANDLE hChildStdinRd, hChildStdinWr, hChildStdinWrDup, 
       hChildStdoutRd, hChildStdoutWr, hChildStdoutRdDup, 
       hInputFile, hSaveStdin, hSaveStdout; 

   SECURITY_ATTRIBUTES saAttr; 
   BOOL fSuccess; 
 
// Set the bInheritHandle flag so pipe handles are inherited. 
 
   saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
   saAttr.bInheritHandle = TRUE; 
   saAttr.lpSecurityDescriptor = NULL; 
 
   // The steps for redirecting child process's STDOUT: 
   //     1. Save current STDOUT, to be restored later. 
   //     2. Create anonymous pipe to be STDOUT for child process. 
   //     3. Set STDOUT of the parent process to be write handle to 
   //        the pipe, so it is inherited by the child process. 
   //     4. Create a noninheritable duplicate of the read handle and
   //        close the inheritable read handle. 
 
// Save the handle to the current STDOUT. 
 
   hSaveStdout = GetStdHandle(STD_OUTPUT_HANDLE); 
 
// Create a pipe for the child process's STDOUT. 
 
   if (! CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0)) 
      throw newException (vm, stack, "Stdout pipe creation failed"); 
 
// Set a write handle to the pipe to be STDOUT. 
 
   if (! SetStdHandle(STD_OUTPUT_HANDLE, hChildStdoutWr)) 
      throw newException (vm, stack, "Redirecting STDOUT failed"); 
 
// Create noninheritable read handle and close the inheritable read 
// handle. 

    fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdoutRd,
        GetCurrentProcess(), &hChildStdoutRdDup , 0,
        FALSE,
        DUPLICATE_SAME_ACCESS);
    if( !fSuccess )
        throw newException (vm, stack, "DuplicateHandle failed");
    CloseHandle(hChildStdoutRd);

// Now create the child process. 
 

   PROCESS_INFORMATION piProcInfo; 
   STARTUPINFOA siStartInfo; 
 
// Set up members of the PROCESS_INFORMATION structure. 
 
   ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );
 
// Set up members of the STARTUPINFO structure. 
 
   ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
   siStartInfo.cb = sizeof(STARTUPINFO); 
 
// Create the child process. 
 
   fSuccess = CreateProcessA(NULL, 
      (char*)command,       // command line 
      NULL,          // process security attributes 
      NULL,          // primary thread security attributes 
      TRUE,          // handles are inherited 
      0,             // creation flags 
      newenv,          // use parent's environment 
      dir[0] == 0 ? NULL : (char*)dir,          // use parent's current directory 
      &siStartInfo,  // STARTUPINFO pointer 
      &piProcInfo);  // receives PROCESS_INFORMATION 


   if (! fSuccess) {
      char str[256] ;
      formaterror ("Create process failed", str, 256) ;
      throw newException (vm, stack, str); 
   }
 
// After process creation, restore the saved STDIN and STDOUT. 
 
   if (! SetStdHandle(STD_OUTPUT_HANDLE, hSaveStdout)) 
      throw newException (vm, stack, "Re-redirecting Stdout failed\n"); 
 

    if (freeenv) {
        delete [] newenv ;
    }

// Close the write end of the pipe before reading from the 
// read end of the pipe. 
 
   if (!CloseHandle(hChildStdoutWr)) 
      throw newException (vm, stack, "Closing handle failed"); 
 

   char buf[1024] ;
   DWORD n ;
   string *line = new string ;
   char *p ;
   for (;;) {
       if( !ReadFile( hChildStdoutRdDup, buf, 1024, &n, NULL) || n == 0) 
          break; 
            
       p = buf ;
       bool skiplf = false ;
       while (n > 0) {
           while (n > 0 && *p != '\r') {
               if (skiplf) {
                  skiplf = false ;
                  p++ ;
               } else {
                   *line += *p++ ;
               }
               n-- ;
           }
           if (n >0 && *p == '\r') {
               *line += '\n' ;
               vec->push_back (Value (line)) ;
               line = new string ;
               p++ ;
               n-- ;
               skiplf = true ;
           }
       }
   }

   if (!CloseHandle(hChildStdoutRdDup)) 
      throw newException (vm, stack, "Closing handle failed"); 

   if (line->size() > 0 /**line != ""*/) {			// any left over in line buffer
      *line  += '\n' ;
      vec->push_back (Value (line)) ;
   } else {
       delete line ;
   }
    return vec ;
}

struct PipeData {
    PipeData (HANDLE h, VirtualMachine *v, bool o, Value &or) : handle (h), vm(v), outcharmode (o), outright (or) {}
    HANDLE handle ;
    VirtualMachine *vm ;
    bool outcharmode ;
    Value &outright ;
} ;

void pipeReaderThread (void *arg) {
    PipeData *data = (PipeData*)arg ;
    const int BUFSIZE = 4096 ;
    char buf[BUFSIZE] ;
    DWORD n ;
    string *outline = new string ;
    char *p ;

   n = 0 ;
   ReadFile( data->handle, buf, BUFSIZE, &n, NULL) ;
   do {
      p = buf ;
      bool skiplf = false ;
       while (n > 0) {
           if (data->outcharmode) {
               if (skiplf) {
                  skiplf = false ;
                  p++ ;
               } else {
                   Value ch (*p++) ;
                   data->vm->stream (NULL, ch, data->outright) ;
               }
               n-- ;
           } else {

                while (n > 0 && *p != '\r') {
                   if (skiplf) {
                      skiplf = false ;
                      p++ ;
                   } else {
                      *outline += *p++ ;
                   }
                    n-- ;
                }
                if (n > 0 && *p == '\r') {
                    *outline += '\n' ;
                    Value linev (outline) ;
                    data->vm->stream (NULL, linev, data->outright) ;
                    outline = new string ;
                    p++ ;
                    n-- ;
                    skiplf = true ;
                }
            }
        }
        ReadFile( data->handle, buf, BUFSIZE, &n, NULL) ;
    } while (n > 0) ;

    // add any remaining lines to the stream
    if (!data->outcharmode) {
        if (*outline != "") {			// any left over in line buffer
           *outline  += '\n' ;
           Value linev (outline) ;
           data->vm->stream (NULL, linev, data->outright) ;
        } else {
            delete outline ;
    }
    } else {
        delete outline ;
    }
}


AIKIDO_NATIVE(execEnv) {
    enum {
        COMMAND = 1,
        OUTSTREAM,
        ERRSTREAM,
        ENV,
        DIR
    } ;
    
    if (paras[COMMAND].type != T_STRING) {
        throw newParameterException (vm, stack, "exec", "Bad command parameter type") ;
        return 0 ;
    }
    if (paras[OUTSTREAM].type != T_STREAM && paras[OUTSTREAM].type != T_OBJECT && paras[OUTSTREAM].type != T_VECTOR) {
        throw newParameterException (vm, stack, "exec", "Bad output stream parameter type") ;
        return 0 ;
    }
    if (paras[ERRSTREAM].type != T_STREAM && paras[ERRSTREAM].type != T_OBJECT && paras[ERRSTREAM].type != T_VECTOR) {
        throw newParameterException (vm, stack, "exec", "Bad error stream parameter type") ;
        return 0 ;
    }
    if (paras[ENV].type != T_VECTOR) {
        throw newParameterException (vm, stack, "exec", "Bad environment parameter type") ;
        return 0 ;
    }
    if (paras[DIR].type != T_STRING) {
        throw newParameterException (vm, stack, "exec", "Bad dir parameter type") ;
        return 0 ;
    }
   
    if (paras[OUTSTREAM].type == T_OBJECT) {
        if (vm->checkForOperator (paras[OUTSTREAM], ARROW) == NULL) {
            throw newParameterException (vm, stack, "exec", "Bad output stream parameter type") ;
            return 0 ;
        }
    }

    if (paras[ERRSTREAM].type == T_OBJECT) {
        if (vm->checkForOperator (paras[ERRSTREAM], ARROW) == NULL) {
            throw newParameterException (vm, stack, "exec", "Bad error stream parameter type") ;
            return 0 ;
        }
    }

    Value::vector *vec = new Value::vector ;
    const char *command = paras[COMMAND].str->c_str() ;
    Value::vector *env = paras[ENV].vec ;

    // check the environment variables
    int stringsize = 0 ;

    for (Value::vector::iterator i = env->begin() ; i != env->end() ; i++) {
        Value &val = *i ;
        if (val.type != T_STRING) {
	    throw newException (vm, stack,"Illegal environment variable string") ;
        }
        stringsize += val.str->size() + 1 ;
    }

    char *newenv = (char*)GetEnvironmentStrings() ;
    bool freeenv = false ;
    char *envstrings = NULL ;
    if (env->size() != 0) {
        envstrings = new char [stringsize] ;
        char *p = envstrings ;
        for (Value::vector::iterator i = env->begin() ; i != env->end() ; i++) {
            Value &val = *i ;
            strcpy (p, val.str->c_str()) ;
            p += val.str->size() ;
            *p++ = 0 ;
        }

        newenv = copyenv (envstrings, stringsize) ;
        freeenv = true ;
        delete [] envstrings ;
    }

    const char *dir = paras[DIR].str->c_str() ;
    
    HANDLE hChildStderrRd, hChildStderrWr, hChildStderrWrDup, 
       hChildStdoutRd, hChildStdoutWr, hChildStdoutRdDup, hChildStderrRdDup,
       hInputFile, hSaveStderr, hSaveStdout; 

   SECURITY_ATTRIBUTES saAttr; 
   BOOL fSuccess; 
 
// Set the bInheritHandle flag so pipe handles are inherited. 
 
   saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
   saAttr.bInheritHandle = TRUE; 
   saAttr.lpSecurityDescriptor = NULL; 
 
   // The steps for redirecting child process's STDOUT: 
   //     1. Save current STDOUT, to be restored later. 
   //     2. Create anonymous pipe to be STDOUT for child process. 
   //     3. Set STDOUT of the parent process to be write handle to 
   //        the pipe, so it is inherited by the child process. 
   //     4. Create a noninheritable duplicate of the read handle and
   //        close the inheritable read handle. 
 
// Save the handle to the current STDOUT. 
 
   hSaveStdout = GetStdHandle(STD_OUTPUT_HANDLE); 
   hSaveStderr = GetStdHandle(STD_ERROR_HANDLE); 
 
// Create a pipe for the child process's STDOUT. 
 
   if (! CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0)) 
      throw newException (vm, stack, "Stdout pipe creation failed"); 
 
// Create a pipe for the child process's STDERR. 
 
   if (! CreatePipe(&hChildStderrRd, &hChildStderrWr, &saAttr, 0)) 
      throw newException (vm, stack, "Stderr pipe creation failed"); 
 
// Set a write handle to the pipe to be STDOUT. 
 
   if (! SetStdHandle(STD_OUTPUT_HANDLE, hChildStdoutWr)) 
      throw newException (vm, stack, "Redirecting STDOUT failed"); 
 
// Create noninheritable read handle and close the inheritable read 
// handle. 

    fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdoutRd,
        GetCurrentProcess(), &hChildStdoutRdDup , 0,
        FALSE,
        DUPLICATE_SAME_ACCESS);
    if( !fSuccess )
        throw newException (vm, stack, "DuplicateHandle failed");
    CloseHandle(hChildStdoutRd);

// Set a write handle to the pipe to be STDERR. 
 
   if (! SetStdHandle(STD_ERROR_HANDLE, hChildStderrWr)) 
      throw newException (vm, stack, "Redirecting STDERR failed"); 
 
// Create noninheritable read handle and close the inheritable read 
// handle. 

    fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStderrRd,
        GetCurrentProcess(), &hChildStderrRdDup , 0,
        FALSE,
        DUPLICATE_SAME_ACCESS);
    if( !fSuccess )
        throw newException (vm, stack, "DuplicateHandle failed");
    CloseHandle(hChildStderrRd);

// Now create the child process. 
 

   PROCESS_INFORMATION piProcInfo; 
   STARTUPINFOA siStartInfo; 
 
// Set up members of the PROCESS_INFORMATION structure. 
 
   ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );
 
// Set up members of the STARTUPINFO structure. 
 
   ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
   siStartInfo.cb = sizeof(STARTUPINFO); 
 
// Create the child process. 
 
   fSuccess = CreateProcessA(NULL, 
      (char*)command,       // command line 
      NULL,          // process security attributes 
      NULL,          // primary thread security attributes 
      TRUE,          // handles are inherited 
      0,             // creation flags 
      newenv,          // use parent's environment 
      dir[0] == 0 ? NULL : (char*)dir,          // use parent's current directory 
      &siStartInfo,  // STARTUPINFO pointer 
      &piProcInfo);  // receives PROCESS_INFORMATION 


   if (! fSuccess) {
      char str[256] ;
      formaterror ("Create process failed", str, 256) ;
      throw newException (vm, stack, str); 
   }
 
// After process creation, restore the saved STDIN and STDOUT. 
 
   if (! SetStdHandle(STD_ERROR_HANDLE, hSaveStderr)) 
      throw newException (vm, stack, "Re-redirecting Stderr failed\n"); 
 
   if (! SetStdHandle(STD_OUTPUT_HANDLE, hSaveStdout)) 
      throw newException (vm, stack, "Re-redirecting Stdout failed\n"); 
 

    if (freeenv) {
        delete [] newenv ;
    }

// Close the write end of the pipe before reading from the 
// read end of the pipe. 
 
   if (!CloseHandle(hChildStdoutWr)) 
      throw newException (vm, stack, "Closing handle failed"); 
 
   if (!CloseHandle(hChildStderrWr)) 
      throw newException (vm, stack, "Closing handle failed"); 
 
   Value outright = &paras[OUTSTREAM] ;
   Value errright = &paras[ERRSTREAM] ;


    bool outcharmode = false ;
    bool errcharmode = false ;

    if (paras[OUTSTREAM].type == T_STREAM) {
        Stream *s = paras[OUTSTREAM].stream ;
        if (s->mode == Stream::CHARMODE) {
            outcharmode = true ;
        }
    }

    if (paras[ERRSTREAM].type == T_STREAM) {
        Stream *s = paras[ERRSTREAM].stream ;
        if (s->mode == Stream::CHARMODE) {
            errcharmode = true ;
        }
    }


#if 0
   bool outopen = true ;
   bool erropen = true ;

   const int BUFSIZE = 4096 ;
   char buf[BUFSIZE] ;
   DWORD n ;
   char *p ;
   string *outline = new string ;
   string *errline = new string ;

   while (outopen || erropen) {
      if (outopen) {
          DWORD nbytes ;
          if (!PeekNamedPipe (hChildStdoutRdDup, NULL, 0, NULL, &nbytes, NULL)) {
              outopen = false ;
          } else {
std::cout << "peek: " << nbytes << "\n" ;
              if (nbytes > 0) {
              
                  n = 0 ;
                  ReadFile( hChildStdoutRdDup, buf, BUFSIZE, &n, NULL) ;
                  do {
                     p = buf ;
                     while (n > 0) {
                          if (outcharmode) {
                              Value ch (*p++) ;
                              vm->stream (NULL, ch, outright) ;
                              n-- ;
                          } else {
               
                               while (n > 0 && *p != '\n') {
                                   *outline += *p++ ;
                                   n-- ;
                               }
                               if (n > 0 && *p == '\n') {
                                   *outline += '\n' ;
                                   Value linev (outline) ;
                                   vm->stream (NULL, linev, outright) ;
                                   outline = new string ;
                                   p++ ;
                                   n-- ;
                               }
                           }
                       }
                       ReadFile( hChildStdoutRdDup, buf, BUFSIZE, &n, NULL) ;
                   } while (n > 0) ;

              } else {
                  Sleep (1) ;
              }
          }
      }
      if (erropen) {
          DWORD nbytes ;
          if (!PeekNamedPipe (hChildStderrRdDup, NULL, 0, NULL, &nbytes, NULL)) {
              erropen = false ;
          } else {
              if (nbytes > 0) {
              
                  n = 0 ;
                  ReadFile( hChildStderrRdDup, buf, BUFSIZE, &n, NULL) ;
                  do {
                     p = buf ;
                     while (n > 0) {
                          if (errcharmode) {
                              Value ch (*p++) ;
                              vm->stream (NULL, ch, errright) ;
                              n-- ;
                          } else {
               
                               while (n > 0 && *p != '\n') {
                                   *errline += *p++ ;
                                   n-- ;
                               }
                               if (n > 0 && *p == '\n') {
                                   *errline += '\n' ;
                                   Value linev (errline) ;
                                   vm->stream (NULL, linev, errright) ;
                                   errline = new string ;
                                   p++ ;
                                   n-- ;
                               }
                           }
                       }
                       ReadFile( hChildStderrRdDup, buf, BUFSIZE, &n, NULL) ;
                   } while (n > 0) ;

              } else {
                  Sleep (1) ;
              }
          }
      }
   }

    // add any remaining lines to the stream
    if (!outcharmode) {
        if (*outline != "") {			// any left over in line buffer
           *outline  += '\n' ;
           Value linev (outline) ;
           vm->stream (NULL, linev, outright) ;
        } else {
            delete outline ;
    }
    } else {
        delete outline ;
    }

    if (!errcharmode) {
        if (*errline != "") {			// any left over in line buffer
           *errline  += '\n' ;
           Value linev (errline) ;
           vm->stream (NULL, linev, errright) ;
        } else {
            delete errline ;
    }
    } else {
        delete errline ;
    }

#else
   PipeData outdata (hChildStdoutRdDup, vm, outcharmode, outright) ;
   PipeData errdata (hChildStderrRdDup, vm, errcharmode, errright) ;
   
   DWORD outid ;
   DWORD errid ;
   HANDLE handles[2] ;

   handles[0] = CreateThread (NULL, 0, (LPTHREAD_START_ROUTINE)pipeReaderThread, &outdata, 0, &outid) ;
   handles[1] = CreateThread (NULL, 0, (LPTHREAD_START_ROUTINE)pipeReaderThread, &errdata, 0, &errid) ;

   WaitForSingleObject (handles[0], INFINITE) ;
   WaitForSingleObject (handles[1], INFINITE) ;

#endif

   if (!CloseHandle(hChildStdoutRdDup)) 
      throw newException (vm, stack, "Closing handle failed"); 
   if (!CloseHandle(hChildStderrRdDup)) 
      throw newException (vm, stack, "Closing handle failed"); 

    return 0 ;
}

// like execEnv except return stream to be used as standard input to command

AIKIDO_NATIVE(pipeEnv) {
    enum {
        COMMAND = 1,
        REDIRECTERR,
        ENV,
        DIR
    } ;
    
    if (paras[COMMAND].type != T_STRING) {
        throw newParameterException (vm, stack, "pipe", "Bad command parameter type") ;
        return 0 ;
    }
    if (!isIntegral (paras[REDIRECTERR])) {
        throw newParameterException (vm, stack, "pipe", "Bad redirectStderr parameter type") ;
        return 0 ;
    }
    if (paras[ENV].type != T_VECTOR) {
        throw newParameterException (vm, stack, "pipe", "Bad environment parameter type") ;
        return 0 ;
    }
    if (paras[DIR].type != T_STRING) {
        throw newParameterException (vm, stack, "pipe", "Bad dir parameter type") ;
        return 0 ;
    }
   
    bool redirectstderr = getInt (paras[REDIRECTERR]) != 0 ;

    Value::vector *vec = new Value::vector ;
    const char *command = paras[COMMAND].str->c_str() ;
    Value::vector *env = paras[ENV].vec ;

    // check the environment variables
    int stringsize = 0 ;

    for (Value::vector::iterator i = env->begin() ; i != env->end() ; i++) {
        Value &val = *i ;
        if (val.type != T_STRING) {
	    throw newException (vm, stack,"Illegal environment variable string") ;
        }
        stringsize += val.str->size() + 1 ;
    }

    char *newenv = (char*)GetEnvironmentStrings() ;
    bool freeenv = false ;
    char *envstrings = NULL ;
    if (env->size() != 0) {
        envstrings = new char [stringsize] ;
        char *p = envstrings ;
        for (Value::vector::iterator i = env->begin() ; i != env->end() ; i++) {
            Value &val = *i ;
            strcpy (p, val.str->c_str()) ;
            p += val.str->size() ;
            *p++ = 0 ;
        }

        newenv = copyenv (envstrings, stringsize) ;
        freeenv = true ;
        delete [] envstrings ;
    }

    const char *dir = paras[DIR].str->c_str() ;
    
    HANDLE hChildStdinRd, hChildStdinWr, hChildStdinWrDup, 
       hChildStdoutRd, hChildStdoutWr, hChildStdoutRdDup, 
       hInputFile, hSaveStdin, hSaveStdout; 

   SECURITY_ATTRIBUTES saAttr; 
   BOOL fSuccess; 
 
// Set the bInheritHandle flag so pipe handles are inherited. 
 
   saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
   saAttr.bInheritHandle = TRUE; 
   saAttr.lpSecurityDescriptor = NULL; 
 
   // The steps for redirecting child process's STDOUT: 
   //     1. Save current STDOUT, to be restored later. 
   //     2. Create anonymous pipe to be STDOUT for child process. 
   //     3. Set STDOUT of the parent process to be write handle to 
   //        the pipe, so it is inherited by the child process. 
   //     4. Create a noninheritable duplicate of the read handle and
   //        close the inheritable read handle. 
 
// Save the handle to the current STDOUT. 
 
   hSaveStdout = GetStdHandle(STD_OUTPUT_HANDLE); 
 
// Create a pipe for the child process's STDOUT. 
 
   if (! CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0)) 
      throw newException (vm, stack, "Stdout pipe creation failed"); 
 
// Set a write handle to the pipe to be STDOUT. 
 
   if (! SetStdHandle(STD_OUTPUT_HANDLE, hChildStdoutWr)) 
      throw newException (vm, stack, "Redirecting STDOUT failed"); 
 
// Create noninheritable read handle and close the inheritable read 
// handle. 

    fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdoutRd,
        GetCurrentProcess(), &hChildStdoutRdDup , 0,
        FALSE,
        DUPLICATE_SAME_ACCESS);
    if( !fSuccess )
        throw newException (vm, stack, "DuplicateHandle failed");
    CloseHandle(hChildStdoutRd);

   // The steps for redirecting child process's STDIN: 
   //     1.  Save current STDIN, to be restored later. 
   //     2.  Create anonymous pipe to be STDIN for child process. 
   //     3.  Set STDIN of the parent to be the read handle to the 
   //         pipe, so it is inherited by the child process. 
   //     4.  Create a noninheritable duplicate of the write handle, 
   //         and close the inheritable write handle. 
 
// Save the handle to the current STDIN. 
 
   hSaveStdin = GetStdHandle(STD_INPUT_HANDLE); 
 
// Create a pipe for the child process's STDIN. 
 
   if (! CreatePipe(&hChildStdinRd, &hChildStdinWr, &saAttr, 0)) 
      throw newException (vm, stack, "Stdin pipe creation failed\n"); 
 
// Set a read handle to the pipe to be STDIN. 
 
   if (! SetStdHandle(STD_INPUT_HANDLE, hChildStdinRd)) 
      throw newException (vm, stack, "Redirecting Stdin failed"); 
 
// Duplicate the write handle to the pipe so it is not inherited. 
 
   fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdinWr, 
      GetCurrentProcess(), &hChildStdinWrDup, 0, 
      FALSE,                  // not inherited 
      DUPLICATE_SAME_ACCESS); 
   if (! fSuccess) 
      throw newException (vm, stack, "DuplicateHandle failed"); 
 
   CloseHandle(hChildStdinWr); 
 
  PipeStream *pipestream = new PipeStream (_open_osfhandle ((DWORD)hChildStdoutRdDup, O_RDONLY), _open_osfhandle ((DWORD)hChildStdinWrDup, O_APPEND)) ;

// Now create the child process. 
 

   PROCESS_INFORMATION piProcInfo; 
   STARTUPINFOA siStartInfo; 
 
// Set up members of the PROCESS_INFORMATION structure. 
 
   ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );
 
// Set up members of the STARTUPINFO structure. 
 
   ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
   siStartInfo.cb = sizeof(STARTUPINFO); 
 
// Create the child process. 
 
   fSuccess = CreateProcessA(NULL, 
      (char*)command,       // command line 
      NULL,          // process security attributes 
      NULL,          // primary thread security attributes 
      TRUE,          // handles are inherited 
      0,             // creation flags 
      newenv,          // use parent's environment 
      dir[0] == 0 ? NULL : (char*)dir,          // use parent's current directory 
      &siStartInfo,  // STARTUPINFO pointer 
      &piProcInfo);  // receives PROCESS_INFORMATION 


   if (! fSuccess) {
      char str[256] ;
      formaterror ("Create process failed", str, 256) ;
      throw newException (vm, stack, str); 
   }
 
// After process creation, restore the saved STDIN and STDOUT. 
 
   if (! SetStdHandle(STD_INPUT_HANDLE, hSaveStdin)) 
      throw newException (vm, stack, "Re-redirecting Stdin failed\n"); 
 
   if (! SetStdHandle(STD_OUTPUT_HANDLE, hSaveStdout)) 
      throw newException (vm, stack, "Re-redirecting Stdout failed\n"); 
 

    if (freeenv) {
        delete [] newenv ;
    }

// Close the write end of the pipe before reading from the 
// read end of the pipe. 
 
   if (!CloseHandle(hChildStdoutWr)) 
      throw newException (vm, stack, "Closing handle failed"); 
 

    pipestream->setThreadID((OSThread_t)piProcInfo.hProcess) ;

    return pipestream ;
}

AIKIDO_NATIVE(pipeclose) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "pipeclose", "Bad stream type") ;
        return 0 ;
    }
    Stream *s = paras[1].stream ;
    HANDLE handle = (HANDLE)s->getThreadID() ;
    s->close() ;
    WaitForSingleObject (handle, INFINITE) ;
//  XXX: process return code
    return 0 ;
}

AIKIDO_NATIVE(pipewait) {
    if (paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "pipewait", "Bad stream type") ;
        return 0 ;
    }
    Stream *s = paras[1].stream ;
    HANDLE handle = (HANDLE)s->getThreadID() ;
    WaitForSingleObject (handle, INFINITE) ;
//  XXX: process return code
    return 0 ;
}

AIKIDO_NATIVE(stat) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "stat", "Invalid filename") ;
    }

    struct stat st ;

    if (stat (paras[1].str->c_str(), &st) != 0) {
        return Value ((Object*)NULL) ;
    }

    Object *statobj = new (statClass->stacksize) Object (statClass, staticLink, stack, stack->inst) ;
    statobj->ref++ ;

    statobj->varstack[0] = Value (statobj) ;
    // construct the object

    vm->execute (statClass, statobj, staticLink, 0) ;

    statobj->varstack[1] = Value((UINTEGER)st.st_mode) ;
    statobj->varstack[2] = Value((UINTEGER)st.st_ino) ;
    statobj->varstack[3] = Value((UINTEGER)st.st_dev) ;
    statobj->varstack[4] = Value((UINTEGER)st.st_rdev) ;
    statobj->varstack[5] = Value((UINTEGER)st.st_nlink) ;
    statobj->varstack[6] = Value((UINTEGER)st.st_uid) ;
    statobj->varstack[7] = Value((UINTEGER)st.st_gid) ;
    statobj->varstack[8] = Value((UINTEGER)st.st_size) ;
    statobj->varstack[9] = Value((INTEGER)st.st_atime * 1000000) ;
    statobj->varstack[10] = Value((INTEGER)st.st_mtime * 1000000) ;
    statobj->varstack[11] = Value((INTEGER)st.st_ctime * 1000000) ;
    statobj->varstack[12] = Value((UINTEGER)0) ;
    statobj->varstack[13] = Value((UINTEGER)0) ;
    statobj->ref-- ;

    return statobj ;
}


static HKEY getprofilelist (VirtualMachine *vm, StackFrame *stack, std::vector<std::string> &profiles) {
    HKEY key ;
    LONG r = RegOpenKeyExA (HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\ProfileList",0, KEY_QUERY_VALUE, &key) ;
    if (r != ERROR_SUCCESS) {
        throw newException (vm, stack, "Unable to open profile list key in registry") ;
    }
    DWORD index = 0 ;
    FILETIME time ;
    char buf[1024] ;
    do {
        DWORD len = 1024 ;
        r = RegEnumKeyExA (key, index, buf, &len, 0, NULL, NULL, &time) ;
        if (r == ERROR_SUCCESS) {
            profiles.push_back (buf) ;
            index++ ;
        }
    } while (r == ERROR_SUCCESS) ;
    return key ;
}

static std::string getUserDir (VirtualMachine *vm, StackFrame *stack, HKEY key, const std::string &profilename) {
    LONG r = RegOpenKeyExA (key, profilename.c_str(), 0, KEY_QUERY_VALUE, &key) ;
    if (r != ERROR_SUCCESS) {
        throw newException (vm, stack, "Unable to open profile name key") ;
    }
    char buf[256] ;
    DWORD len = 256 ;
    DWORD type ;
    r = RegQueryValueExA (key, "ProfileImagePath", 0, &type, (LPBYTE)buf, &len) ;
    if (r != ERROR_SUCCESS) {
        throw newException (vm, stack, "Unable to query ProfileImagePath value") ;
    }
    if (type != REG_SZ) {
        throw newException (vm, stack, "Bad data type for ProfileImagePath value") ;
    }
    RegCloseKey (key) ;
    return buf ;
}


AIKIDO_NATIVE(getUser) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "getUser", "Invalid username") ;
    }


    // static link for stat object is same as for stat function (System block)

    Object *userobj = new (userClass->stacksize) Object (userClass, staticLink, stack, stack->inst) ;
    userobj->ref++ ;

    userobj->varstack[0] = Value (userobj) ;
    // construct the object

    vm->execute (userClass, userobj, staticLink, 0) ;

    std::vector<std::string> users ;
    HKEY key = getprofilelist (vm, stack, users) ;

    bool found = false ;
    for (int i = 0 ; i < users.size() ; i++) {
        if (paras[1].str->str == users[i]) {
            found = true ;
            break ;
        }
    }

    if (!found) {
        RegCloseKey (key) ;
        return Value((Object*)NULL) ;
    }
    std::string dir = getUserDir (vm, stack, key, paras[1].str->str) ;

    RegCloseKey (key) ;

    userobj->varstack[1] = paras[1] ;
    userobj->varstack[2] = 0 ; // pwd.pw_uid ;
    userobj->varstack[3] = 0 ; // pwd.pw_gid ;
    userobj->varstack[4] = 0 ; // pwd.pw_gecos ;
    userobj->varstack[5] = Value(new string (dir)) ; // pwd.pw_dir ;
    userobj->varstack[6] = "MS DOS" ; // pwd.pw_shell ;
    userobj->varstack[7] = "" ; // spwd.sp_pwdp ;		// password

    userobj->ref-- ;


    return userobj ;
}

AIKIDO_NATIVE(readdir) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "readdir", "Invalid directory name") ;
        return 0 ;
    }
    WIN32_FIND_DATAA file ;
    HANDLE handle ;


    Value::vector *vec = new Value::vector ;

    string pattern = *paras[1].str + "\\*" ;
    handle = FindFirstFileA (pattern.c_str(), &file) ;
    if (handle != INVALID_HANDLE_VALUE) {
        bool more ;
        do {
            if (strcmp (file.cFileName, ".") != 0 &&
                strcmp (file.cFileName, "..") != 0) {
                vec->push_back (Value (string (file.cFileName))) ;
            }
            more = FindNextFileA (handle, &file)  ;
        } while (more) ;
    }

    return vec ;
}

AIKIDO_NATIVE(chdir) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "chdir", "Invalid directory name") ;
        return 0 ;
    }
    BOOL r = SetCurrentDirectoryA (paras[1].str->c_str()) ;
    if (!r) {
       throw newException (vm, stack, "Unable to set current directory") ;
    }
    return 0 ;
}

AIKIDO_NATIVE(getwd) {
    char buffer[4096] ;
    GetCurrentDirectoryA (4096, buffer) ;
    return new string (buffer) ;
}

// get the time of day in microseconds UTC

AIKIDO_NATIVE(time) {
    struct timeval t ;
    OS::gettimeofday (&t) ;
    return (INTEGER)t.tv_sec * 1000000 + t.tv_usec ;
}

static Value toDateObject (VirtualMachine *vm, StaticLink *staticLink, StackFrame *stack, struct tm *tm) {
    // static link for date object is same as for date function (System block)

    Object *dateobj = new (dateClass->stacksize) Object (dateClass, staticLink, stack, stack->inst) ;
    dateobj->ref++ ;

    dateobj->varstack[0] = Value (dateobj) ;

    // construct the object

    vm->execute (dateClass, dateobj, staticLink, 0) ;

    dateobj->varstack[1] = Value("") ;          // parameter
    dateobj->varstack[2] = Value (tm->tm_sec) ;
    dateobj->varstack[3] = Value (tm->tm_min) ;
    dateobj->varstack[4] = Value (tm->tm_hour) ;
    dateobj->varstack[5] = Value (tm->tm_mday) ;
    dateobj->varstack[6] = Value (tm->tm_mon) ;
    dateobj->varstack[7] = Value (tm->tm_year) ;
    dateobj->varstack[8] = Value (tm->tm_wday) ;
    dateobj->varstack[9] = Value (tm->tm_yday) ;
    dateobj->varstack[10] = Value (tm->tm_isdst) ;
    dateobj->varstack[11] = timezone ;
    dateobj->varstack[12] = tzname[tm->tm_isdst > 0 ? 1 : 0] ;

    dateobj->ref-- ;
    return dateobj ;
}

AIKIDO_NATIVE(initdate) {
    if (paras[1].type != T_OBJECT) {
        throw newParameterException (vm, stack, "initdate", "Need a Date object");
    }

    Object *dateobj = paras[1].object;
    time_t clock = time (NULL) ;
    struct tm *tm = localtime (&clock) ;

    dateobj->varstack[2] = Value (tm->tm_sec) ;
    dateobj->varstack[3] = Value (tm->tm_min) ;
    dateobj->varstack[4] = Value (tm->tm_hour) ;
    dateobj->varstack[5] = Value (tm->tm_mday) ;
    dateobj->varstack[6] = Value (tm->tm_mon) ;
    dateobj->varstack[7] = Value (tm->tm_year) ;
    dateobj->varstack[8] = Value (tm->tm_wday) ;
    dateobj->varstack[9] = Value (tm->tm_yday) ;
    dateobj->varstack[10] = Value (tm->tm_isdst) ;
    dateobj->varstack[11] = timezone ;
    dateobj->varstack[12] = tzname[tm->tm_isdst > 0 ? 1 : 0] ;
    return dateobj ;
}


AIKIDO_NATIVE(date) {
    time_t clock = time (NULL) ;
    struct tm *tm = localtime (&clock) ;
 
    return toDateObject (vm, staticLink, stack, tm) ;
}

AIKIDO_NATIVE(gmdate) {
    time_t clock = time (NULL) ;
    struct tm *tm = gmtime (&clock) ;
 
    return toDateObject (vm, staticLink, stack, tm) ;
}

AIKIDO_NATIVE(makedate) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "makedate", "Invalid time parameter") ;
    }
    time_t clock = (time_t)(getInt(paras[1]) / 1000000) ;               // make into seconds
    struct tm *tm = localtime (&clock) ;
 
    return toDateObject (vm, staticLink, stack, tm) ;
}

AIKIDO_NATIVE(makegmdate) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "makegmdate", "Invalid time parameter") ;
    }
    time_t clock = (time_t)(getInt(paras[1]) / 1000000) ;               // make into seconds
    struct tm *tm = gmtime (&clock) ;
 
    return toDateObject (vm, staticLink, stack, tm) ;
}


static int string2resource (VirtualMachine *vm, StackFrame *stack, string &resname) {
    int resource ;
    if (resname == "cputime") {
        resource = 0 ;
    } else if (resname == "filesize") {
        resource = 1 ;
    } else if (resname == "datasize") {
        resource = 2 ;
    } else if (resname == "stacksize") {
        resource = 3 ;
    } else if (resname == "coredumpsize") {
        resource = 4 ;
    } else if (resname == "descriptors") {
        resource = 5 ;
    } else if (resname == "memorysize") {
        resource = 6 ;
    } else {
        throw newParameterException (vm, stack, "get/setlimit", "Invalid resource name") ;
    }
    return resource ;
}


static int limits [7] ;

AIKIDO_NATIVE (setlimit) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "setlimit", "Invalid resource name type") ;
        return 0 ;
    }
    if (paras[2].type != T_INTEGER) {
        throw newParameterException (vm, stack, "setlimit", "Invalid resource value type") ;
        return 0 ;
    }
    int lim = string2resource (vm, stack, *paras[1].str) ;
    limits[lim] = paras[2].integer ;
    return 0 ;
}

AIKIDO_NATIVE (getlimit) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "getlimit", "Invalid resource name type") ;
        return 0 ;
    }
    int lim = string2resource (vm, stack, *paras[1].str) ;
    return limits[lim] ;
}

AIKIDO_NATIVE (getenv) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "getenv", "invalid environment variable") ;
    }
    const char*r = ::getenv (paras[1].str->c_str()) ;
    if (r == NULL) {
        return (Object*)NULL ;
    }
    return r ;
}

AIKIDO_NATIVE (setenv) {
    if (paras[1].type != T_STRING || paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "setenv", "invalid environment variable or value") ;
    }
    char *buf ;				// cannot be automatic for putenv
    const char *name = paras[1].str->c_str() ;
    const char *value = paras[2].str->c_str() ;

    buf = new char[strlen (name) + strlen (value) + 2] ;
    sprintf (buf, "%s=%s", name, value) ;
    ::putenv (buf) ;			// makes the arg part of the environment
    return 0 ;
}


//
// perform a select operation on a socket
//
// args:
//   1: stream
//   2: timeout in microseconds
//
// returns:
//   true if there is data ready on the stream
//

AIKIDO_NATIVE (select) {
    int fd = paras[1].stream->getInputFD() ;
    if (paras[1].stream->availableChars() > 0) {
        return 1 ;
    }
    struct timeval timeout ;
    timeout.tv_sec = paras[2] / 1000000 ;
    timeout.tv_usec = paras[2] % 1000000 ;
    fd_set fds ;
    FD_ZERO (&fds) ;
    FD_SET (fd, &fds) ;
    int n = select (256, &fds, NULL, NULL, &timeout) ;
    if (n < 0) {
        throw newException (vm, stack, strerror (errno)) ;
    }
    if (n == 0) {
        return 0 ;
    }
    if (FD_ISSET (fd, &fds)) {
        return 1 ;
    }
    return 0 ;
}


AIKIDO_NATIVE (kill) {
    if (!isIntegral (paras[1]) && paras[1].type != T_STREAM) {
        throw newParameterException (vm, stack, "kill", "Illegal pid type") ;
    }
    if (!isIntegral (paras[2])) {
        throw newParameterException (vm, stack, "kill", "Illegal signal type") ;
    }
    if (paras[1].type == T_STREAM) {
        int pid = (int)paras[1].stream->getThreadID() ;
		TerminateProcess ((HANDLE)pid, 0);
    } else {
		TerminateProcess ((HANDLE)getInt(paras[1]), 0);
    }
    return 0 ;
}

AIKIDO_NATIVE (sigset) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigset", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > NSIG) {
        throw newParameterException (vm, stack, "sigset", "Illegal signal value") ;
    }

    void *result ;
    Closure *prev ;

    if (paras[2].type == T_CLOSURE && paras[2].closure->type == T_FUNCTION) {
        Function *f = (Function*)paras[2].closure->block ;
        if (f->isNative()) {
            throw newParameterException (vm, stack, "sigset", "Illegal signal handler - function can't be native") ;
        }
        InterpretedBlock *func = reinterpret_cast<InterpretedBlock*>(f) ;
        if (func->parameters.size() != 1) {
            throw newParameterException (vm, stack, "sigset", "Illegal signal handler - function has incorrect number of parameters") ;
        }
        parserLock.acquire(true) ;
        prev = signals[sig] ;
        signals[sig] = paras[2].closure ;
        incRef (paras[2].closure, closure) ;
        parserLock.release(true) ;

        result = signal (sig, signalHit) ;          // set the signal handler
    } else if (paras[2].type == T_INTEGER) {
        void *handler = (void*)paras[2].integer ;
        parserLock.acquire(true) ;
        prev = signals[sig] ;
        signals[sig] = NULL ;
        parserLock.release(true) ;

        result = signal (sig, (void (*)(int))handler) ;
    } else {
       throw newParameterException (vm, stack, "sigset", "Illegal signal handler type") ;
    }

    if (result == signalHit) {		// had we previously handled the signal?
        if (prev == NULL) {
            return (Object*)NULL ;
        } else {
            return prev ;
        }

    } else if (result == SIG_ERR) {
        throw newException (vm, stack, string ("Error setting signal: ") + strerror(errno)) ;
    } else {
        return (INTEGER)result ;
    }

    return 0 ;
}

AIKIDO_NATIVE (sigrelse) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigrelse", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > NSIG) {
        throw newParameterException (vm, stack, "sigrelse", "Illegal signal value") ;
    }
    signal (sig, SIG_DFL) ;
    return 0 ;
}

AIKIDO_NATIVE (sighold) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sighold", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > NSIG) {
        throw newParameterException (vm, stack, "sighold", "Illegal signal value") ;
    }
    //sighold (sig) ;
    return 0 ;
}

AIKIDO_NATIVE (sigignore) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigignore", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > NSIG) {
        throw newParameterException (vm, stack, "sigignore", "Illegal signal value") ;
    }
    //sigignore (sig) ;
    return 0 ;
}

AIKIDO_NATIVE (sigpause) {
    if (!isIntegral (paras[1])) {
        throw newParameterException (vm, stack, "sigpause", "Illegal signal type") ;
    }
    int sig = getInt (paras[1]) ;
    if (sig < 0 || sig > MAXSIGS) {
        throw newParameterException (vm, stack, "sigpause", "Illegal signal value") ;
    }
    //sigpause (sig) ;
    return 0 ;
}


AIKIDO_NATIVE (rename) {
    if (paras[1].type != T_STRING || paras[2].type != T_STRING) {
        throw newParameterException (vm, stack, "rename", "Illegal filename type") ;
    }
	LPCTSTR  oldname = paras[1].str->c_str();
	LPCTSTR  newname = paras[2].str->c_str();
	if (MoveFileEx (oldname, newname, MOVEFILE_COPY_ALLOWED|MOVEFILE_REPLACE_EXISTING)) {
		return 0;
	}
	throw newException (vm, stack, "Failed to rename file");

    return 0 ;
}

AIKIDO_NATIVE (remove) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "remove", "Illegal filename type") ;
    }
	LPCTSTR  name = paras[1].str->c_str();
	if (DeleteFile (name)) {
		return 0;
	}
	throw newException (vm, stack, "Failed to remove file");

#if 0
    int err = remove (paras[1].str->c_str()) ;
2         throw newException (vm, stack, strerror (errno)) ;
    }
#endif
    return 0 ;
}

AIKIDO_NATIVE (mkdir1) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "mkdir", "Illegal filename type") ;
    }
    if (!isIntegral (paras[2].type)) {
        throw newParameterException (vm, stack, "mkdir", "Illegal mode type") ;
    }
	LPCTSTR  name = paras[1].str->c_str();
	if (CreateDirectory (name, 0)) {
		return 0;
	}
	throw newException (vm, stack, "Failed to create directory");


    return 0 ;
}

AIKIDO_NATIVE (mknod) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "mknod", "Illegal filename type") ;
    }
    if (!isIntegral (paras[2].type)) {
        throw newParameterException (vm, stack, "mknod", "Illegal mode type") ;
    }
#if 0
    int err = mknod (paras[1].str->c_str(), getInt (paras[2]), 0) ;
    if (err == -1) {
         throw newException (vm, stack, strerror (errno)) ;
    }
#endif
    return 0 ;
}

AIKIDO_NATIVE (tmpnam) {
    char buffer[256] ;
    tmpnam_s (buffer,256) ;
    return buffer ;
}

AIKIDO_NATIVE(lstat) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "stat", "Invalid filename") ;
    }
    struct stat st ;

    std::string filename = lockFilename (b, paras[1]) ;

    if (stat (filename.c_str(), &st) != 0) {
        return Value ((Object*)NULL) ;
    }

    // static link for stat object is same as for stat function (System block)

    Object *statobj = new (statClass->stacksize) Object (statClass, staticLink, stack, stack->inst) ;
    statobj->ref++ ;

    statobj->varstack[0] = Value (statobj) ;
    // construct the object

    vm->execute (statClass, statobj, staticLink, 0) ;

    statobj->varstack[1] = Value((UINTEGER)st.st_mode) ;
    statobj->varstack[2] = Value((UINTEGER)st.st_ino) ;
    statobj->varstack[3] = Value((UINTEGER)st.st_dev) ;
    statobj->varstack[4] = Value((UINTEGER)st.st_rdev) ;
    statobj->varstack[5] = Value((UINTEGER)st.st_nlink) ;
    statobj->varstack[6] = Value((UINTEGER)st.st_uid) ;
    statobj->varstack[7] = Value((UINTEGER)st.st_gid) ;
    statobj->varstack[8] = Value((UINTEGER)st.st_size) ;
    statobj->varstack[9] = Value((INTEGER)st.st_atime * 1000000) ;
    statobj->varstack[10] = Value((INTEGER)st.st_mtime * 1000000) ;
    statobj->varstack[11] = Value((INTEGER)st.st_ctime * 1000000) ;
    statobj->varstack[12] = Value((UINTEGER)0) ;
    statobj->varstack[13] = Value((UINTEGER)0) ;
	statobj->ref-- ;

    return statobj ;
}

AIKIDO_NATIVE(ttyname) {
    return 0;
}

AIKIDO_NATIVE(chown) {
	throw newException (vm,stack, "Not implemented in Windows");
    return 0;
}

AIKIDO_NATIVE(readlink) {
	throw newException (vm,stack, "Not implemented in Windows");
    return 0;
}

AIKIDO_NATIVE(symlink) {
#if 0
	LPCTSTR  oldname = paras[1].str->c_str();
	LPCTSTR  newname = paras[2].str->c_str();
	if (CreateSymbolicLink (oldname, newname, 0)) {
		return 0;
	}
	throw newException (vm, stack, "Failed to create symbolic link");
    return 0;
#else
		throw newException (vm,stack, "Not implemented in Windows");

#endif
}

AIKIDO_NATIVE(fork) {
 	throw newException (vm,stack, "Not implemented in Windows");
   return 0;
}

AIKIDO_NATIVE(waitpid) {
 	throw newException (vm,stack, "Not implemented in Windows");
   return 0;
}

AIKIDO_NATIVE(opentty) {
 	throw newException (vm,stack, "Not implemented in Windows");
   return 0;
}

AIKIDO_NATIVE(closetty) {
 	throw newException (vm,stack, "Not implemented in Windows");
   return 0;
}

AIKIDO_NATIVE (raw_open) {
    if (paras[1].type != T_STRING) {
        throw newParameterException (vm, stack, "raw_open", "Illegal filename type") ;
    }
    if (!isIntegral (paras[2].type)) {
        throw newParameterException (vm, stack, "raw_open", "Illegal flags type") ;
    }
    if (!isIntegral (paras[3].type)) {
        throw newParameterException (vm, stack, "raw_open", "Illegal mode type") ;
    }
    std::string filename = lockFilename (b, paras[1]) ;
    int flags = getInt (paras[2]) ;
    int mode = getInt (paras[3]) ;

    int fd = open (filename.c_str(), flags, mode) ;
    if (fd < 0) {
        throw newException (vm, stack, "Failed to open file") ;
    }
    return fd ;
}

AIKIDO_NATIVE (raw_close) {
    if (!isIntegral (paras[1].type)) {
        throw newParameterException (vm, stack, "raw_close", "Illegal fd type") ;
    }
    ::close (getInt (paras[1])) ;
    return 0 ;
}


AIKIDO_NATIVE (raw_read) {
    if (!isIntegral (paras[1].type)) {
        throw newParameterException (vm, stack, "raw_read", "Illegal fd type") ;
    }
    if (!isIntegral (paras[2].type)) {
        throw newParameterException (vm, stack, "raw_read", "Illegal size type") ;
    }
    int fd = getInt (paras[1]) ;
    int size =  getInt (paras[2]) ;
    Value::bytevector *vec = new Value::bytevector() ;
    char *buf = new char[size] ;
    int n = ::read (fd, buf, size) ;
    if (n == 0) {
        delete [] buf ;
        return vec ;
    }
    if (n < 0) {
        delete [] buf ;
        throw newException (vm, stack, "Read failed") ;
    }
    for (int i = 0 ; i < n ; i++) {
        vec->push_back (buf[i]) ;
    }
    delete [] buf ;
    return vec ;
}


AIKIDO_NATIVE (raw_write) {
    if (!isIntegral (paras[1].type)) {
        throw newParameterException (vm, stack, "raw_write", "Illegal fd type") ;
    }
    if (paras[2].type != T_STRING && paras[2].type != T_BYTEVECTOR) {
        throw newParameterException (vm, stack, "raw_write", "Illegal buf type") ;
    }

    if (!isIntegral (paras[3].type)) {
        throw newParameterException (vm, stack, "raw_write", "Illegal size type") ;
    }
    int fd = getInt (paras[1]) ;
    int size =  getInt (paras[3]) ;
    const char *buf ;
    bool deletebuf = false ;
    if (paras[2].type == T_STRING) {
        buf = paras[2].str->c_str() ;
    } else {
        deletebuf = true ;
        char *b = new char [paras[2].bytevec->vec.size()] ;
        for (int i = 0 ; i < paras[2].bytevec->vec.size() ; i++) {
            *b++ = paras[2].bytevec->vec[i] ;
        }
        buf = const_cast<const char *>(b) ;
    }

    int n = ::write (fd, buf, size) ;
    if (n >= 0) {
        if (deletebuf) {
            delete [] buf ;
        }
        return 0 ;
    }
    if (deletebuf) {
        delete [] buf ;
    }
    throw newException (vm, stack, "Write failed") ;
}


AIKIDO_NATIVE (raw_select) {
    if (paras[1].type != T_VECTOR) {
        throw newParameterException (vm, stack, "raw_select", "Illegal fdset type") ;
    }
    if (paras[2].type != T_INTEGER) {
        throw newParameterException (vm, stack, "raw_select", "Illegal timeout type") ;
    }
    Value::vector *fdvec = paras[1].vec ;
    int timeout = getInt (paras[2]) ;
    fd_set fdset ;
    FD_ZERO (&fdset) ;

    int max = 0 ;
    for (int i = 0 ; i < fdvec->size() ; i++) {
        if ((*fdvec)[i].type != T_INTEGER) {
            throw newException (vm, stack, "Only integer values allowed in raw_select fdset vector") ;
        }
        if ((*fdvec)[i].integer > max) {
            max = (*fdvec)[i].integer ;
        }
        FD_SET ((*fdvec)[i].integer, &fdset) ;
    }

    struct timeval to ;
    to.tv_sec = timeout / 1000000 ;
    to.tv_usec = timeout % 1000000 ;

    // -1 timeout means none
    struct timeval *top = timeout == -1 ? NULL : &to ;

    int e = select (max+1, &fdset, NULL, NULL, top) ;
    if (e < 0) {
        if (errno == EINTR) {
            return new Value::vector() ;        // empty for interrupt
        }
        throw newException (vm, stack, "Select failed") ;
    }
    Value::vector *result = new Value::vector() ;
    for (int i = 0 ; i < fdvec->size() ; i++) {
        if (FD_ISSET ((*fdvec)[i].integer, &fdset)) {
            result->push_back (Value ((*fdvec)[i].integer)) ;
        }
    }
    return result ;
}

AIKIDO_NATIVE (raw_seek) {
    if (!isIntegral (paras[1].type)) {
        throw newParameterException (vm, stack, "raw_seek", "Illegal fd type") ;
    }
    if (!isIntegral (paras[2].type)) {
        throw newParameterException (vm, stack, "raw_seek", "Illegal offset type") ;
    }
    if (!isIntegral (paras[3].type)) {
        throw newParameterException (vm, stack, "raw_seek", "Illegal whence type") ;
    }
    int e = lseek (getInt (paras[1]), getInt(paras[2]), getInt(paras[3])) ;
    if (e == -1) {
        throw newException (vm, stack, "seek failed") ;
    }
    return 0 ;
}

AIKIDO_NATIVE(unsetenv) {
    return 0;
}

AIKIDO_NATIVE(getUserName) {
	char buffer[256];
	DWORD size;
	if (GetUserName (buffer, &size)) {
		return 0;
	}
	throw newException(vm,stack, "Failed to get user name");
}

AIKIDO_NATIVE(getGroupName) {
    return "";
}

AIKIDO_NATIVE(getProcessMemorySize) {
    HANDLE hProcess;
    PROCESS_MEMORY_COUNTERS pmc;

    hProcess = OpenProcess(  PROCESS_QUERY_INFORMATION |
                                    PROCESS_VM_READ,
                                    FALSE, GetCurrentProcessId() );
    if (NULL == hProcess)
        return 0;

    double m = 0;
    if ( GetProcessMemoryInfo( hProcess, &pmc, sizeof(pmc)) )
    {
        int bytes = pmc.WorkingSetSize;     // bytes
        m = (double)bytes / (1024.0*1024.0);

    }

    CloseHandle( hProcess );
    return m;
}



//
// terminal functions
//
AIKIDO_NATIVE ( terminal_create) {
    return (reinterpret_cast<INTEGER>(new Terminal())) ;
}

AIKIDO_NATIVE ( terminal_open) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->open() ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_close) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->close() ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_put_string) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->put(paras[1].str->str) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_put_char) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->put((char)paras[1].integer) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_get ) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    char ch ;
    bool ok = term->get (ch, (int)paras[2].integer) ;
    *paras[1].addr = (char)ch ;
    return ok ;
}

AIKIDO_NATIVE ( terminal_erase_line) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->erase_line() ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_insert_char) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->insert_char((char)paras[1].integer) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_delete_left) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->delete_left() ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_amove ) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->amove(paras[1].integer, paras[2].integer) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_move ) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->move(paras[1].integer, paras[2].integer) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_clear_screen) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->clear_screen() ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_get_screen_size ) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    int rows, cols ;
    term->get_screen_size (rows, cols) ;
    *paras[1].addr = rows ;
    *paras[2].addr = cols ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_set_screen_size ) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    int rows = paras[2].integer ;
    int cols = paras[3].integer ;
    term->set_screen_size (rows, cols) ;
    return 0 ;
}

AIKIDO_NATIVE ( terminal_scroll) {
    Terminal *term = reinterpret_cast<Terminal*>(paras[0].object->varstack[1].integer) ;
    term->scroll(paras[1].integer, paras[2].integer, paras[3].integer, paras[4].integer) ;
    return 0 ;
}


}		// end of extern "C"

// these functions are in the main package

// sleep for microseconds

AIKIDO_NATIVE(threadSleep) {
    if (!isIntegral (paras[0])) {
        throw newParameterException (vm, stack, "sleep", "Illegal sleep value") ;
    }
    Sleep (getInt (paras[0]) / 1000) ;
    return 0 ;
}


}

