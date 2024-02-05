#include "my_editor/libraries.h"

// This enum defines key codes for various editor keys.
enum editorKey {
  BACKSPACE = 127,   // ASCII value for backspace key
  ARROW_LEFT = 1000, // Custom value for left arrow key
  ARROW_RIGHT,       // Automatically increments from the previous value
  ARROW_UP,          // Automatically increments
  ARROW_DOWN,        // Automatically increments
  DEL_KEY,           // Delete key
  HOME_KEY,          // Home key
  END_KEY,           // End key
  PAGE_UP,           // Page up key
  PAGE_DOWN          // Page down key
};

// This enum defines highlight types for syntax highlighting.
enum editorHighlight {
  HL_NORMAL = 0,     // Normal text
  HL_COMMENT,        // Comment
  HL_MLCOMMENT,      // Multi-line comment
  HL_KEYWORD1,       // First category of keywords
  HL_KEYWORD2,       // Second category of keywords
  HL_STRING,         // String literal
  HL_NUMBER,         // Numeric literal
  HL_MATCH           // Matching character (for example, a closing parenthesis)
};

// Bitwise flags for enabling specific types of syntax highlighting.
#define HL_HIGHLIGHT_NUMBERS (1<<0) // Flag for highlighting numbers
#define HL_HIGHLIGHT_STRINGS (1<<1) // Flag for highlighting strings


/*** data ***/

// Struct to define syntax highlighting rules for a specific filetype.
struct editorSyntax {
  char *filetype;               // Name of the supported file type (e.g., "C" or "Python").
  char **filematch;            // Array of file extensions associated with this filetype.
  char **keywords;              // Array of keywords for syntax highlighting.
  char *singleline_comment_start; // Starting characters for single-line comments.
  char *multiline_comment_start;  // Starting characters for multi-line comments.
  char *multiline_comment_end;    // Ending characters for multi-line comments.
  int flags;                    // Flags for enabling specific highlighting features (e.g., numbers or strings).
};

// Struct to represent a row of text in the editor.
typedef struct erow {
  int idx;                  // Index of the row in the editor.
  int size;                 // Size of the row's character buffer.
  int rsize;                // Size of the row's render buffer (used for tabs).
  char *chars;              // Buffer containing the actual text characters.
  char *render;             // Buffer for rendering characters with tabs and syntax highlighting.
  unsigned char *hl;        // Array storing the syntax highlighting information for each character.
  int hl_open_comment;      // Flag indicating if the row has an open multi-line comment.
} erow;

// Struct to represent the editor's configuration and state.
struct editorConfig {
  int cx, cy;               // Current cursor position (x, y) in characters.
  int rx;                   // Current cursor position in render (used for tabs).
  int rowoff;               // Offset of the top visible row in the text.
  int coloff;               // Offset of the leftmost visible column in the text.
  int screenrows;           // Number of rows visible on the screen.
  int screencols;           // Number of columns visible on the screen.
  int numrows;              // Total number of rows in the editor.
  erow *row;                // Array of erow structs to store the text rows.
  int dirty;                // Flag to indicate if there are unsaved changes.
  char *filename;           // Current filename (if applicable).
  char statusmsg[80];       // Status message (e.g., for displaying errors or prompts).
  time_t statusmsg_time;    // Time at which the status message was set.
  struct editorSyntax *syntax; // Pointer to the syntax highlighting rules for the current file type.
  struct termios orig_termios; // Original terminal settings for the editor.
};

// Global instance of the editor configuration.
struct editorConfig E;

/*** filetypes ***/

// Array of file extensions supported for C/C++ syntax highlighting.
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };

// Array of keywords and data types for C/C++ syntax highlighting.
char *C_HL_keywords[] = {
  // Control flow keywords
  "switch", "if", "while", "for", "break", "continue", "return", "else",

  // Type-related keywords
  "struct", "union", "typedef", "static", "enum", "class", "case",

  // Primitive data types
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

// Struct to define syntax highlighting rules for a specific filetype (e.g., C/C++).
struct editorSyntax HLDB[] = {
  {
    "c",                                // Filetype (e.g., "c" for C/C++).
    C_HL_extensions,                    // Supported file extensions.
    C_HL_keywords,                      // Keywords and data types.
    "//", "/*", "*/",                   // Comment delimiters (single-line and multi-line).
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS // Flags for enabling specific highlighting features.
  },
};

// Macro to calculate the number of entries in the syntax highlighting database (HLDB).
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))


