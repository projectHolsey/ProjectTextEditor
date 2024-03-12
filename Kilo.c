// /*** includes ***/

// feature test macros - being imported from header files
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h> // Control characters
#include <stdio.h> // standard IO module for printf
#include <errno.h> 
#include <string.h>
#include <sys/ioctl.h> // Get size of terminal window
#include <sys/types.h> // malloc & ssize_t come from this import
#include <stdlib.h> // standard library - type conversion, mem alloc...
#include <termios.h> // importing variables for terminal
#include <time.h> 
#include <unistd.h>  // importing standard io module for input keys


// /*** defines ***/

// const string to show version of the program
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

// defining a constant / function
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  // giving thee arrow keys a representation that doesn't clash with char type 
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  HOME_KEY,
  END_KEY,
  DEL_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** data ***/

// data type for storing row in text editor
typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render; // rendering tabs and other special chars
} erow;


// global struct to contain editor's state
struct editorConfig {
  // cursor position
  int cx, cy;
  int rx; // horizontal co-ordinate for tabs e.t.c
  int rowoff; // vertical scrolling
  int coloff; // horizontal scrolling
  int screenrows;
  int screencols;
  int numrows;
  erow *row; // storing multiple lines
  char *filename; // adding filename for status bar
  char statusmsg[80]; // creating status message line under status bar
  time_t statusmsg_time;  // current time of the status msg

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
int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) {
      die("read");
    }
  }

  if (c == '\x1b') {

    char seq[3];

    if (read(STDIN_FILENO,  &seq[0], 1) != 1) {
      return '\x1b';
    }
    if (read(STDIN_FILENO,  &seq[1], 1) != 1) {
      return '\x1b';
    }

    // replacing up|down|left|right arrows with WASD
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {


        if (read(STDIN_FILENO, &seq[2], 1) != 1){
          return '\x1b';
        }

        // if byte after [ is a digit
        // read another byte expecting ~
        // See if digit byte was 5 or 6
        if (seq[2] == '~'){
          switch(seq[1]){
            case '1': 
              return HOME_KEY;
            case '3':
              return DEL_KEY;
            case '4':
              return END_KEY;
            case '5':
              return PAGE_UP;
            case '6':
              return PAGE_DOWN;
            case '7':
              return HOME_KEY;
            case '8':
              return END_KEY;

          }
        }
      } else { 
        switch(seq[1]) {
          case 'A': 
            return ARROW_UP;
          case 'B': 
            return ARROW_DOWN;
          case 'C': 
            return ARROW_RIGHT;
          case 'D': 
            return ARROW_LEFT;
          case 'H':
            return HOME_KEY;
          case 'F':
            return END_KEY;
        }
      }
    } else if (seq[0] == 'O'){
      switch (seq[1]) {
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
      }
    }      
    return '\x1b';

  } else {
    return c;
  }
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




// // // get the size of the terminal
int getWindowSize(int *rows, int *cols) {

  //ioctl(), TIOCGWINSZ, and struct winsize come from <sys/ioctl.h>.
  struct winsize ws;

  // on success, system call will place numbers of cols and rows in winsize struct
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {

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

/** file I/O ***/

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    // if the current char we're looping over is tab
    if (row->chars[j] == '\t') {
      // Add tab 'spaces' to move cursor in row
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    }
    rx++;
  }
  return rx;
}

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++) {
    // Check if tabs char is present in row to be rendered
    if (row->chars[j] == '\t') {
      tabs++;
    }
  }

  // free the memory currently in use
  free(row->render);
  // Allocate new memory as row size +1 + tabs*7
  row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);

  int idx = 0;
  // Loop through all chars in row
  for (j = 0; j < row->size; j++) {
    // if current char is tab
    if (row->chars[j] == '\t') {
      // add in spaces for count of 8 (or.. sometimes it's less dependent on how far away end of tab is)
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) {
        row->render[idx++] = ' ';
      }
    } else{ 
      // copy them to render array
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0'; // append end of line char
  row->rsize = idx; // size of row

}


void editorAppendRow(char *s, size_t len){

  // allocating space for new erow
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  // copy given string to end of eRow
  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  // Copy the line to chars in row
  memcpy(E.row[at].chars, s, len);
  // each erow represents 1 line of text, so no need for the new line
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]); // pass reference to current row

  E.numrows++;
}


/*** file i/o ***/

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename); // strdup comes from string.h
  // makes copy of given string, allocating required memory and assuming you will free the memory
  
  FILE *fp = fopen(filename, "r");
  
  if (!fp) {
    die("fopen");
  }

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  // read lines from file
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    // if linelen == -1, it's at the end of the file
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
      linelen--;
   
    }
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}


/*** append buffer ***/
// Instead of doing x small writes, we are making buffer to do 1 big write

// creating our own dynamic string type
struct abuf {
  char *b;
  int len;
};

// acts as constructor for the abuf type
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  // realloc comes from <stdlib.h>
  // makes sure we have enough memory to hold new string
  char *new = realloc(ab->b, ab->len + len);
  // gives us memory = cur_mem_size + new_thing_to_append

  // return if the size is null
  if (new == NULL) {
    return;
  }

  // memcpy comes from <string.h>
  // copy string s after the end of current data
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;

}


// deconstructor that deallocates dynamic memory used by an abuf
void abFree(struct abuf *ab){
  // comes from <stdlib.h>
  free(ab->b);
}


/*** output ***/
void editorScroll() {
  E.rx = 0; // change cursor to be render item not chars
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  // check if cursor moved outside of visible window
  // adjust E.rowoff so cursor is just inside visible window 
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  } 
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }

  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}



