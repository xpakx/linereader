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

char *readline(const char *prompt) {
	int buflen = 128;
	int len = 0;
	char *buffer = malloc(buflen);
	if (!buffer) {
		return NULL;
	}
	buffer[0] = '\0';

	write(STDOUT_FILENO, prompt, strlen(prompt));

	char c;
	while (read(STDOUT_FILENO, &c, 1) == 1) {
		if (c == '\n' || c == '\r') {
			// enter
			break;
		} else if (c >= 32 && c < 127) {
			if (len >= buflen - 1) {
				// TODO: grow buffer
				return buffer;
			}
			buffer[len++] = c;
			buffer[len] = '\0';
			write(STDOUT_FILENO, &c, 1);
		}
	}


	return buffer;
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