/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

/*** terminal ***/

// Function to print an error message and terminate the program.
void die(const char *s) {
  // Clear the screen and move the cursor to the top-left corner.
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  // Print the error message and exit with an error code.
  perror(s);
  exit(1);
}

// Function to disable raw mode for the terminal.
void disableRawMode() {
  // Restore the original terminal settings.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

// Function to enable raw mode for the terminal.
void enableRawMode() {
  // Save the original terminal settings for later restoration.
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  // Create a new termios struct with modified settings for raw mode.
  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  // Apply the modified settings to the terminal.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

// Function to read a keypress from the user.
int editorReadKey() {
  int nread;
  char c;

  // Keep reading until a keypress is received.
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  // Handle escape sequences for special keys.
  if (c == '\x1b') {
    char seq[3];

    // Read additional characters for escape sequences.
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    // Process different escape sequences and map them to key constants.
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b'; // Return escape character if no special sequence is matched.
  } else {
    return c; // Return the regular character.
  }
}

// Function to retrieve the cursor position in the terminal.
int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  // Request cursor position report from the terminal.
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  // Read the response from the terminal.
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  // Parse the response to extract the cursor position.
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

// Function to retrieve
/*** syntax highlighting ***/

// Function to check if a character is a separator (whitespace or specific characters).
int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

// Function to update syntax highlighting for a row of text.
void editorUpdateSyntax(erow *row) {
  // Resize the row's syntax highlight array and initialize it with HL_NORMAL.
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);

  // If no syntax highlighting rules are defined, return.
  if (E.syntax == NULL) return;

  // Extract syntax highlighting rules and settings.
  char **keywords = E.syntax->keywords;
  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;
  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;
  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    // Handle single-line comments.
    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }

    // Handle multi-line comments.
    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }

    // Handle string literals with escape sequences.
    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }

    // Handle numeric literals.
    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    // Handle keyword highlighting.
    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2) klen--;

        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(c);
    i++;
  }

  // Update the row's open comment state and trigger updates for subsequent rows if needed.
  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    editorUpdateSyntax(&E.row[row->idx + 1]);
}

// Function to map a syntax highlight type to a terminal color.
int editorSyntaxToColor(int hl) {
  switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36; // Cyan
    case HL_KEYWORD1: return 33; // Yellow
    case HL_KEYWORD2: return 32; // Green
    case HL_STRING: return 35; // Magenta
    case HL_NUMBER: return 31; // Red
    case HL_MATCH: return 34; // Blue
    default: return 37; // Default (white)
  }
}

// Function to select the appropriate syntax highlighting rules based on the file's extension.
void editorSelectSyntaxHighlight() {
  E.syntax = NULL;

  // Return if there is no filename associated with the editor.
  if (E.filename == NULL) return;

  // Iterate through the syntax highlighting database to find a match.
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;

    // Check if the file extension matches any in the current syntax entry.
    while (s->filematch[i]) {
      char *p = strstr(E.filename, s->filematch[i]);
      if (p != NULL) {
        int patlen = strlen(s->filematch[i]);

        // Check if the match is exact or if it occurs at the end of the filename.
        if (s->filematch[i][0] != '.' || p[patlen] == '\0') {
          E.syntax = s;

          // Update syntax highlighting for all rows in the editor.
          int filerow;
          for (filerow = 0; filerow < E.numrows; filerow++) {
            editorUpdateSyntax(&E.row[filerow]);
          }

          return;
        }
      }
      i++;
    }
  }
}


/*** row operations ***/

// Function to convert the character index (cx) to the visual index (rx) for rendering.
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }
  return rx;
}

// Function to convert the visual index (rx) to the character index (cx).
int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
    cur_rx++;

    if (cur_rx > rx) return cx;
  }
  return cx;
}

