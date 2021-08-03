#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include "ini.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTRL_KEY(k) ((k) & 0x1f)

#define DEFAULT_TAB_STOP 8
#define DEFAULT_NL_WIDTH 6

enum Key
{
	BACKSPACE = 127,

	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,

	UNHANDLED_KEY
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
	// Cursor Position
	int cx, cy;
	int rx;
	// Rendering Offsets
	int rowoff;
	int coloff;
	// Screen Dimensions
	int screenrows;
	int screencols;
	// File State
	char *filename;
	int line_count;
	row *text;
	int dirty;
	// Terminal State
	struct termios original_termios;
	// Configurables
	int tab_stop;
	int number_line;
	// Statusbar
	char statusmsg[80];
	time_t statusmsg_time;
	int duration;
	// Text Rendering Dimensions
	int textcols;
	int number_line_width;
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
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original_termios) == -1)
		die("DisableRawMode: tcsetatrr");
}

void enableRawMode()
{
	if (tcgetattr(STDIN_FILENO, &E.original_termios) == -1) die("EnableRawMode: tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.original_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("EnableRawMode: tcsetattr");
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

int editorReadKey()
{
	int nread;
	char c;
	void editorRefreshScreen();

	while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
	{
		editorRefreshScreen();
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
					if (seq[1] == '3') return DEL_KEY;
				}
			} else {
				switch (seq[1])
				{
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
				}
			}
		}
		return UNHANDLED_KEY;
	} else {
		return c;
	}
}

