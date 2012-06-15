#include "terminal.h"

#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>

namespace aikido {

Terminal::Terminal() : sp(strings), is_open (false), nchars(0), bufptr(keybuf) {
    const char *tmp ;
    tmp = get_ctrl ("pc") ;
    if (tmp != NULL) {
        PC = *tmp ;
    }


    tmp = get_ctrl ("bc") ;
    if (tmp != NULL && tmp[1] == '\0') {
        BC = *tmp ;
    } else {
        BC = '\b' ;
    }

    tmp = get_ctrl ("nd") ;
    if (tmp != NULL && tmp[1] == '\0') {
        ND = *tmp ;
    } else {
        delete [] tmp ;
    }
    
    KS = get_ctrl("ks") ;
    KE = get_ctrl("ke") ;
    VS = get_ctrl("vs") ;
    VE = get_ctrl("ve") ;
    TI = get_ctrl("ti") ;
    TE = get_ctrl("te") ;
    CE = get_ctrl("ce") ;
    CL = get_ctrl("cl") ;
    AL = get_ctrl("al") ;
    DL = get_ctrl("dl") ;
    IC = get_ctrl("ic") ;
    IM = get_ctrl("im") ;
    EI = get_ctrl("ei") ;
    CM = get_ctrl("cm") ;
    HO = get_ctrl("ho") ;
    CS = get_ctrl("cs") ;
    sf = get_ctrl("sf") ;
    sr = get_ctrl("sr") ;
    SF = get_ctrl("SF") ;
    SR = get_ctrl("SR") ;
    SO = get_ctrl("so") ;
    MD = get_ctrl("md") ;
    ME = get_ctrl("me") ;
    SE = get_ctrl("se") ;
    RS = get_ctrl("rs") ;
    VB = get_ctrl("vb") ;
    DC = get_ctrl("dc") ;

    // relative cursor motion
    LE = get_ctrl("le") ;
    RI = get_ctrl("nd") ;
    DO = get_ctrl("do") ;
    UP = get_ctrl("up") ;

    get_state (cooked) ;                // get current state as cooked state
    raw = cooked ;                      // save as raw
    
    // now modify the raw state
#if defined (_OS_MACOSX)
    raw.c_oflag &= ~(OCRNL | ONLRET);
    raw.c_iflag &= ~(ICRNL | IGNCR | INLCR);
    raw.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOCTL | ECHOPRT | ECHOKE);
#else
    raw.c_oflag &= ~(OLCUC | OCRNL | ONLRET | XTABS);
    raw.c_iflag &= ~(ICRNL | IGNCR | INLCR);
    raw.c_lflag &= ~(ICANON | XCASE | ECHO | ECHOE | ECHOK | ECHONL | ECHOCTL | ECHOPRT | ECHOKE);
#endif

    
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    
}

Terminal::~Terminal() {
    close() ;
    delete [] KS ;            // keypad transmit start
    delete [] KE ;            // keypad transmit end
    delete [] VS ;            // visual start
    delete [] VE ;            // visual end
    delete [] TI ;            // motion start
    delete [] TE ;            // motion end
    delete [] CE ;            // erase line
    delete [] CL ;            // erase display
    delete [] AL ;            // insert line
    delete [] DL ;            // delete line
    delete [] IC ;            // insert char
    delete [] IM ;            // insert mode
    delete [] EI ;            // end insert mode
    delete [] HO ;            // home
    delete [] CS ;            // change scroll region
    delete [] sf ;            // scroll forward 1 line
    delete [] sr ;            // scroll backward 1 line
    delete [] SF ;            // scroll forward n lines
    delete [] SR ;            // scroll backward n lines
    delete [] SO ;            // standout mode on
    delete [] SE ;            // standout mode off
    delete [] MD ;            // double bright mode start
    delete [] ME ;            // double bright mode end
    delete [] RS ;            // reset
    delete [] VB ;            // visual bell
    delete [] CM ;            // cursor motion (absolute)
    delete [] LE ;            // left N chars 
    delete [] DO ;            // down N line
    delete [] UP ;            // up N lines
    delete [] RI ;            // right N chars

    delete [] DC ;            // delete one character

}

void Terminal::open() {
    if (is_open) {
        return ;
    }
   

    // set the terminal into raw mode now
    set_state (raw) ;
    is_open = true ;
    
    // no scroll region
    sr_top = 0 ;
    sr_bottom = 0 ;
    
}


void Terminal::close() {
    if (is_open) {
        set_state (cooked) ;           // set cooked state
        is_open = false ;
    }
}

void Terminal::put (const char *s, ...) {
    va_list ap ;
    va_start (ap, s) ;
    vprintf (s, ap) ;
    fflush (stdout) ;
    va_end (ap) ;
}

void Terminal::put (std::string s) {
    fprintf (stdout, "%s", s.c_str()) ;
    fflush (stdout) ;
}

void Terminal::put (char ch) {
    putchar (ch) ;
    fflush (stdout) ;
}

// called by tputs
static int outchar (int ch) {
    putchar (ch) ;
    fflush (stdout) ;
    return 0 ;
}


