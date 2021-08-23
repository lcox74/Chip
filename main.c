#include <ctype.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

#define CHIP_VERSION "0.1"

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

struct chip_config {
    int cx, cy;
    int n_rows, n_cols;
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

/* Input Handling */
int chip_read_key();
void chip_process_key();
void chip_move_cursor(int key);

/* Output */
void chip_refresh_screen();
void chip_draw_rows(struct abuf *ab);
    
/* Error Handling */
void die(const char *s);

int main() {
    enable_raw_mode();
    init_chip();

    while (1) {
        chip_refresh_screen();
        chip_process_key();
    }

    return 0;
}

void init_chip() {
    config.cx = 0; config.cy = 0;

    if (get_window_size(&config.n_rows, &config.n_cols) == -1)
        die("Get Window Size");
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
            write(_TOUT, "\x1b[2J", 4);
            write(_TOUT, "\x1b[H", 3);
            exit(0);
            break;
        case HOME_KEY: config.cx = 0; break;
        case END_KEY: config.cy = config.n_cols - 1; break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
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
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); /* Hide cursor */
    abAppend(&ab, "\x1b[H", 3);    /* Reset Cursor */

    chip_draw_rows(&ab);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", config.cy + 1, config.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); /* Unhide cursor */
    
    write(_TOUT, ab.b, ab.len);
    abFree(&ab);
}

void chip_draw_rows(struct abuf *ab) {
    int y;
    for (y = 0; y < config.n_rows; ++y) {
        if (y == config.n_rows / 3) {
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

        abAppend(ab, "\x1b[K",3);
        if (y < config.n_rows - 1) abAppend(ab, "\r\n", 2);
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
    write(_TOUT, "\x1b[2J", 4);
    write(_TOUT, "\x1b[H", 3);
    perror(s);
    exit(1);
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
    switch (key) {
        case ARROW_UP:    if (config.cy != 0)               config.cy--; break;
        case ARROW_LEFT:  if (config.cx != 0)               config.cx--; break;
        case ARROW_DOWN:  if (config.cy != config.n_rows)   config.cx++; break;
        case ARROW_RIGHT: if (config.cx != config.n_cols)   config.cy++; break;
    }
}
