#ifndef terminal_h_included
#define terminal_h_included

#include <string>
#include <windows.h>

namespace aikido {

class Terminal {
public:
    Terminal() ;
    ~Terminal() ;

    /**
     * Open the terminal and put it in raw mode
     */
    void open() ;

    /**
     * Close the terminal, putting it back in the original state
     */
    void close() ;

    /**
     * Put a string to the terminal with formatting controls
     * @param   s       string put including printf formatting codes
     */
    void put (const char *s, ...) ;

    /**
     * Put a string to the terminal 
     * @param   s       string put 
     */
    void put (std::string s) ;

    /**
     * Put a single character at the cursor position
     * @param   ch      character to put
     */
    void put (char ch) ;

    /**
     * Get a single character from the terminal
     * @param   ch      where to put the character
     * @return          true if a character present, false otherwise
     */
    bool get(char &ch, int timeoutms = 1) ;

    /**
     * Erase the line
     */
    void erase_line() ;

    /**
     * Insert a character at the cursor position
     * @param   ch      character to insert
     */
    void insert_char (char ch) ;

    /**
     * delete the character to the left of the cursor
     */
    void delete_left() ;

    /**
     * Move the cursor to the given (x,y) coordinates
     * @param   x       the column to move to
     * @param   y       the row to move to
     */
    void amove (int x, int y) ;

    /**
     * Move the cursor relative to the current position
     * @param   dx      change in col
     * @param   dy      change in row
     */
    void move (int dx, int dy) ;

    /**
     * Clear the screen
     */
    void clear_screen() ;

    /**
     * Get the current screen size in rows and columns
     * @param   rows    receives the number of rows
     * @param   cols    receives the number of columns
     */
    void get_screen_size (int &rows, int &cols) ;
    void set_screen_size (int rows, int cols) ;

/*!
    @method     scroll
    @abstract   scroll a region of the screen
    @param      top     top line
    @param      bottom  bottom line
    @param      direction       negative number for up, positive for down
    @param      nlines          number of lines
    @discussion (comprehensive description)
*/

    void scroll (int top, int bottom, int direction, int nlines=1) ;
    
    void set_state (struct termios &state, bool now=false) ;
    void get_state (struct termios &state) ;
    
    
private:
    HANDLE stdoutconsole;
	HANDLE stdinconsole;
	HANDLE consolebuffer;
    CONSOLE_SCREEN_BUFFER_INFO info;
	WORD defaultattrs;		// default attributes
    bool is_open;
    CHAR_INFO *mainbuffer;
	DWORD currmode;

    void injectKey (int vkey, char key);
    void set_scroll_region (int top, int bottom) ;
    void scroll_up (int top, int bottom, int nlines) ;
    void scroll_down (int top, int bottom, int nlines) ;

} ;

}

#endif