// the following escape sequences are from the /etc/termcap entry for xterm-r6.  They
// should work on  any ANSI terminal

struct EscapeCode {
    const char *code ;
    const char *seq ;
} ;
EscapeCode escapes[] = {
{"nd", "\\E[C"},
{"ks", "\\E[?1h\\E="},
{"ke", "\\E[?1l\\E>"},
{"ti", "\\E7\\E[?47h"},
{"te", "\\E[2J\\E[?47l\\E8"},
{"ce", "\\E[K"},
{"cl", "\\E[H\\E[2J"},
{"al", "\\E[L"},
{"dl", "\\E[M"},
{"im", "\\E[4h"},
{"ei", "\\E[4l"},
{"cm", "\\E[%i%d;%dH"},
{"ho", "\\E[H"},
{"cs", "\\E[%i%d;%dr"},
{"sf", "\012"},
{"sr", "\\EM"},
{"so", "\\E[7m"},
{"md", "\\E[1m"},
{"me", "\\E[m"},
{"se", "\\E[m"},
{"rs", "\\E7\\E[r\\E[m\\E[?7h\\E[?1;3;4;6l\\E[4l\\E8\\E>"},
{"dc", "\\E[P"},
{"le", "\010"},
{"nd", "\\E[C"},
{"do", "\012"},
{"up", "\\E[A"},
{NULL, NULL}
} ;

const char *Terminal::get_ctrl (const char *code) {
    for (int i = 0 ; escapes[i].code != NULL ; i++) {
        if (strcmp (escapes[i].code, code) == 0) {
            const char *ch = escapes[i].seq ;
            char *buf = new char [strlen(ch) + 1] ;
            char *p = buf ;
            while (*ch != 0) {
                if (*ch == '\\' && ch[1] == 'E') {      // \E is escape
                    *p = '\033' ;
                    ch++ ;
                } else {
                    *p = *ch ;
                }
                ch++ ;
                p++ ;
            }
            *p = 0 ;
            return const_cast<const char *>(buf) ;
        }
    }
    return NULL ;
}

// put a control code
void Terminal::put_ctrl (const char *code, int count) {
    if (code != NULL) {
        char buf[256] ;
        snprintf (buf, sizeof(buf), code, count) ;
        char *ch = buf ;
        while (*ch != 0) {
            outchar (*ch++) ;
        }
    }
}

// the following tgoto code is copyright as follows

/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

static const char *tgoto(const char *CM, int destcol, int destline) {
        static char result[256];
        static char added[10];
        char *cp = const_cast<char *>(CM);
        char *dp = result;
        int c;
        int oncol = 0;
        int which = destline;

        if (cp == 0) {
toohard:
                /*
                 * ``We don't do that under BOZO's big top''
                 */
                return ("OOPS");
        }
        added[0] = 0;
        while ((c = *cp++) != 0) {
                if (c != '%') {
                        *dp++ = c;
                        continue;
                }
                switch (c = *cp++) {

                case 'n':
                        destcol ^= 0140;
                        destline ^= 0140;
                        goto setwhich;

                case 'd':
                        if (which < 10)
                                goto one;
                        if (which < 100)
                                goto two;
                        /* fall into... */

                case '3':
                        *dp++ = (which / 100) | '0';
                        which %= 100;
                        /* fall into... */

                case '2':
two:    
                        *dp++ = which / 10 | '0';
one:
                        *dp++ = which % 10 | '0';
swap:
                        oncol = 1 - oncol;
setwhich:
                        which = oncol ? destcol : destline;
                        continue;

                case '>':
                        if (which > *cp++)
                                which += *cp++;
                        else
                                cp++;
                        continue;

                case '+':
                        which += *cp++;
                        /* fall into... */

#if 0
                case '.':
/* casedot: */
                        /*
                         * This code is worth scratching your head at for a
                         * while.  The idea is that various weird things can
                         * happen to nulls, EOT's, tabs, and newlines by the
                         * tty driver, arpanet, and so on, so we don't send
                         * them if we can help it.
                         *
                         * Tab is taken out to get Ann Arbors to work, otherwise
                         * when they go to column 9 we increment which is wrong
                         * because bcd isn't continuous.  We should take out
                         * the rest too, or run the thing through more than
                         * once until it doesn't make any of these, but that
                         * would make termlib (and hence pdp-11 ex) bigger,
                         * and also somewhat slower.  This requires all
                         * programs which use termlib to stty tabs so they
                         * don't get expanded.  They should do this anyway
                         * because some terminals use ^I for other things,
                         * like nondestructive space.
                         */
                        if (which == 0 || which == CTRL('d') || /* which == '\t' || */ which == '\n') {
                                if (oncol || UP) /* Assumption: backspace works */
                                        /*
                                         * Loop needed because newline happens
                                         * to be the successor of tab.
                                         */
                                        do {
                                                strcat(added, oncol ? (BC ? BC : "\b") : UP);
                                                which++;
                                        } while (which == '\n');
                        }
#endif
                        *dp++ = which;
                        goto swap;

                case 'r':
                        oncol = 1;
                        goto setwhich;

                case 'i':
                        destcol++;
                        destline++;
                        which++;
                        continue;

                case '%':
                        *dp++ = c;
                        continue;

                case 'B':
                        which = (which/10 << 4) + which%10;
                        continue;

                case 'D':
                        which = which - 2 * (which%16);
                        continue;

                default:
                        goto toohard;
                }
        }
        strcpy(dp, added);
        return const_cast<const char *>(result);
}

