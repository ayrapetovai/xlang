CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -Werror -O0 -g

bootstrap: main.c lexer.c tokens.c parser.c tokens.h lexer.h parser.h
	$(CC) $(CFLAGS) -o $@ main.c lexer.c tokens.c parser.c

clean:
	rm -f bootstrap

.PHONY: clean