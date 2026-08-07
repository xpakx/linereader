#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>

struct termios orig_termios;

void disable_raw_mode() {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disable_raw_mode); // restore settings on exit
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_iflag &= ~(IXON | ICRNL);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

#define INIT_BUF_SIZE 128

typedef struct {
	char *buffer;
	int buflen;
	int len;
	int cursor;

	int repl;
	int multibyte;  // temporary flag, bc some operations are bugged after introducing multibyte utf-8

	char multibuffer[4];
	char multilen;
	char multipred;
	int viscursor;
	int vislen;
} EditorState;

int grow_buffer(EditorState *state) {
	if (state->len >= state->buflen - 1) {
		state->buflen *= 2;
		char *new_buffer = realloc(state->buffer, state->buflen);
		if (!new_buffer) {
			free(state->buffer);
			return 0;
		}
		state->buffer = new_buffer;
	}
	return 1;
}

int append_end(EditorState *state, char c) {
	state->buffer[state->len++] = c;
	state->buffer[state->len] = '\0';
	state->cursor++;
	return 1;
}

int append_middle(EditorState *state, char c) {
	for(int i=state->len; i>state->cursor; i--) {
		state->buffer[i] = state->buffer[i-1];
	}
	state->buffer[state->cursor++] = c;
	state->len++;
	state->buffer[state->len] = '\0';
	return 1;
}

int append(EditorState *state, char c) {
	if (!grow_buffer(state)) return 0;
	if (state->cursor == state->len) {
		return append_end(state, c);
	}
	return append_middle(state, c);
}

void append_visual(EditorState *state, char c) {
	if (c >= 32 && c < 127) {
		write(STDOUT_FILENO, &c, 1);
		state->viscursor++;
		state->vislen++;
	}
	if ((c & 0xE0) == 0xC0) {
		state->multipred = 2;
		state->multilen = 0;
		state->multibuffer[state->multilen++] = c;
		return;
	} else if ((c & 0xF0) == 0xE0) {
		state->multipred = 3;
		state->multilen = 0;
		state->multibuffer[state->multilen++] = c;
		return;
	} else if ((c & 0xF8) == 0xF0) {
		state->multipred = 4;
		state->multilen = 0;
		state->multibuffer[state->multilen++] = c;
		return;
	} else if ((c & 0xC0) == 0x80) {
		if (!state->multipred) return;
		state->multibuffer[state->multilen++] = c;
		if (state->multilen == state->multipred) {
			write(STDOUT_FILENO, state->multibuffer, state->multilen);
			state->multipred = 0;
			// TODO: some 3-byte chars are also 2 col wide
			// this also ignores zero-width chars, etc
			int width = (state->multilen == 4) ? 2 : 1;
			state->viscursor += width;
			state->vislen += width;
		} else {
			return;
		}
	}
	if (state->cursor != state->len) {
		write(STDOUT_FILENO, &state->buffer[state->cursor], state->len - state->cursor);
		char jumpbuf[16];
		int len = snprintf(jumpbuf, sizeof(jumpbuf), "\033[%dD", state->vislen-state->viscursor);
		write(STDOUT_FILENO, jumpbuf, len);
	}
}

void move_left(EditorState *state) {
	if (state->cursor <= 0) return;
	state->cursor--;

	while (state->cursor > 0 && (state->buffer[state->cursor] & 0xC0) == 0x80) {
		state->cursor--;
	}
}

void move_left_visual(EditorState *state) {
	if (state->viscursor <= 0) return;
	
	// TODO: some 3-byte chars are also 2 col wide
	// this also ignores zero-width chars, etc
	if ((state->buffer[state->cursor] & 0xF8) == 0xF0) {
		state->viscursor -= 2;
		write(STDOUT_FILENO, "\b\b", 2);
		return;
	}
	state->viscursor--;
	write(STDOUT_FILENO, "\b", 1);
}

void move_right(EditorState *state) {
	if (state->cursor >= state->len) return;
	state->cursor++;

	while (state->cursor < state->len && (state->buffer[state->cursor] & 0xC0) == 0x80) {
		state->cursor++;
	}
}

void move_right_visual(EditorState *state) {
	if (state->viscursor >= state->vislen) return;
	
	// TODO: some 3-byte chars are also 2 col wide
	// this also ignores zero-width chars, etc
	if ((state->buffer[state->cursor] & 0xF8) == 0xF0) {
		state->viscursor += 2;
		write(STDOUT_FILENO, "\033[2C", 4);
		return;
	}
	state->viscursor++;
	write(STDOUT_FILENO, "\033[C", 3);
}


int backspace_end(EditorState *state) {
	if (state->len <= 0) return 0;
	state->len--;
	state->cursor--;

	while (state->cursor > 0 && (state->buffer[state->cursor] & 0xC0) == 0x80) {
		state->cursor--;
		state->len--;
	}

	state->buffer[state->len] = '\0';
	return 1;
}

