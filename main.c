#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

enum Keys
{
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	PAGE_UP,
	PAGE_DOWN
};

typedef struct
{
	int size;
	int rsize;
	char *chars;
	char *render;
} row;

struct
{
	int cx, cy;
	int rx;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int line_count;
	row *text;
	struct termios default_termios;
	int tab_stop;
} E;

void die(const char *s)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

void disableRawMode()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.default_termios) == -1)
		die("DisableRawMode: tcsetatrr");
}

void enableRawMode()
{
	if (tcgetattr(STDIN_FILENO, &E.default_termios) == -1) die("EnableRawMode: tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.default_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("EnableRawMode: tcsetattr");
}

int editorReadKey()
{
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("EditorReadKey: read");
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
					}
				}
			} else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
				}
			}
		}
		return '\x1b';
	} else {
		return c;
	}
}

int getCursorPosition(int *rows, int *cols)
{
	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	while (i < sizeof(buf) - 1)
	{
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

int editorRowCxToRx(row *row, int cx)
{
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t')
			rx += (E.tab_stop - 1) - (rx % E.tab_stop);
		rx++;
	}
	return rx;
}

void editorUpdateRow(row *line)
{
	int tabs = 0;
	int j;
	for (j = 0; j < line->size; j++)
		if (line->chars[j] == '\t') tabs++;

	free(line->render);
	line->render = malloc(line->size + tabs*(E.tab_stop - 1) + 1);

	int idx = 0;
	for (j = 0; j < line->size; j++) {
		if (line->chars[j] == '\t') {
			line->render[idx++] = ' ';
			while (idx % E.tab_stop != 0) line->render[idx++] = ' ';
		} else {
			line->render[idx++] = line->chars[j];
		}
	}
	line->render[idx] = '\0';
	line->rsize = idx;
}

void editorAppendRow(char *s, size_t len)
{
	E.text = realloc(E.text, sizeof(row) * (E.line_count + 1));

	int at = E.line_count;
	E.text[at].size = len;
	E.text[at].chars = malloc(len + 1);
	memcpy(E.text[at].chars, s, len);
	E.text[at].chars[len] = '\0';

	E.text[at].rsize = 0;
	E.text[at].render = NULL;
	editorUpdateRow(&E.text[at]);

	E.line_count++;
}

void editorOpen(char *filename)
{
	FILE *fp = fopen(filename, "r");
	if (!fp) die("EditorOpen: fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
}

typedef struct
{
	char *b;
	int len;
} buffer;

#define BUFFER_INIT {NULL, 0}

void bufferAppend(buffer *buf, const char *s, int len)
{
	char *new = realloc(buf->b, buf->len + len);

	if (new == NULL) return;
	memcpy(&new[buf->len], s, len);
	buf->b = new;
	buf->len += len;
}

void editorScroll()
{
	E.rx = 0;
	if (E.cy < E.line_count) {
		E.rx = editorRowCxToRx(&E.text[E.cy], E.cx);
	}

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

void editorDrawRows(buffer *buf)
{
	int y;
	for (y = 0; y < E.screenrows; y++)
	{
		int filerow = y + E.rowoff;
		if (filerow >= E.line_count) {
			bufferAppend(buf, "~", 1);
		} else {
			int len = E.text[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			bufferAppend(buf, &E.text[filerow].render[E.coloff], len);
		}

		bufferAppend(buf, "\x1b[K", 3);
		if (y < E.screenrows - 1) {
			bufferAppend(buf, "\r\n", 2);
		}
	}
}

void editorRefreshScreen()
{
	editorScroll();

	buffer b = BUFFER_INIT;

	bufferAppend(&b, "\x1b[?25l", 6);
	bufferAppend(&b, "\x1b[H", 3);

	editorDrawRows(&b);

	char buf[32];
	snprintf(buf, sizeof(buf),"\x1b[%d;%dH",
			(E.cy - E.rowoff) + 1,
			(E.rx - E.coloff) + 1);
	bufferAppend(&b, buf, strlen(buf));

	bufferAppend(&b, "\x1b[?25h", 6);

	write(STDOUT_FILENO, b.b, b.len);
	free(b.b);
}

void editorMoveCursor(int key)
{
	row *line = (E.cy >= E.line_count) ? NULL : &E.text[E.cy];

	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			} else if (E.cy > 0) {
				E.cy--;
				E.cx = E.text[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (line && E.cx < line->size) {
				E.cx++;
			} else if (line && E.cx == line->size) {
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
			if (E.cy < E.line_count) {
				E.cy++;
			}
			break;
	}

	line = (E.cy >= E.line_count) ? NULL : &E.text[E.cy];
	int rowlen = line ? line->size : 0;
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}

void editorProcessKeypress()
{
	int c = editorReadKey();

	switch (c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
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
	}
}

void initEditor()
{
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.line_count = 0;
	E.text = NULL;
	E.tab_stop = 8;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("EditorInit: getWindowSize");
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}
