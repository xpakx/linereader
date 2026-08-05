#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

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
	int repl;
} EditorState;

int append_end(EditorState *state, char c) {
	if (state->len >= state->buflen - 1) {
		state->buflen *= 2;
		char *new_buffer = realloc(state->buffer, state->buflen);
		if (!new_buffer) {
			free(state->buffer);
			return 0;
		}
		state->buffer = new_buffer;
	}

	state->buffer[state->len++] = c;
	state->buffer[state->len] = '\0';
	return 1;
}

int backspace(EditorState *state) {
	if (state->len > 0) {
		state->len--;
		state->buffer[state->len] = '\0';
		return 1;
	}
	return 0;
}


char *readline(const char *prompt) {
	EditorState state;
	state.buflen = INIT_BUF_SIZE;
	state.len = 0;
	state.buffer = malloc(state.buflen);
	state.repl = 1;
	if (!state.buffer) {
		return NULL;
	}
	state.buffer[0] = '\0';

	write(STDOUT_FILENO, prompt, strlen(prompt));

	char c;
	while (read(STDOUT_FILENO, &c, 1) == 1) {
		if (c == '\n' || c == '\r') {
			// enter
			break;
		} else if (c == 127 || c == 8) {
			if (backspace(&state)) write(STDOUT_FILENO, "\b \b", 3);
		} else if (c == '\033') {
			char seq[3];
			if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
			if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;
		} else if (c >= 32 && c < 127) {
			if (!append_end(&state, c)) {
				free(state.buffer);
				return NULL;
			}

			write(STDOUT_FILENO, &c, 1);
		} else if (c == 3 && state.repl) { // ctrl+c
			// this shouldn't be active for shell,
			// but is useful for userspace apps
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
		    	// TODO
		} else if (c == 5) { // ctrl+e
		    	// TODO
		} else if (c == 11) { // ctrl+k
		    	// TODO
		} else if (c == 21) { // ctrl+u
		    	// TODO
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