// Function to update the rendered version of a row.
void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  // Update syntax highlighting for the row.
  editorUpdateSyntax(row);
}

// Function to insert a new row at a specific position.
void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
  for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

  E.row[at].idx = at;

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

// Function to free memory associated with a row.
void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

// Function to delete a row at a specific position.
void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
  E.numrows--;
  E.dirty++;
}

// Function to insert a character into a row at a specific position.
void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

// Function to append a string to the end of a row.
void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

// Function to delete a character from a row at a specific position.
void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}
/*** editor operations ***/

// Function to insert a character into the text editor at the current cursor position
void editorInsertChar(int c) {
  // Check if the cursor is at the end of the document
  if (E.cy == E.numrows) {
    // If so, insert a new empty row
    editorInsertRow(E.numrows, "", 0);
  }
  // Insert the character into the current row at the current cursor position
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  // Move the cursor one position to the right
  E.cx++;
}

// Function to insert a new line (newline) into the text editor
void editorInsertNewline() {
  // Check if the cursor is at the beginning of a line
  if (E.cx == 0) {
    // If so, insert a new empty row before the current line
    editorInsertRow(E.cy, "", 0);
  } else {
    // Otherwise, split the current line at the cursor position
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    // Update the current row's size and null-terminate it at the cursor position
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    // Update the display of the current row
    editorUpdateRow(row);
  }
  // Move the cursor to the beginning of the next line
  E.cy++;
  E.cx = 0;
}

// Function to delete a character from the text editor
void editorDelChar() {
  // Check if the cursor is at the end of the document or at the very beginning
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    // If the cursor is not at the beginning of the line, delete the character to the left
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    // If the cursor is at the beginning of the line, append the current line to the previous line
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    // Delete the current row and move the cursor up one line
    editorDelRow(E.cy);
    E.cy--;
  }
}


/*** file i/o ***/

// Function to convert editor rows to a single string, suitable for saving to a file
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;

  // Calculate the total length needed to store all rows
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  // Allocate memory for the string
  char *buf = malloc(totlen);
  char *p = buf;

  // Copy each row and append a newline character
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

// Function to open a file in the text editor
void editorOpen(char *filename) {
  // Free the current filename and set it to the new one
  free(E.filename);
  E.filename = strdup(filename);

  // Select syntax highlighting based on the file's extension
  editorSelectSyntaxHighlight();

  // Open the file for reading
  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  // Read each line from the file and insert it as a row in the editor
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }

  free(line);
  fclose(fp);
  E.dirty = 0;
}

// Function to save the current editor content to a file
void editorSave() {
  if (E.filename == NULL) {
    // If there is no filename, prompt the user for a new one
    E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
    // Select syntax highlighting based on the new filename's extension
    editorSelectSyntaxHighlight();
  }

  int len;
  char *buf = editorRowsToString(&len);

  // Open the file for writing
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}


/*** find ***/