int editorRowCxToRx(row *row, int cx)
{
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++)
	{
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
	for (j = 0; j < line->size; j++)
	{
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

void editorInsertRow(int at, char *s, size_t len)
{
	if (at < 0 || at > E.line_count) return;

	E.text = realloc(E.text, sizeof(row) * (E.line_count + 1));
	memmove(&E.text[at + 1], &E.text[at], sizeof(row) * (E.line_count - at));

	E.text[at].size = len;
	E.text[at].chars = malloc(len + 1);
	memcpy(E.text[at].chars, s, len);
	E.text[at].chars[len] = '\0';

	E.text[at].rsize = 0;
	E.text[at].render = NULL;
	editorUpdateRow(&E.text[at]);

	E.line_count++;
	E.dirty++;
}

void editorFreeRow(row *line)
{
	free(line->render);
	free(line->chars);
}

void editorDelRow(int at)
{
  if (at < 0 || at >= E.line_count) return;
  editorFreeRow(&E.text[at]);
  memmove(&E.text[at], &E.text[at + 1], sizeof(row) * (E.line_count - at - 1));
  E.line_count--;

  E.dirty++;
}

void editorRowInsertChar(row *line, int at, int c)
{
	if (at < 0 || at > line->size) at = line->size;
	line->chars = realloc(line->chars, line->size + 2);
	memmove(&line->chars[at + 1], &line->chars[at], line->size - at + 1);
	line->size++;
	line->chars[at] = c;
	editorUpdateRow(line);

	E.dirty++;
}

void editorRowDelChar(row *line, int at)
{
	if (at < 0 || at >= line->size) return;

	memmove(&line->chars[at], &line->chars[at + 1], line->size - at);
	line->size--;
	editorUpdateRow(line);

	E.dirty++;
}

void editorInsertChar(int c)
{
	if (E.cy == E.line_count) {
		editorInsertRow(E.line_count, "", 0);
	}
	editorRowInsertChar(&E.text[E.cy], E.cx, c);
	E.cx++;
}

void editorInsertNewline()
{
	if (E.cx == 0) {
		editorInsertRow(E.cy, "", 0);
	} else {
		row *line = &E.text[E.cy];
		editorInsertRow(E.cy + 1, &line->chars[E.cx], line->size - E.cx);
		line = &E.text[E.cy];
		line->size = E.cx;
		line->chars[line->size] = '\0';
		editorUpdateRow(line);
	}
	E.cy++;
	E.cx = 0;
}

void editorRowAppendString(row *line, char *s, size_t len)
{
  line->chars = realloc(line->chars, line->size + len + 1);
  memcpy(&line->chars[line->size], s, len);
  line->size += len;
  line->chars[line->size] = '\0';
  editorUpdateRow(line);

  E.dirty++;
}

void editorDelChar()
{
	if (E.cy == E.line_count) return;
	if (E.cx == 0 && E.cy == 0) return;

	row *line = &E.text[E.cy];
	if (E.cx > 0) {
		editorRowDelChar(line, E.cx - 1);
		E.cx--;
	} else {
		E.cx = E.text[E.cy - 1].size;
		editorRowAppendString(&E.text[E.cy - 1], line->chars, line->size);
		editorDelRow(E.cy);
		E.cy--;
	}
}

char *editorRowsToString(int *buflen)
{
	int total_len = 0;
	int j;

	for (j = 0; j < E.line_count; j++)
		total_len += E.text[j].size + 1;

	*buflen = total_len;
	char *buf = malloc(total_len);
	char *p = buf;

	for (j = 0; j < E.line_count; j++) {
		memcpy(p, E.text[j].chars, E.text[j].size);
		p += E.text[j].size;
		*p = '\n';
		p++;
	}
	return buf;
}

void editorOpen(char *filename)
{
	free(E.filename);
	E.filename = strdup(filename);

	FILE *fp = fopen(filename, "r");
	if (!fp) die("EditorOpen: fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1)
	{
		while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(E.line_count, line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave()
{
	void editorSetStatusMessage(int duration, const char *fmt, ...);

	if (E.filename == NULL) E.filename = "file.txt";

	int len;
	char *buf = editorRowsToString(&len);

	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				E.dirty = 0;
				editorSetStatusMessage(5, "%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editorSetStatusMessage(5, "can't write to disk! I/O error: %s", strerror(errno));
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
	if (E.rx >= E.coloff + E.textcols) {
		E.coloff = E.rx - E.textcols + 1;
	}
}

void editorDrawNumberLine(buffer *buf, int index)
{
	char n[16];
	snprintf(n, sizeof(n), "%d", index + 1);
	int padding = E.number_line_width - strlen(n) - 1;
	while(padding--)
		bufferAppend(buf, " ", 1);

	bufferAppend(buf, n, strlen(n));
	bufferAppend(buf, "\x1b(0\x78\x1b(B ", 8);
}

void editorDrawRows(buffer *buf)
{
	int y;
	for (y = 0; y < E.screenrows; y++)
	{
		int filerow = y + E.rowoff;
		if (filerow >= E.line_count) {
			if (filerow == 0 && E.number_line)
				editorDrawNumberLine(buf, filerow);
			else
				bufferAppend(buf, "~", 1);
		} else {
			if (E.number_line)
				editorDrawNumberLine(buf, filerow);

			int len = E.text[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.textcols) len = E.textcols;
			bufferAppend(buf, &E.text[filerow].render[E.coloff], len);
		}

		bufferAppend(buf, "\x1b[K", 3);
		bufferAppend(buf, "\r\n", 2);
	}
}

void editorDrawStatusBar(buffer *buf)
{
	bufferAppend(buf, "\x1b[7m", 4);
	char status[80], rstatus[80];

	int len = snprintf(status, sizeof(status), " %.20s%s - %d lines",
		E.filename ? E.filename : "New Buffer",
		E.dirty ? " (modified)" : "",
		E.line_count);
	int rlen = snprintf(rstatus, sizeof(status), "%d/%d ",
		E.cy + 1, E.line_count);

	if (len > E.screencols) len = E.screencols;
	bufferAppend(buf, status, len);

	while (len < E.screencols)
	{
		if (E.screencols - len == rlen) {
			bufferAppend(buf, rstatus, rlen);
			break;
		} else {
			bufferAppend(buf, " ", 1);
			len++;
		}
	}
	bufferAppend(buf, "\x1b[m", 3);
	bufferAppend(buf, "\r\n", 2);
}

void editorDrawMessageBar(buffer *buf)
{
	bufferAppend(buf, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols) msglen = E.screencols;
	if (E.duration == 0)
		bufferAppend(buf, E.statusmsg, msglen);
	else if (msglen && time(NULL) - E.statusmsg_time < E.duration)
		bufferAppend(buf, E.statusmsg, msglen);
}

void editorRefreshScreen()
{
	editorScroll();
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("EditorRefreshScreen: getWindowSize");
	E.screenrows -= 2;
	E.textcols = E.screencols;

	if (E.number_line) {
		E.number_line_width = floor(log10(E.line_count + 1)) + 3;
		if (E.number_line_width < DEFAULT_NL_WIDTH)
			E.number_line_width = DEFAULT_NL_WIDTH;

		E.textcols = E.screencols - E.number_line_width;
	}

	buffer b = BUFFER_INIT;

	bufferAppend(&b, "\x1b[?25l", 6);
	bufferAppend(&b, "\x1b[H", 3);

	editorDrawRows(&b);
	editorDrawStatusBar(&b);
	editorDrawMessageBar(&b);

	char buf[32];
	snprintf(buf, sizeof(buf),"\x1b[%d;%dH",
		E.cy - E.rowoff + 1,
		E.rx - E.coloff + E.number_line_width + 1);
	bufferAppend(&b, buf, strlen(buf));

	bufferAppend(&b, "\x1b[?25h", 6);

	write(STDOUT_FILENO, b.b, b.len);
	free(b.b);
}

void editorSetStatusMessage(int duration, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
	E.duration = duration;
}

void editorClearStatusMessage()
{
	E.statusmsg[0] = '\0';
}

void editorMoveCursor(int key)
{
	row *line = (E.cy >= E.line_count) ? NULL : &E.text[E.cy];

	switch (key)
	{
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			}
			break;
		case ARROW_RIGHT:
			if (line && (E.cx - 0) < line->size) {
				E.cx++;
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

	switch (c)
	{
		case '\r':
			editorInsertNewline();
			break;

		case '\x1b':
		case CTRL_KEY('c'):
			if (E.dirty) {
				editorSetStatusMessage(
					0,
					"WARNING: This buffer has unsaved changes. "
					"Discard changes? (y/N)"
				);
				int reply = editorReadKey();
				switch (reply)
				{
					case 'y':
					case 'Y':
						break;
					case 'n':
					case 'N':
					default:
						editorClearStatusMessage();
						return;
				}
			}
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;

		case CTRL_KEY('w'):
			editorSave();
			break;

		case CTRL_KEY('h'):
		case BACKSPACE:
		case DEL_KEY:
			if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;

		// keys to be ignored
		case CTRL_KEY('l'):
		case CTRL_KEY('g'):
		case UNHANDLED_KEY:
			break;

		default:
			editorInsertChar(c);
	}
}

void editorLoadConfig()
{
	ini_t *conf = ini_load("ctxt.ini");
	if (conf != NULL) {
		ini_sget(conf, NULL, "tabstop", "%d", &E.tab_stop);
		const char *nl = ini_get(conf, NULL, "numberline");

		if (strcmp(nl, "on") == 0)
			E.number_line = 1;
		ini_free(conf);
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
	E.filename = NULL;
	E.dirty = 0;

	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.duration = 0;

	E.tab_stop = DEFAULT_TAB_STOP;
	E.number_line = 0;
	editorLoadConfig();

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("EditorInit: getWindowSize");
	E.screenrows -= 2;
	E.textcols = E.screencols;

	if (E.number_line) {
		E.number_line_width = floor(log10(E.line_count + 1)) + 3;
		if (E.number_line_width < DEFAULT_NL_WIDTH)
			E.number_line_width = DEFAULT_NL_WIDTH;

		E.textcols = E.screencols - E.number_line_width;
	}
}

int main(int argc, char *argv[])
{
	enableRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	editorSetStatusMessage(5, "press ESC to quit | ^W (CTRL+W) to save");

	while (1)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}