void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff; // displaying correct line of the file if reading from file

    // check if the row we're drawing is part of text buffer or row that comes before / after
    if (filerow >= E.numrows) {

      // don't write the welcome message if we have read in a file
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        // snprinft - comes from <stdio.h>
        // interpolate KILO_VERSION string into welcome msg
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "Kilo editor -- version %s", KILO_VERSION);

        // truncate string length if terminal is too small
        if (welcomelen > E.screencols) {
          welcomelen = E.screencols;
        }

        // creating padding around the welcome string in the terminal
        int padding = (E.screencols - welcomelen) / 2;

        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) {
          abAppend(ab, " ", 1);
        }
        abAppend(ab, welcome, welcomelen);
      } else {
        // making sure we write ~ on every row
        // write(STDOUT_FILENO, "~", 1);
        abAppend(ab, "~", 1);

      }
    } else {

      // displaying correct row at each y position of text editor
      // adjusting for coloff(set) to keep x position correct too
      int len = E.row[filerow].rsize - E.coloff;
      
      if (len < 0) {
        len = 0;
      }
      if (len > E.screencols) {
        len = E.screencols;
      }
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }


    abAppend(ab, "\x1b[K", 3); // clear the line

    abAppend(ab, "\r\n", 2);

  }
}

void editorDrawStatusBar(struct abuf *ab) {
  // explaining m commands - https://vt100.net/docs/vt100-ug/chapter3.html#SGR
  abAppend(ab, "\x1b[7m", 4); // escape sequence - switches to inverted colours
  char status[80], rstatus[80];
  
  // getting length of row to write
  // Copying filename / [no name] to buffer
  int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows);
  
  // Render line also includes the current line number at right edge of screen
  int rlen = snprintf(rstatus, sizeof(rstatus),  "%d/%d", E.cy + 1, E.numrows);

  // Cut string short if it's too big..
  if (len > E.screencols) {
    len = E.screencols;
  }
  // write to the screen 
  abAppend(ab, status, len);
  
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }

  abAppend(ab, "\x1b[m", 3); // escape sequence - switches back to normal colours
}


// Clears the terminal
void editorRefreshScreen() {
  editorScroll();

  // init the new dynamic memo string buffer
  struct abuf ab = ABUF_INIT;

  // removing the cursor flicker
  abAppend(&ab, "\x1b[?25l", 6); // l = set mode

  // 4 = write 4 bytes
  // \x1b[2j] -> ESCAPE SEQUENCE

  // \x1b -> escape character
  // [ -> always follows the escape character

  // J -> clear the screen
  // 2 -> clear the entire screen
  // <esc>[1J would clear screen up to where cursor is
  // write(STDOUT_FILENO, "\x1b[2J", 4);
  // abAppend(&ab, "\x1b[2J", 4); -> replaced clear screen with clear line in editorDrawRows

  // Reposition cursor to top left corner after refresh
  // write(STDOUT_FILENO, "\x1b[H", 3);
  abAppend(&ab, "\x1b[H", 3);


  // draw the ~ symbol at beggining of row
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);

  // specifying exact position for the cursor to move to
  // 
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));


  // returning cursor flicker
  abAppend(&ab, "\x1b[?25h", 6); // h = reset mode

  // write the buffer to the screen
  write(STDOUT_FILENO, ab.b, ab.len);

  // free the buffer after the write
  abFree(&ab);

}

/*** input ***/

void editorMoveCursor(int key) {

  // Limiting the scroll to right.
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        // If users oge soff to left  of the screen, then move them to end of row on next line up
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size){
        // changed to allow user to scroll to the right of screen
        E.cx++;
      } else if (row && E.cx == row->size) {
        // If users goes off of left of screen, move them 1 line up and to the end of row.
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      // allowing cursor to move past bottom of screen, but not past EoF
      if (E.cy < E.numrows) {
        E.cy++;
      }
      break;
   }

  
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}



// wait for keypress, handles it later
void editorProcessKeypress() {
  int c = editorReadKey();

  switch(c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    // making home key jump to beigging of line
    case HOME_KEY:
      E.cx = 0;
      break;
    
      // making end key jump to end of line
    case END_KEY:
      if (E.cy < E.numrows) {
        E.cx = E.row[E.cy].size;
      }
      
      break;

    // Move cursor to top or bottom of page
    case PAGE_DOWN:
    case PAGE_UP:
      {

        // Adding scroll capabilties for page up / down  
        if (c == PAGE_UP){
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) {
            E.cy = E.numrows;
          }
        }

        // can only declare vars inside { }
        int times = E.screenrows;
        while (times--) {
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
      }
      break;

    // allowing user to move around screen with wasd keys
    case ARROW_RIGHT:
    case ARROW_LEFT:
    case ARROW_DOWN:
    case ARROW_UP:
      editorMoveCursor(c);
      break;
  }
}


/*** init ***/
void initEditor() {
  E.cy = 0;
  E.cx = 0;
  E.rx = 0;
  E.rowoff = 0; // scroll to top by default
  E.coloff = 0;
  E.numrows = 0; // will only display a single line of text
  E.row = NULL;
  E.filename = NULL; // initalised to NULL - will stay if there's no file read in

  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  // Check if we could get window size on init
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
    die("getWindowSize");
  }

  E.screenrows -= 1; // Decremented so editor doesnt draw row at bottom of screen
  // Allows us t make teh status bar
}





/*** init ***/
int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  // if there's a file, open the file
  if (argc >= 2) { 
    
    editorOpen(argv[1]);
  }
  

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}







