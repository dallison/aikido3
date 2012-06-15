#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <fstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>

int main (int argc, char **argv) {
    char *tmpfile = tmpnam(NULL) ;
    char *infile = NULL ;
    char *outfile = "a.out" ;
    std::vector<char *> syms ;

    for (int i = 1 ; i < argc ; i++) {
        if (strcmp (argv[i], "-o") == 0) {
            if (i == argc-1) {
               std::cerr << "-o requires a filename\n" ;
               exit (2) ;
            } 
            i++ ;
            outfile = argv[i] ;
        } else if (argv[i][0] == '-' && argv[i][1] == 'U') {
            syms.push_back (&argv[i][2]) ;
        } else if (infile == NULL) {
            infile = argv[i] ;
        } else {
           std::cerr << "too many input files\n" ;
           exit (2) ;
        }
    }
    if (infile == NULL) {
       std::cerr << "usage: mkelf [-o outfile] infile\n" ;
       exit (2) ;
    }
    std::ofstream os (tmpfile) ;
    std::ifstream is (infile) ;

    os << ".section \".text\"\n" ;
    os << ".globl __aikido_obj_file\n" ;
    // reference undefined symbols
    for (int i = 0 ; i < syms.size() ; i++) {
        os << ".globl " << syms[i] << "\n" ;
    }
    os << "__aikido_obj_file:\n" ;
    struct stat st ;
    if (stat (infile, &st) == 0) {
        os << ".long " << st.st_size << "\n" ;
    } else {
        std::cerr << "Unable to find " << infile << "\n" ;
        exit (1) ;
    }
  
    char buf[80] ;
    while (!is.eof()) {
        int i = 0 ;
        while (i < 80) {
            char c = is.get() ;
            if (is.eof()) {
                break ;
            }
            buf[i] = c ;
            i++ ;
        }
        os << ".byte " ;
        bool comma = false ;
        for (int j = 0 ; j < i ; j++) {
           if (comma) {
              os << "," ;
           }
           os << (int)buf[j] ;
           comma = true ;
        }
        os << '\n' ;
    }
    os.close() ;
    is.close() ;
    sprintf (buf, "as -o %s %s", outfile, tmpfile) ;
    system (buf) ;
    remove (tmpfile) ;
}