int backspace_middle(EditorState *state) {
	if (state->cursor == 0) return 0;
	int len = 1;
	state->len--;
	state->cursor--;
	while (state->cursor > 0 && (state->buffer[state->cursor] & 0xC0) == 0x80) {
		state->cursor--;
		state->len--;
		len++;
	}
	for(int i=state->cursor; i<state->len; i++) {
		state->buffer[i] = state->buffer[i+len];
	}
	state->buffer[state->len] = '\0';
	return 2;
}

int backspace(EditorState *state) {
	if (state->cursor == state->len) {
		return backspace_end(state);
	}
	return backspace_middle(state);
}

char *readline(const char *prompt) {
	EditorState state;
	state.buflen = INIT_BUF_SIZE;
	state.len = 0;
	state.cursor = 0;
	state.vislen = 0;
	state.viscursor = 0;
	state.buffer = malloc(state.buflen);
	state.repl = 1;
	state.multibyte = 1;
	if (!state.buffer) {
		return NULL;
	}
	state.buffer[0] = '\0';

	write(STDOUT_FILENO, prompt, strlen(prompt));

	char c;
	while (read(STDIN_FILENO, &c, 1) == 1) {
		if (c == '\n' || c == '\r') {
			// enter
			break;
		} else if (c == 127 || c == 8) {
			// TODO: multibyte (2-col chars)
			int res = backspace(&state);
			if (res == 1) write(STDOUT_FILENO, "\b \b", 3);
			else if (res == 2) {
				write(STDOUT_FILENO, "\b", 1);
				write(STDOUT_FILENO, &state.buffer[state.cursor], state.len - state.cursor);
				write(STDOUT_FILENO, " ", 1);
				char jumpbuf[16];
				int len = snprintf(jumpbuf, sizeof(jumpbuf), "\033[%dD", state.vislen-state.viscursor+1);
				write(STDOUT_FILENO, jumpbuf, len);
			}
		} else if (c == '\033') {
			struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
			if (poll(&pfd, 1, 50) <= 0) {
				continue; 
			}

			char seq[3];
			if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
			if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;

			if (seq[0] == '[') {
				if (seq[1] == 'D') {
					// Left arrow
					move_left(&state);
					move_left_visual(&state);
				} else if (seq[1] == 'C') {
					// Right arrow
					move_right(&state);
					move_right_visual(&state);
				} else if (seq[1] == 'A') {
					// Up arrow (History prev)
					// TODO
				} else if (seq[1] == 'B') {
					// Down arrow (History next)
					// TODO
				}
			}

		} else if (c >= 32 && c < 127 || (state.multibyte && (unsigned char)c >= 128)) {
			if(!append(&state, c))  {
				return NULL;
			}
			append_visual(&state, c);
		} else if (c == 3 && state.repl) { // ctrl+c
			// this shouldn't be active for shell,
			// but is useful for userspace apps
			free(state.buffer);
			exit(0);
		} else if (c == 4) {
			if (state.len == 0) { // ctrl+d
				free(state.buffer);
				return NULL;
			}
		} else if (c == 26 && state.repl) { // ctrl+z
			// this shouldn't be active for shell,
			// but is useful for userspace apps
			disable_raw_mode();
			signal(SIGTSTP, SIG_DFL);
			// raise(SIGTSTP); ???
			kill(0, SIGTSTP);

			// --- waitin for user's `fg` ---
			enable_raw_mode();
			write(STDOUT_FILENO, "\n", 1);
			write(STDOUT_FILENO, prompt, strlen(prompt));
			if (state.len > 0) {
				write(STDOUT_FILENO, state.buffer, state.len);
			}

		} else if (c == 1) { // ctrl+a
			int jump = state.viscursor;
			if (!jump) continue;
			state.cursor = 0;
			state.viscursor = 0;

			char jumpbuf[16];
			int len = snprintf(jumpbuf, sizeof(jumpbuf), "\033[%dD", jump);
			write(STDOUT_FILENO, jumpbuf, len);
		} else if (c == 5) { // ctrl+e
			int jump = state.vislen - state.viscursor;
			if (!jump) continue;
			state.cursor = state.len;
			state.viscursor = state.vislen;

			char jumpbuf[16];
			int len = snprintf(jumpbuf, sizeof(jumpbuf), "\033[%dC", jump);
			write(STDOUT_FILENO, jumpbuf, len);
		} else if (c == 11) { // ctrl+k
			state.buffer[state.cursor] = '\0';
			state.len = state.cursor;
			state.vislen = state.viscursor;
			write(STDOUT_FILENO, "\033[K", 3);
		} else if (c == 21) { // ctrl+u
			int jump = state.viscursor;
			state.buffer[0] = '\0';
			state.len = 0;
			state.cursor = 0;
			state.vislen = 0;
			state.viscursor = 0;

			if (jump) {
				char jumpbuf[16];
				int len = snprintf(jumpbuf, sizeof(jumpbuf), "\033[%dD", jump);
				write(STDOUT_FILENO, jumpbuf, len);
			}
			write(STDOUT_FILENO, "\033[K", 3);
		} else if (c == 23) { // ctrl+w
		    	// TODO
		}
	}

	return state.buffer;
}

int main() {
	enable_raw_mode();
	char *line = readline("prompt> ");

	if (line) {
		printf("\n\n%s\n", line);
		free(line);
	}
	return 0;
}
