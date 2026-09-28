CC      ?= cc
CFLAGS  ?= -std=c23 -Wall -Wextra -Werror -O0 -g

bootstrap: main.c lexer.c reader.c parser.c tokens.c exec.c tokens.h reader.h lexer.h parser.h exec.h
	$(CC) $(CFLAGS) -o $@ main.c lexer.c parser.c tokens.c exec.c reader.c -lm

clean:
	rm -f bootstrap

tags: main.c lexer.c reader.c parser.c tokens.c exec.c tokens.h reader.h lexer.h parser.h exec.h
	ctags -R --languages=C --exclude=.git --exclude=build -f tags .

.PHONY: clean
