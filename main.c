#include <ctype.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define _TOUT STDOUT_FILENO
#define _TIN STDIN_FILENO

struct chip_config {
	int n_rows, n_cols;
	struct termios start_termios;
} config;

/* Terminal Initialisation */
void init_chip();
void disable_raw_mode();
void enable_raw_mode();
int get_window_size(int *rows, int *cols);
int get_cursor_position(int *rows, int *cols);

/* Input Handling */
char chip_read_key();
void chip_process_key();

/* Output */
void chip_clear_screen();
void chip_refresh_screen();
void chip_draw_rows();
	
/* Error Handling */
void die(const char *s);

int main() {
	enable_raw_mode();
	init_chip();

	while (1) {
		chip_clear_screen();
		chip_process_key();
	}

	return 0;
}

void init_chip() {
	if (get_window_size(&config.n_rows, &config.n_cols) == -1)
		die("Get Window Size");
}

char chip_read_key() {
	char c;
	int nread;
	while ((nread = read(_TIN, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	return c;
}

void chip_process_key() {
	char c = chip_read_key();

	switch (c) {
		case CTRL_KEY('q'):
			chip_clear_screen();
			exit(0);
			break;
	}
}

void chip_clear_screen() {
	write(_TOUT, "\x1b[2J", 4); /* Clear Screen */
	write(_TOUT, "\x1b[H", 3); 	/* Reset Cursor */
}

void chip_refresh_screen() {
	chip_clear_screen();

	chip_draw_rows();
	write(_TOUT, "\x1b[H", 3);  /* Reset Cursor */
}

void chip_draw_rows() {
	int y;
	for (y = 0; y < config.n_rows; ++y) {
		write(_TOUT, "~", 1);
		if (y < config.n_rows - 1) write(_TOUT, "\r\n", 2);
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
	chip_clear_screen();
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
