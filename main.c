#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
} EditorState;

char *readline(const char *prompt) {
	EditorState state;
	state.buflen = INIT_BUF_SIZE;
	state.len = 0;
	state.buffer = malloc(state.buflen);
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
		} else if (c >= 32 && c < 127) {
			if (state.len >= state.buflen - 1) {
				// TODO: grow buffer
				return state.buffer;
			}
			state.buffer[state.len++] = c;
			state.buffer[state.len] = '\0';
			write(STDOUT_FILENO, &c, 1);
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
