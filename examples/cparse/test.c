/*
 test program for the Aikido C parser example
 */


#include <stdio.h>
#include <stdlib.h>

void factorial (int n) {
    if (n <= 1) {
        return 1 ;
    }
    return n * factorial (n - 1) ;
}

void print_factorials (int n) {
    int i ;
    for (i = 0 ; i < n ; i++) {
        printf ("factorial %d = %d\n", i, factorial (i)) ;
    }
}

enum PointType {
   TYPE1, TYPE2 
} ;

struct Point {
   enum PointType t ;
   int x, y ;
} ;

typedef struct Point Point ;

Point *makePoint (int x, int y) {
    Point *p = (Point *)malloc (sizeof (Point)) ;
    p->x = x ;
    p->y = y ;
    p->t = TYPE2 ;
    return p ;
}


