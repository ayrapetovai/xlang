CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -Werror -O0 -g

bootstrap: main.c lexer.c parser.c exec.c tokens.h lexer.h parser.h exec.h
	$(CC) $(CFLAGS) -o $@ main.c lexer.c parser.c exec.c -lm

clean:
	rm -f bootstrap

.PHONY: clean
