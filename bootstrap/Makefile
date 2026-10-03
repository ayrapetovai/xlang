CC      ?= cc
CFLAGS  ?= -std=c23 -Wall -Wextra -Werror -O0 -g

bootstrap: main.c ast.c lexer.c logger.c reader.c parser.c tokens.c exec.c ast.h tokens.h reader.h lexer.h logger.h parser.h exec.h
	$(CC) $(CFLAGS) -o $@ main.c ast.c lexer.c logger.c parser.c tokens.c exec.c reader.c -lm

clean:
	rm -f bootstrap

tags: main.c ast.c lexer.c logger.c reader.c parser.c tokens.c exec.c ast.h tokens.h reader.h lexer.h logger.h parser.h exec.h
	ctags -R --languages=C --exclude=.git --exclude=build -f tags .

.PHONY: clean
