#include "winconsole.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>


namespace aikido {

Terminal::Terminal():is_open(false) {
}

Terminal::~Terminal() {
    close() ;

}

void Terminal::open() {
    if (is_open) {
        return ;
    }
   

    is_open = true ;
    
    stdoutconsole = GetStdHandle (STD_OUTPUT_HANDLE);
	stdinconsole = GetStdHandle (STD_INPUT_HANDLE);
	GetConsoleScreenBufferInfo (stdoutconsole, &info);
	defaultattrs = info.wAttributes;

	// allocate space for the current screen
	int rows = info.srWindow.Bottom - info.srWindow.Top;
	int cols = info.srWindow.Right - info.srWindow.Left;

	mainbuffer = (CHAR_INFO*)malloc (sizeof(CHAR_INFO) * rows * cols);
	COORD coord;
	COORD zero = {0,0};

	coord.X = cols;
	coord.Y = rows;

	SMALL_RECT rect;

	rect.Top = 0;
	rect.Bottom = rows-1;
	rect.Left = 0;
	rect.Right = cols-1;

	// read the current screen
	ReadConsoleOutput (stdoutconsole,mainbuffer,coord,zero,&rect);

	consolebuffer = CreateConsoleScreenBuffer (GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,CONSOLE_TEXTMODE_BUFFER,NULL);
	SetConsoleActiveScreenBuffer(consolebuffer);
	


	// copy to the current screen
	WriteConsoleOutput(consolebuffer,mainbuffer,coord,zero,&rect);

	GetConsoleMode (stdinconsole, &currmode);
	SetConsoleMode (stdinconsole, currmode & ~(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT));
}


void Terminal::close() {
    if (is_open) {
		free (mainbuffer);
		SetConsoleActiveScreenBuffer (stdoutconsole);
		SetConsoleMode (stdinconsole, currmode);
        is_open = false ;
    }
}

// shift all the characters after the cursor on the current line 
static void shift_line (HANDLE console, int delta) {
	SMALL_RECT rect, writerect;
	CONSOLE_SCREEN_BUFFER_INFO info;
	CHAR_INFO buffer[1024];
	COORD bufsize;
	COORD bufcoord;

	GetConsoleScreenBufferInfo (console, &info);

	rect.Top = info.dwCursorPosition.Y;
	rect.Left = info.dwCursorPosition.X;
	rect.Bottom = info.dwCursorPosition.Y + 1;
	rect.Right = info.dwSize.X-1;

	writerect.Top = rect.Top;
	writerect.Left = rect.Left + delta;
	writerect.Right = rect.Right + delta;
	writerect.Bottom = rect.Bottom;

	bufsize.Y = 1;
	bufsize.X = info.dwSize.X;
	bufcoord.X = 0;
	bufcoord.Y = 0;

	ReadConsoleOutput (console, buffer, bufsize, bufcoord, &rect);

	WriteConsoleOutput (console, buffer, bufsize, bufcoord, &writerect);

}

#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7

#define COLOR_NORMAL (1<<8)
#define COLOR_BOLD (2<<8)

static void set_color (HANDLE console, int color) {
	bool bold = color & COLOR_BOLD;
	color &= 0xff;

	WORD attr = 0;
	switch (color) {
	case COLOR_BLACK:
		attr = 0;
		break;
	case COLOR_RED:
		attr = FOREGROUND_RED;
		break;
	case COLOR_GREEN:
		attr = FOREGROUND_GREEN;
		break;
	case COLOR_YELLOW:
		attr = FOREGROUND_RED|FOREGROUND_GREEN;
		break;
	case COLOR_BLUE:
		attr = FOREGROUND_BLUE;
		break;
	case COLOR_MAGENTA:
		attr = FOREGROUND_RED|FOREGROUND_BLUE;
		break;
	case COLOR_CYAN:
		attr = FOREGROUND_GREEN|FOREGROUND_BLUE;
		break;
	case COLOR_WHITE:
		attr = FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_GREEN;
		break;
	}
	if (bold) {
		attr |= FOREGROUND_INTENSITY;
	}
	SetConsoleTextAttribute (console, attr);
}

// s points to and ANSI escape code (the char after the escape).  Parse and handle it.  This is only color for now
static const char *parse_ansi_code (HANDLE console, WORD def, const char *s) {
	if (s[0] == '[') {
		s++;
		char buf[16];
		char *p = buf;
		while (*s != 0 && isdigit(*s)) {
			*p++ = *s++;
		}
		*p = 0;
		if (*s == ';') {
			s++;
			while (*s != 0 && isdigit (*s)) {
				s++;
			}
		}
		if (*s == 'm') {
			s++;
			int color = atoi (buf);
			if (color >= 30 && color < 38) {
				color -= 30;
				set_color (console, color);
			} else if (color == 39) {
				// reset
				SetConsoleTextAttribute (console, def);
			}
		}
	}
	return s;
}

static void write_console (HANDLE console, WORD def, const char *s) {
	const char *p = s;
	while (*p != 0) {
		if (*p == '\033') {
			if (s != p) {
				// flush current string to console
				DWORD n = p - s;
				WriteConsoleA (console, s, n, &n, NULL);
			}
			p++;
			p = parse_ansi_code (console, def, p);
			s = p;			// start again
		} else {
			p++;
		}
	}
	// flush final string to console
	if (s != p) {
		DWORD n = p - s;
		WriteConsoleA (console, s, n, &n, NULL);
	}
}


void Terminal::put (const char *s, ...) {
	char buffer[4096];
	//DWORD n;
    va_list ap ;
    va_start (ap, s) ;
    vsprintf (buffer, s, ap) ;
	//n = strlen (buffer);
	//WriteConsoleA (consolebuffer,buffer,n, &n, NULL);
	write_console (consolebuffer, defaultattrs, buffer);
    va_end (ap) ;
}

void Terminal::put (std::string s) {
	write_console (consolebuffer, defaultattrs, s.c_str());
	//DWORD n = s.size();
	//WriteConsoleA (consolebuffer, s.c_str(), n, &n, NULL);
}

void Terminal::put (char ch) {
	char buffer[2];
	DWORD n;
    buffer[0] = ch;
    buffer[1] = 0;
	n = 1;
	WriteConsoleA (consolebuffer,buffer,n, &n, NULL);

}


void Terminal::injectKey (int vkey, char key) {
    INPUT_RECORD irec;
    irec.EventType = KEY_EVENT;
    irec.Event.KeyEvent.bKeyDown = 1;
    irec.Event.KeyEvent.wRepeatCount = 1;
    irec.Event.KeyEvent.wVirtualKeyCode =  vkey;
    irec.Event.KeyEvent.wVirtualScanCode = 0;
    irec.Event.KeyEvent.uChar.AsciiChar = key;
    irec.Event.KeyEvent.dwControlKeyState = 0;

    DWORD recs = 0;
    WriteConsoleInput (consolebuffer, &irec, 1, &recs);
}

void Terminal::delete_left() {
	shift_line (consolebuffer, -1);
}

// clear the screen
void Terminal::clear_screen() {
	GetConsoleScreenBufferInfo (consolebuffer, &info);

   DWORD consize = info.dwSize.X * info.dwSize.Y;
   DWORD chars;

   COORD zerocoord = {0,0};

   FillConsoleOutputCharacter (consolebuffer,(TCHAR)' ', consize, zerocoord, &chars);

}

// erase the whole line
void Terminal::erase_line() {
   GetConsoleScreenBufferInfo (consolebuffer, &info);

   DWORD consize = info.dwSize.X - info.dwCursorPosition.X;
   DWORD chars;

   COORD coord = {info.dwCursorPosition.X,info.dwCursorPosition.Y};

   FillConsoleOutputCharacter (consolebuffer,(TCHAR)' ', consize, coord, &chars);
}

// insert a character at the current position
void Terminal::insert_char (char ch) {
	shift_line (consolebuffer, 1);
    put (ch) ;
}

// move the cursor to (x,y)
void Terminal::amove (int x, int y) {
    COORD c;
    c.X = x;
    c.Y = y;
    SetConsoleCursorPosition (consolebuffer, c);
}

// move relative
void Terminal::move (int dx, int dy) {
    GetConsoleScreenBufferInfo (consolebuffer, &info);
    info.dwCursorPosition.X += dx;
    info.dwCursorPosition.Y += dy;
    
    SetConsoleCursorPosition (consolebuffer, info.dwCursorPosition);
}

void Terminal::set_state (struct termios &state, bool now) {
}

void Terminal::get_state (struct termios &state) {
}


static BOOL handle_key_event (KEY_EVENT_RECORD &r, char charbuffer[], int &charcount) {
	if (!r.bKeyDown) {
		charcount = 0;
		return false;
	}
	if (r.dwControlKeyState & ENHANCED_KEY) {
		// arrow key, etc.
		bool shift = r.dwControlKeyState & SHIFT_PRESSED;
		bool ctrl = r.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);

		switch (r.wVirtualKeyCode) {
		case VK_LEFT:
			charbuffer[0] = '\033';
			charbuffer[1] = '[';
			if (shift) {
				charbuffer[2] = '2';
				charbuffer[3] = 'D';
				charcount = 4;
			} else if (ctrl) {
				charbuffer[2] = '1';
				charbuffer[3] = ';';
				charbuffer[4] = '5';
				charbuffer[5] = 'D';
				charcount = 6;
			} else {
				charbuffer[2] = 'D';
				charcount = 3;
			}
			return true;
		case VK_RIGHT:
			charbuffer[0] = '\033';
			charbuffer[1] = '[';
			if (shift) {
				charbuffer[2] = '2';
				charbuffer[3] = 'C';
				charcount = 4;
			} else if (ctrl) {
				charbuffer[2] = '1';
				charbuffer[3] = ';';
				charbuffer[4] = '5';
				charbuffer[5] = 'C';
				charcount = 6;
			} else {
				charbuffer[2] = 'C';
				charcount = 3;
			}
			charcount = 3;
			return true;
		case VK_UP:
			charbuffer[0] = '\033';
			charbuffer[1] = '[';
			if (shift) {
				charbuffer[2] = '2';
				charbuffer[3] = 'A';
				charcount = 4;
			} else if (ctrl) {
				charbuffer[2] = '1';
				charbuffer[3] = ';';
				charbuffer[4] = '5';
				charbuffer[5] = 'A';
				charcount = 6;
			} else {
				charbuffer[2] = 'A';
				charcount = 3;
			}
			charcount = 3;
			return true;
		case VK_DOWN:
			charbuffer[0] = '\033';
			charbuffer[1] = '[';
			if (shift) {
				charbuffer[2] = '2';
				charbuffer[3] = 'B';
				charcount = 4;
			} else if (ctrl) {
				charbuffer[2] = '1';
				charbuffer[3] = ';';
				charbuffer[4] = '5';
				charbuffer[5] = 'B';
				charcount = 6;
			} else {
				charbuffer[2] = 'B';
				charcount = 3;
			}
			charcount = 3;
			return true;
		case VK_HOME:
			charbuffer[0] = '\033';
			charbuffer[1] = '[';
			charbuffer[2] = 'H';
			charcount = 3;
			return true;
		case VK_END:
			charbuffer[0] = '\033';
			charbuffer[1] = '[';
			charbuffer[2] = 'F';
			charcount = 3;
			return true;
		case VK_PRIOR:		// page up
			charbuffer[0] = '\033';
			charbuffer[1] = '[';
			charbuffer[2] = '5';
			charbuffer[3] = '~';
			charcount = 4;
			return true;
		case VK_NEXT:		// page down
			charbuffer[0] = '\033';
			charbuffer[1] = '[';
			charbuffer[2] = '6';
			charbuffer[3] = '~';
			charcount = 4;
			return true;

		}
	}

