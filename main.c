#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>

#define CHIP_VERSION "0.1"
#define CHIP_TAB_SIZE 4

#define CTRL_KEY(k) ((k) & 0x1f)
#define _TOUT STDOUT_FILENO
#define _TIN STDIN_FILENO


enum chip_key {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

struct chip_config {
    int cx, cy, rx;
    int rowoff;
    int coloff;
    int n_rows, n_cols;
    int numrows;
    erow *row;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios start_termios;
};

struct abuf {
    char *b;
    int len;
};
#define ABUF_INIT {NULL, 0}
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

struct chip_config config;

/* Terminal Initialisation */
void init_chip();
void disable_raw_mode();
void enable_raw_mode();
int get_window_size(int *rows, int *cols);
int get_cursor_position(int *rows, int *cols);

/* Message Bar */
void chip_set_status_message(const char *fmt, ...);
void chip_draw_message_bar(struct abuf *ab);

/* Input Handling */
int chip_read_key();
void chip_process_key();
void chip_move_cursor(int key);

/* Output */
void chip_refresh_screen();
void chip_draw_rows(struct abuf *ab);
    
/* Error Handling */
void die(const char *s);

/* File I/O */
void chip_open(const char* filename);

/* Row Operations */
void chip_append_row(char *s, size_t len);
void chip_update_row(erow *row);
int chip_row_cx_to_rx(erow *row, int cx);
void chip_scroll();
void chip_draw_status_bar(struct abuf *ab);

int main(int argc, char* argv[]) {
    enable_raw_mode();
    init_chip();
    if (argc >= 2) chip_open(argv[1]);

    chip_set_status_message("HELP: Ctrl-Q = quit");

    while (1) {
        chip_refresh_screen();
        chip_process_key();
    }

    return 0;
}

void init_chip() {
    config.cx = 0; config.cy = 0; config.rx = 0;
    config.rowoff = 0; config.coloff = 0;
    config.numrows = 0;
    config.row = NULL;
    config.filename = NULL;
    config.statusmsg[0] = '\0';
    config.statusmsg_time = 0;

    if (get_window_size(&config.n_rows, &config.n_cols) == -1)
        die("Get Window Size");
    
    config.n_rows -= 2;
}

int chip_read_key() {
    char c;
    int nread;
    while ((nread = read(_TIN, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(_TIN, &seq[0], 1) != 1) return '\x1b';
        if (read(_TIN, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] < '9') {
                if (read(_TIN, &seq[2], 1) != 1) return '\x1b';
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
        return '\x1b';
    }

    return c;
}

void chip_process_key() {
    int c = chip_read_key();

    switch (c) {
        case CTRL_KEY('q'):
            if (write(_TOUT, "\x1b[2J\x1b[H", 7) == -1)
                die("Failed to quit properly");
            exit(0);
            break;
        case HOME_KEY: config.cx = 0; break;
        case END_KEY: {
            if (config.cy < config.numrows)
                config.cx = config.row[config.cy].size; 
            break;
        }
        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    config.cy = config.rowoff;
                } else if (c == PAGE_DOWN) {
                    config.cy = config.rowoff + config.n_rows - 1;
                    if (config.cy > config.numrows) config.cy = config.numrows;
                }

                int times = config.n_rows;
                while (times--) chip_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            chip_move_cursor(c);
            break;
    }
}

void chip_refresh_screen() {
    chip_scroll();
    
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); /* Hide cursor */
    abAppend(&ab, "\x1b[H", 3);    /* Reset Cursor */

    chip_draw_rows(&ab);
    chip_draw_status_bar(&ab);
    chip_draw_message_bar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (config.cy - config.rowoff) + 1, 
                                              (config.rx - config.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); /* Unhide cursor */
    
    if (write(_TOUT, ab.b, ab.len) == -1)   die("Failed to refresh screen");
}

void chip_draw_rows(struct abuf *ab) {
    int y;
    for (y = 0; y < config.n_rows; ++y) {
        int filerow = y + config.rowoff;
        if (filerow >= config.numrows) {
            if (config.numrows == 0 && y == config.n_rows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                        "Chip Editor -- version %s", CHIP_VERSION);

                if (welcomelen > config.n_cols) welcomelen = config.n_cols;
                
                int padding = (config.n_cols - welcomelen) / 2;

                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);

                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = config.row[filerow].rsize - config.coloff;
            if (len < 0) len = 0;
            if (len > config.n_cols) len = config.n_cols;
            abAppend(ab, &config.row[filerow].render[config.coloff], len);
        }

        abAppend(ab, "\x1b[K",3);
        abAppend(ab, "\r\n", 2);
    }
}