// Function to handle find operations initiated by user input
void editorFindCallback(char *query, int key) {
  // Static variables to remember the last match and search direction
  static int last_match = -1;
  static int direction = 1;

  // Static variables to remember and restore the syntax highlighting of the matched line
  static int saved_hl_line;
  static char *saved_hl = NULL;

  // If there is saved syntax highlighting, restore it and free the memory
  if (saved_hl) {
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }

  // Handle special keys: Enter (Return) or Escape (ESC)
  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  // If there's no last match, set the search direction to forward (1)
  if (last_match == -1) direction = 1;
  int current = last_match;
  int i;
  for (i = 0; i < E.numrows; i++) {
    current += direction;
    if (current == -1) current = E.numrows - 1;
    else if (current == E.numrows) current = 0;

    erow *row = &E.row[current];
    char *match = strstr(row->render, query);
    if (match) {
      // Update the last match and cursor position
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numrows;

      // Save the current line's syntax highlighting and highlight the match
      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

// Function to initiate a find operation and handle user input for search queries
void editorFind() {
  // Save current cursor and display settings
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  // Prompt the user for a search query and invoke the callback function
  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

  // Handle the result of the search query prompt
  if (query) {
    free(query);
  } else {
    // If the user canceled the search, restore previous cursor and display settings
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}


/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** output ***/

// Function to handle scrolling and update the display position
void editorScroll() {
  // Initialize the rendered cursor position (rx) to 0
  E.rx = 0;
  // Calculate the rendered cursor position (rx) for the current row and column (cx)
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  // Scroll the display vertically based on the cursor position
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }

  // Scroll the display horizontally based on the cursor position
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

// Function to draw the visible rows of text on the screen
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      // Display a welcome message or '~' for empty lines
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
        // Display the welcome message
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      // Display the content of the file with syntax highlighting
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) {
        if (iscntrl(c[j])) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);
          if (current_color != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        } else {
          int color = editorSyntaxToColor(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
    }

    // Clear the rest of the line and move to the next line
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}
// Function to draw the status bar at the top of the screen
void editorDrawStatusBar(struct abuf *ab) {
  // Display file information, such as filename, line count, and modification status
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    E.filename ? E.filename : "[No Name]", E.numrows,
    E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
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
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}
// Function to draw the message bar at the bottom of the screen
void editorDrawMessageBar(struct abuf *ab) {
  // Display status messages (e.g., search results or error messages)
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}
// Function to refresh the entire screen
void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  // Hide the cursor, move to the top left corner, and start drawing

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  // Draw the visible rows, status bar, and message bar
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                            (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/

// Function to display a user prompt and capture user input
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  // Initialize buffer size and allocate memory for input buffer
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  // Initialize buffer length and set the first character to null
  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    // Display the prompt and current buffer content in the status bar
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    // Handle various keypresses
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
      // Handle backspace or delete key
    } else if (c == '\x1b') {
      // Handle escape key
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      // Handle enter key
      if (buflen != 0) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      // Handle printable characters
      if (buflen == bufsize - 1) {
        // If the buffer is full, reallocate it with double the size
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    // Invoke the callback function (if provided) with the updated buffer
    if (callback) callback(buf, c);
  }
}

// Function to move the cursor based on arrow key presses
void editorMoveCursor(int key) {
  // Get the current row
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
    // Move cursor left
    // Handle boundary conditions
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      // Move cursor right
      // Handle boundary conditions
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
    // Move cursor up
    // Handle boundary conditions
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
     // Move cursor down
    // Handle boundary conditions
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
// Function to process user keypress events
void editorProcessKeypress() {
  static int quit_times = KILO_QUIT_TIMES;

  int c = editorReadKey();

  switch (c) {
    case '\r':
      editorInsertNewline();
      break;

    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;

    case CTRL_KEY('f'):
      editorFind();
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }

        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    case CTRL_KEY('l'):
    case '\x1b':
      break;

    default:
      editorInsertChar(c);
      break;
  }

  // Reset the quit countdown for unsaved changes
  quit_times = KILO_QUIT_TIMES;
}

/*** init ***/

void initEditor() {
  E.cx = 0;             // Cursor column
  E.cy = 0;             // Cursor row
  E.rx = 0;             // Rendered cursor column
  E.rowoff = 0;         // Row offset for scrolling
  E.coloff = 0;         // Column offset for scrolling

  // Initialize row and file-related variables
  E.numrows = 0;        // Number of rows in the editor
  E.row = NULL;         // Array of editor rows
  E.dirty = 0;          // Track whether the file has unsaved changes
  E.filename = NULL;    // File name (if applicable)

  // Initialize status message and syntax highlighting
  E.statusmsg[0] = '\0';   // Status message text
  E.statusmsg_time = 0;    // Time when the status message was set
  E.syntax = NULL;         // Syntax highlighting rules

  // Get the terminal window size and adjust screen dimensions
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;  // Adjust for status bar and message bar
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  // If a filename is provided as a command-line argument, open the file
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  // Display an initial status message with keyboard shortcuts
  editorSetStatusMessage(
    "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

  // Main loop for handling user input and updating the display
  while (1) {
    editorRefreshScreen();    // Refresh the screen
    editorProcessKeypress();  // Process user keypresses
  }

  return 0;
}