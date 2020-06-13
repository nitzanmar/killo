/* includes */
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/* defines */
#define KILLO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

#define ESCAPE_CHAR '\x1b'

enum EditorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN
};

/* terminal */
void die(const char *err_msg);
void disable_raw_mode(void);
void enable_raw_mode(void);
int editor_read_key(void);
int get_window_size(int *rows, int *cols);

/* append buffer */
struct AppendBuffer {
    char *buf;
    int len;
};

void ab_append(struct AppendBuffer *ab, const char *s, int len);
void ab_init(struct AppendBuffer *ab);
void ab_destroy(struct AppendBuffer *ab);

/* input */
void editor_process_keypress(void);
void editor_move_cursor(int key);

/* output */
void clear_screen(void);
void reposition_cursor(void);
void editor_draw_rows(struct AppendBuffer *ab);
void editor_refresh_screen(void);

/* data */
struct EditorConfig {
    struct termios orig_termios;
    int screen_rows;
    int screen_cols;
    int c_x;
    int c_y;
};

struct EditorConfig global_config;

/* init */

void init_editor(void);

int main(void)
{
    enable_raw_mode();
    init_editor();

    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }
    
    return EXIT_SUCCESS;
}

/* terminal */
void die(const char *err_msg)
{
    clear_screen();
    reposition_cursor();
    perror(err_msg);
    exit(EXIT_FAILURE);
}

void disable_raw_mode(void)
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &global_config.orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enable_raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &global_config.orig_termios) == -1) {
        die("tcgetattr");
    }

    atexit(disable_raw_mode);

    struct termios raw = global_config.orig_termios;
    raw.c_iflag &= ~((tcflag_t) (BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    raw.c_oflag &= ~((tcflag_t) OPOST);
    raw.c_cflag |= ((tcflag_t) CS8);
    raw.c_lflag &= ~((tcflag_t) (ECHO | ICANON | IEXTEN | ISIG));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        die("tcsetattr");
    }
}

int editor_read_key(void)
{
    int num_read;
    char c;

    while ((num_read = read(STDIN_FILENO, &c, 1) == 1) != 1) {
        if (num_read == -1 && errno != EAGAIN) {
            die("read");
        }
    }

    if (c == ESCAPE_CHAR) {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return ESCAPE_CHAR;
        }

        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return ESCAPE_CHAR;
        }

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return ESCAPE_CHAR;
                }

                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                
                }
            }
        }

        return ESCAPE_CHAR;
    }

    return c;
}

int _get_win_size_by_cursor(int *rows, int *cols)
{
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
        return -1;
    }

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    char buf[32];
    size_t i = 0;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1 || buf[i] == 'R') {
            break;
        }

        i++;
    }

    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') {
        return -1;
    }

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }

    return 0;
}

int _get_win_size_by_ioctl(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
        return -1;
    }

    *rows = ws.ws_row;
    *cols = ws.ws_col;

    return 0;
}

int get_window_size(int *rows, int *cols)
{
    int res;

    res = _get_win_size_by_ioctl(rows, cols);

    if (res != 0) {
        res = _get_win_size_by_cursor(rows, cols);
    }

    return res;
}

/* append buffer */
void ab_init(struct AppendBuffer *ab)
{
    ab->buf = NULL;
    ab->len = 0;
}

void ab_destroy(struct AppendBuffer *ab)
{
    if (ab->buf) {
        free(ab->buf);
    }
}

void ab_append(struct AppendBuffer *ab, const char *s, int len)
{
    char *new_buf = realloc(ab->buf, (size_t) (ab->len + len));

    if (new_buf == NULL) {
        return;
    }

    memcpy(&new_buf[ab->len], s, len);
    ab->buf = new_buf;
    ab->len += len;
}

/* input */
void editor_process_keypress(void)
{
    int c = editor_read_key();

    switch(c) {
        case CTRL_KEY('q'):
            clear_screen();
            reposition_cursor();
            exit(EXIT_SUCCESS);
            break;
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
        case ARROW_DOWN:
            editor_move_cursor(c);
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            for (int i = 0; i < global_config.screen_rows; ++i) {
                editor_move_cursor((c == PAGE_UP ? ARROW_UP : ARROW_DOWN));
            }
            break;
    }
}

void editor_move_cursor(int key)
{
    switch(key) {
        case ARROW_LEFT:
            if (global_config.c_x > 0) {
                global_config.c_x--;
            }
            break;
        case ARROW_RIGHT:
            if (global_config.c_x < global_config.screen_cols - 1) {
                global_config.c_x++;
            }
            break;
        case ARROW_UP:
            if (global_config.c_y > 0) {
                global_config.c_y--;
            }
            break;
        case ARROW_DOWN:
            if (global_config.c_y < global_config.screen_rows - 1) {
                global_config.c_y++;
            }
            break;
    }
}

/* output */
void clear_screen(void)
{
    write(STDIN_FILENO, "\x1b[2j", 4);
}

void reposition_cursor(void)
{
    write(STDIN_FILENO, "\x1b[H", 3);
}

void editor_draw_rows(struct AppendBuffer *ab)
{
    for (int i = 0; i < global_config.screen_rows; i++) {
        if (i == global_config.screen_rows / 3) {
            char welcome[80];
            int welcome_len = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILLO_VERSION);

            if (welcome_len > global_config.screen_cols) {
                welcome_len = global_config.screen_cols;
            }

            int padding = (global_config.screen_cols - welcome_len) / 2;

            if (padding) {
                ab_append(ab, "~", 1);
                padding--;
            }

            while (padding) {
                ab_append(ab, " ", 1);
                padding--;
            }

            ab_append(ab, welcome, welcome_len);
        } else {
            ab_append(ab, "~", 1);
        }

        ab_append(ab, "\x1b[K", 3);

        if (i < global_config.screen_rows - 1) {
            ab_append(ab, "\r\n", 2);
        }
    }
}

void editor_refresh_screen(void)
{
    struct AppendBuffer ab;
    ab_init(&ab);

    ab_append(&ab, "\x1b[?25l", 6);
    ab_append(&ab, "\x1b[H", 3);

    clear_screen();
    reposition_cursor();

    editor_draw_rows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", global_config.c_y + 1, global_config.c_x + 1);
    ab_append(&ab, buf, strlen(buf));

    ab_append(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.buf, (size_t) ab.len);
    ab_destroy(&ab);
}

/* init */
void init_editor(void)
{
    if (get_window_size(&global_config.screen_rows, &global_config.screen_cols) == -1) {
        die("get_window_size");
    }

    global_config.c_x = 0;
    global_config.c_y = 0;
}