int get_cursor_position(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(_TOUT, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) -1) {
        if (read(_TIN, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        ++i;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int get_window_size(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(_TOUT, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(_TOUT, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return get_cursor_position(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }
    return 0;
}

void die(const char *s) {
    if (write(_TOUT, "\x1b[2J\x1b[H", 7) == -1) {
        perror(s);
        exit(1);
    }    
}

void enable_raw_mode() {
    if (tcgetattr(_TIN, &config.start_termios) == -1) die("tcgetattr");
    atexit(disable_raw_mode);

    struct termios raw = config.start_termios;

    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(_TIN, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

void disable_raw_mode() {
    if(tcsetattr(_TIN, TCSAFLUSH, &config.start_termios) == -1)
        die("tcsetattr");
}

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

void chip_move_cursor(int key) {
    erow *row = (config.cy >= config.n_rows) ? NULL : &config.row[config.cy];
    switch (key) {
        case ARROW_UP: {
            if (config.cy != 0) config.cy--; 
            break;
        }
        case ARROW_LEFT:  {
            if (config.cx != 0) config.cx--;
            else if (config.cy > 0) {
                config.cy--;
                config.cx = config.row[config.cy].size;
            }
            break;
        }
        case ARROW_DOWN: {
            if (config.cy < config.numrows) config.cy++; 
            break;
        }
        case ARROW_RIGHT: {
            if (row && config.cx < row->size) config.cx++; 
            else if (row && config.cx == row->size) {
                config.cy++;
                config.cx = 0;
            }
            break;
        }
    }

    row = (config.cy >= config.numrows) ? NULL : &config.row[config.cy];
    int rowlen = row ? row->size : 0;
    if (config.cx > rowlen) config.cx = rowlen;
}

void chip_open(const char* filename) {
    free(config.filename);
    config.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            linelen--;
        }
        chip_append_row(line, linelen);
    }

    free(line);
    fclose(fp);
}

void chip_append_row(char *s, size_t len) {
    config.row = realloc(config.row, sizeof(erow) * (config.numrows + 1));

    int at = config.numrows;
    config.row[at].size = len;
    config.row[at].chars = malloc(len + 1);
    memcpy(config.row[at].chars, s, len);
    config.row[at].chars[len] = '\0';

    config.row[at].rsize = 0;
    config.row[at].render = NULL;
    chip_update_row(&config.row[at]);

    config.numrows++;
}

void chip_scroll() {
    config.rx = 0;
    if (config.cy < config.numrows) 
        config.rx = chip_row_cx_to_rx(&config.row[config.cy], config.cx);

    if (config.cy < config.rowoff)  config.rowoff = config.cy;
    if (config.rx < config.coloff) config.coloff = config.rx;

    if (config.cy >= config.rowoff + config.n_rows) 
        config.rowoff = config.cy - config.n_rows + 1;
    if (config.rx >= config.coloff + config.n_cols) 
        config.coloff = config.rx - config.n_cols + 1;
}

void chip_update_row(erow *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++) 
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + (tabs * (CHIP_TAB_SIZE-1)) + 1);

    int idx = 0;
    for (j = 0; j < row->size; ++j) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % CHIP_TAB_SIZE != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

int chip_row_cx_to_rx(erow *row, int cx) {
    int j, rx = 0;
    for (j = 0; j < cx; ++j) {
        if (row->chars[j] == '\t')
            rx += (CHIP_TAB_SIZE - 1) - (rx % CHIP_TAB_SIZE);
        rx++;
    }
    return rx;
}

void chip_draw_status_bar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m",4);
    char status[80], rstatus[80];

    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
        config.filename ? config.filename : "[NO FILE]", config.numrows);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", config.cy + 1, 
        config.numrows);

    if (len > config.n_cols) len = config.n_cols;
    abAppend(ab, status, len);
    while (len < config.n_cols) {
        if (config.n_cols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m\r\n", 5);
}

void chip_set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(config.statusmsg, sizeof(config.statusmsg), fmt, ap);
    va_end(ap);
    config.statusmsg_time = time(NULL);
}

void chip_draw_message_bar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(config.statusmsg);
    if (msglen > config.n_cols) msglen = config.n_cols;
    if (msglen && time(NULL) - config.statusmsg_time < 5)
        abAppend(ab, config.statusmsg, msglen);
}