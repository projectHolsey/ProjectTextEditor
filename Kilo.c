/*** includes ***/

#include <ctype.h> // Control characters
#include <stdio.h> // standard IO module for printf
#include <errno.h> 
#include <sys/ioctl.h> // Get size of terminal window
#include <stdlib.h> // standard library - type conversion, mem alloc...
#include <termios.h> // importing variables for terminal
#include <unistd.h>  // importing standard io module for input keys

/*** defines ***/

// defining a constant / function
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/

// global struct to contain editor's state
struct editorConfig {
    int screencols;
    int screenrows;
    struct termios orig_termios; // Saving original termios state
};

struct editorConfig E;


/*** terminal ***/
void die(const char *s) {
    // clear screen on exit
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s); // prints string given to it before error occured
    exit(1);
}

void disableRawMode() {
    // If we can't disable raw mode, exit
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

/**
 * Function to modify struct variables / flags
 * all of these variables / objects come from termios.h
 */
void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
        // if we can' get the raw mode attributes, exit
        die("tcgetattr");
    } // Save struct to orig_termios var
    
    atexit(disableRawMode); // at exit, disable the raw mode
    

    struct termios raw = E.orig_termios;

    // Disabling ctrl-s & ctrl-q - input types  
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // disabling output processing
    raw.c_oflag &= ~(OPOST);

    // Bit mask with multiple bits ( | = bitwise OR )
    raw.c_cflag |= (CS8);

    // Read input byte-by-byte, not line-by-line
    // Turn off canonical mode
    // turn off Ctrl-c and ctrl-z signals
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 
    // ~ (BITWISE NOT) > from 0010 to 1101
    // & (BITWISE AND) > from 0010 to 0000
    /*
    BITWISE AND
    0  0  >  0
    0  1  >  0
    1  0  >  0
    1  1  >  1    
    */

   // c.cc = control characters - array of bytes with terminal settings
   raw.c_cc[VMIN] = 0; // num bytes before read can return
   raw.c_cc[VTIME] = 1; // max time to wait before read returns (100mS)


    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        // if we can't set the new attributes, exit
        die("tcsetattr");
    } // setting attrbiute
    // TCASFLUSH - specifies when to apply change, here we wait until output to be written to terminal
}

// wait for a key press and return it
char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }
    return c;
}

int getCursorPosition(int *rows, int *cols) {

    char buf[32];
    unsigned int i = 0;

    // 'n' is device status report command - finding cursor pos
    // https://vt100.net/docs/vt100-ug/chapter3.html#DSR
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    // read in to fill the buffer - breka on 'R' character
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }
        if (buf[i] == 'R') {
            break;
        }
        i++;
    }

    // make the final byte \0 
    buf[i] = '\0';

    // make sur eit responded with escape character
    if (buf[0] != '\x1b' || buf[1] != '[') {
        return -1;
    }
    // pass pointer to 3rd ele of buf[]
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }

    return 0;
}

// get the size of the terminal
int getWindowSize(int *rows, int *cols) {

    //ioctl(), TIOCGWINSZ, and struct winsize come from <sys/ioctl.h>.
    struct winsize ws;

    // on success, system call will place numbers of cols and rows in winsize struct
    if (1 || ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        
        // C -> move the cursor right 999 spaces
        // B -> move the cursor down 999 spaces
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12){
            return -1;
        }
        // return current current position
        return getCursorPosition(rows, cols);

    } else {
        // setting the references passed to new values
        // common way to return multiple values in C
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}


/*** output ***/
void editorDrawRows() {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        // Write ~ on the left hand side of the screen
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

// Clears the terminal
void editorRefreshScreen() {
   
    // 4 = write 4 bytes
    // \x1b[2j] -> ESCAPE SEQUENCE

    // \x1b -> escape character
    // [ -> always follows the escape character

    // J -> clear the screen
    // 2 -> clear the entire screen
    // <esc>[1J would clear screen up to where cursor is
    write(STDOUT_FILENO, "\x1b[2J", 4);

    // Reposition cursor to top left corner after refresh
    write(STDOUT_FILENO, "\x1b[H", 3);

    
    // draw the ~ symbol at beggining of row
    editorDrawRows();

      // Reposition cursor to top left corner after refresh
    write(STDOUT_FILENO, "\x1b[H", 3);

}

/*** input ***/
// wait for keypress, handles it later
void editorProcessKeypress() {
    char c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            // clear screen on quit
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** init ***/
void initEditor() {
    // Check if we could get window size on init
    if (getWindowSize(&E.screenrows, &E.screenrows) == -1) {
        die("getWindowSize");
    }
}





/*** init ***/
int main()
{
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}