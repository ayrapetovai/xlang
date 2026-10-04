# For clangd language server:
# $ bear -- make -B

CC      ?= cc
CFLAGS  ?= -std=c23 -Wall -Wextra -Werror -O0 -g #-fsanitize=address,undefined,bounds

HDR     = ast.h tokens.h reader.h lexer.h logger.h parser.h exec.h
SRC     = main.c ast.c lexer.c logger.c parser.c tokens.c exec.c reader.c 

bootstrap: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

clean:
	rm -f bootstrap

tags: $(SRC) $(HDR)
	ctags -R --languages=C --exclude=.git --exclude=build -f tags .

.PHONY: clean
