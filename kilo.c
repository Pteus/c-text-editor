/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/ttycom.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** data ***/
struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/
void die(const char *s) {
  // clear the screen
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}
void disableRawMode(void) {
  // TCSAFLUSH - discards any unread input before applying the changes to the
  // terminal.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode(void) {
  // Terminal attributes can be read into a termios struct by tcgetattr()
  // store original terminal settings
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");

  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  // ECHO is a bitflag, defined as 00000000000000000000000000001000 in binary.
  // We use the bitwise-NOT operator (~) on this value to get
  // 11111111111111111111111111110111. We then bitwise-AND this value with the
  // flags field, which forces the fourth bit in the flags field to become 0,
  // and causes every other bit to retain its current value. Flipping bits like
  // this is common in C.

  raw.c_cflag |= (CS8);
  // turns off Ctrl-S and Ctrl-Q, carriage return and new line
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  // turns off ECHO, Canonical mode, CTRL C CTRL Z, CTRL v
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  // turns off "\n" to "\r\n"
  raw.c_oflag &= ~(OPOST);

  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  // apply the the changes
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

char editorReadKey(void) {
  int nread;
  char c;

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  return c;
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;
  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** output ***/
void editorDrawRows(void) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    write(STDOUT_FILENO, "~", 1);
    if (y < E.screenrows - 1) {
      write(STDOUT_FILENO, "\r\n", 2);
    }
  }
}

// clear the screen
void editorRefreshScreen(void) {
  // \x1b is the escape character - 27 in decimal
  // Escape sequences always start with an escape character (27) followed by a [
  // character
  // J - erase in display
  // 2 - means all the screen
  write(STDOUT_FILENO, "\x1b[2J", 4);

  // H - cursor position
  // The default arguments for H both happen to be 1, so we can leave both
  // arguments out and it will position the cursor at the first row and first
  // column, as if we had sent the <esc>[1;1H command
  write(STDOUT_FILENO, "\x1b[H", 3);

  editorDrawRows();
  write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input ***/
void editorProcessKeypress(void) {
  char c = editorReadKey();
  switch (c) {
  case CTRL_KEY('q'):
    // clear the screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    exit(0);
    break;
  }
}

/*** init ***/

void initEditor(void) {
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
}

int main(void) {
  // terminal starts in canonical mode - the input is only sent to the program
  // when enter is pressed this enales raw mode.
  enableRawMode();

  initEditor();

  // reads 1 byte from the standard input into c, and keeps doing it until there
  // are no more bytes to read
  // press q to quit
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}