	bool ctrl = r.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
	char ch = r.uChar.AsciiChar;

	if (ctrl) {
		if (ch >= 'A' && ch <= 'Z') {
			ch -= 'A';
		}
	}

	charbuffer[0] = ch;
	charcount = 1;
	return true;
}

// get a character with a timeout
bool Terminal::get (char &ch, int timeout) {
	static 	INPUT_RECORD buffer[16];
	static DWORD numread;			// number of input records in the buffer
	static DWORD numprocessed;		// number of input records we've processed out of the buffer
	static char charbuffer[16];		// characters to return to caller
	static int charcount;			// number of characters we've returned from the buffer
	static int charbuffersize;		// number of characters in the buffer

	// any characters in the buffer?
	if (charbuffersize > 0 && charcount < charbuffersize) {
		ch = charbuffer[charcount++];
		return true;
	}

	if (numprocessed < numread) {
		if (buffer[numprocessed].EventType == KEY_EVENT) {

			charcount = 0;
			// no characters in the buffer, get one from the terminal
			if (handle_key_event (buffer[numprocessed].Event.KeyEvent, charbuffer, charbuffersize)) {
				ch = charbuffer[charcount++];
				numprocessed++;
				return true;
			}

		}
		// all other event types are ignored for now
		numprocessed++;
	}

	DWORD n = WaitForSingleObject (stdinconsole, timeout);
	if (n == WAIT_TIMEOUT) {
		return false;
	}
	BOOL b = ReadConsoleInput(stdinconsole,buffer,16,&numread);
	if (b == 0) {
		printf ("got error %d\n", GetLastError());
		return false;
	}
	numprocessed = 0;
	return false;		// next call will read from buffer
}

