#ifndef terminal_h_included
#define terminal_h_included

#include <termios.h>
#include <string>

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
    struct termios cooked ;             /// cooked mode terminal state
    struct termios raw ;                /// raw mode terminal state

    const char *get_ctrl (const char *code) ;
    void put_ctrl (const char *code, int count) ;

    void set_input_timeout (int millisecs, int min) ;
    int get_char() ;

    char PC ;             // pad char
    const char *KS ;            // keypad transmit start
    const char *KE ;            // keypad transmit end
    const char *VS ;            // visual start
    const char *VE ;            // visual end
    const char *TI ;            // motion start
    const char *TE ;            // motion end
    const char *CE ;            // erase line
    const char *CL ;            // erase display
    const char *AL ;            // insert line
    const char *DL ;            // delete line
    const char *IC ;            // insert char
    const char *IM ;            // insert mode
    const char *EI ;            // end insert mode
    const char *HO ;            // home
    const char *CS ;            // change scroll region
    const char *sf ;            // scroll forward 1 line
    const char *sr ;            // scroll backward 1 line
    const char *SF ;            // scroll forward n lines
    const char *SR ;            // scroll backward n lines
    const char *SO ;            // standout mode on
    const char *SE ;            // standout mode off
    const char *MD ;            // double bright mode start
    const char *ME ;            // double bright mode end
    const char *RS ;            // reset
    const char *VB ;            // visual bell

    // cursor motion
    const char *CM ;            // cursor motion (absolute)
    const char *LE ;            // left N chars
    const char *DO ;            // down N line
    const char *UP ;            // up N lines
    const char *RI ;            // right N chars

    const char *DC ;            // delete one character
    char BC ;             // backspace
    char ND ;             // backspace

    char strings[1024] ;        // space for terminal strings
    char termbuf[1024] ;        // space for termcap entry
    char *sp ;                  // string pointer (for tgetstr)
    bool is_open ;              // is the terminal open
    char keybuf[100] ;          // buffer for characters read
    int nchars ;                // number of chars in buffer
    char *bufptr ;              // next character in buffer
    
    int sr_top ;                // top of current scroll region
    int sr_bottom ;             // bottom of current scroll region
    int lines ;                 // number of lines in the window
    
    void set_scroll_region (int top, int bottom) ;
    void scroll_up (int top, int bottom, int nlines) ;
    void scroll_down (int top, int bottom, int nlines) ;

} ;

}

#endif