void Terminal::delete_left() {
    put (BC) ;
    put_ctrl (DC, 1) ;
}

// clear the screen
void Terminal::clear_screen() {
    put_ctrl (CL, 1) ;
    fflush (stdout) ;
}

// erase the whole line
void Terminal::erase_line() {
    put_ctrl (CE, 1) ;
}

// insert a character at the current position
void Terminal::insert_char (char ch) {
    put_ctrl (IM, 1) ;
    put_ctrl (IC, 1) ;
    outchar (ch) ;
    put_ctrl (EI, 1) ;
}

// move the cursor to (x,y)
void Terminal::amove (int x, int y) {
    put_ctrl (tgoto (CM, x, y), 1) ;
}

// move relative
void Terminal::move (int dx, int dy) {
    if (dy < 0) {
        while (dy++ < 0) {
            put_ctrl (UP, 1) ;
        }
    } else while (dy-- > 0) {
        put_ctrl (DO, 1) ;
    }
    fflush (stdout) ;
    if (dx < 0) {               // move left?
        while (dx++ < 0) {
            put_ctrl (LE, 1) ;
        }
    } else while (dx-- > 0) {
        put_ctrl (RI, 1) ;
    }
}

void Terminal::set_state (struct termios &state, bool now) {
    tcsetattr (0, now ? TCSANOW : TCSADRAIN, &state) ;
}

void Terminal::get_state (struct termios &state) {
    tcgetattr (0, &state) ;
}

int Terminal::get_char() {
    if (nchars == 0) {
        nchars = read (0, keybuf, sizeof (keybuf)) ;
        if (nchars <= 0) {
            return EOF ;
        }
        bufptr = keybuf ;               // reset the char buffer pointer
    }
    nchars-- ;
    return *bufptr++ ;
}

void Terminal::set_input_timeout (int millisecs, int min) {
    // the time for the terminal timeout is in deciseconds
    int time = (millisecs + 99) / 100 ;
    raw.c_cc[VMIN] = min ;
    raw.c_cc[VTIME] = time;
    set_state(raw, true);
}

// get a character with a timeout
bool Terminal::get (char &ch, int timeout) {
    if (nchars > 0) {
        ch = (char)get_char() ;
        return true ;
    }
    set_input_timeout (timeout, 0) ;
    int c = get_char() ;
    set_input_timeout (0, 1) ;
    ch = (char)c ;
    return c != EOF ;
}

// get the screen size
void Terminal::get_screen_size (int &rows, int &cols) {
    if (!isatty(0)) {
        rows = 24 ;
        cols = 80 ;
        return ;
    }
    struct winsize winsz ;
    winsz.ws_row = 24 ;
    winsz.ws_col = 80 ;
    int err = ioctl (0, TIOCGWINSZ, (char *) &winsz);
    if (err < 0) {
        rows = 24 ;
        cols = 80 ;
    } else {
        rows = winsz.ws_row;
        cols = winsz.ws_col;
    }
    
    // defensive: check for malformed result from the ioctl (can happen with pseudo ttys)
    if (cols <= 0) {
        cols = 80 ;
    }
    if (rows <= 0) {
        rows = 24 ;
    }
    lines = rows ;

}

void Terminal::set_screen_size (int rows, int cols) {
    if (!isatty(0)) {
        return ;
    }
    struct winsize winsz ;
    winsz.ws_row = rows ;
    winsz.ws_col = cols ;
    ioctl (0, TIOCSWINSZ, (char *) &winsz);
    lines = rows ;
}

void Terminal::set_scroll_region (int top, int bottom) {
    if (top != sr_top || bottom != sr_bottom) {
        put_ctrl (tgoto (CS, bottom, top), bottom - top) ;
        sr_top = top ;
        sr_bottom = bottom ;
    }
}


void Terminal::scroll_up (int top, int bottom, int nlines) {
    set_scroll_region (top, bottom) ;
    put_ctrl (tgoto (CM, 0, bottom), 1) ;
    for (int i = 0 ; i < nlines ; i++) {
        put_ctrl (sf, bottom-top) ;
    }
    set_scroll_region (0, lines) ;
}

void Terminal::scroll_down (int top, int bottom, int nlines) {
    set_scroll_region (top, bottom) ;
    put_ctrl (tgoto (CM, 0, top), 1) ;      // move to top left of scroll region
    for (int i = 0 ; i < nlines ; i++) {
        put_ctrl (sr, bottom-top) ;
    }
    set_scroll_region (0, lines) ;
}

void Terminal::scroll (int top, int bottom, int direction, int nlines) {
    if (direction < 0) {
        scroll_up (top, bottom, nlines) ;
    } else {
        scroll_down (top, bottom, nlines) ;
    }
}

}