// get the screen size
void Terminal::get_screen_size (int &rows, int &cols) {
	HANDLE console;
	if (is_open) {
		console = consolebuffer;
	} else {
		console =  GetStdHandle (STD_OUTPUT_HANDLE);
	}
    GetConsoleScreenBufferInfo (console, &info);
    cols =  info.srWindow.Right - info.srWindow.Left+1;
    rows = info.srWindow.Bottom - info.srWindow.Top+1;
}

void Terminal::set_screen_size (int rows, int cols) {
}

void Terminal::set_scroll_region (int top, int bottom) {
}


void Terminal::scroll_up (int top, int bottom, int nlines) {
   GetConsoleScreenBufferInfo (consolebuffer, &info);
   SMALL_RECT scrollrect, cliprect;
   COORD dest;
   CHAR_INFO fill;

   scrollrect.Top = top;
   scrollrect.Bottom = bottom;
   scrollrect.Left = 0;
   scrollrect.Right = info.dwSize.X - 1;

   dest.X = 0;
   dest.Y = top - nlines;

   cliprect = scrollrect;

   fill.Attributes = 0;
   fill.Char.AsciiChar = (char)' ';
   ScrollConsoleScreenBuffer (consolebuffer,&scrollrect,&cliprect,dest,&fill);
}

void Terminal::scroll_down (int top, int bottom, int nlines) {
   GetConsoleScreenBufferInfo (consolebuffer, &info);
   SMALL_RECT scrollrect, cliprect;
   COORD dest;
   CHAR_INFO fill;

   scrollrect.Top = top;
   scrollrect.Bottom = bottom;
   scrollrect.Left = 0;
   scrollrect.Right = info.dwSize.X - 1;

   dest.X = 0;
   dest.Y = top + nlines;

   cliprect = scrollrect;

   fill.Attributes = 0;
   fill.Char.AsciiChar = (char)' ';
   ScrollConsoleScreenBuffer (consolebuffer,&scrollrect,&cliprect,dest,&fill);


}

void Terminal::scroll (int top, int bottom, int direction, int nlines) {
    if (direction < 0) {
        scroll_up (top, bottom, nlines) ;
    } else {
        scroll_down (top, bottom, nlines) ;
    }
}

}

