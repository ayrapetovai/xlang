# For clangd language server:
# $ bear -- make -B

CC      ?= cc
CFLAGS  ?= -std=c23 -Wall -Wextra -Werror -O0 -g

SAN     = -fsanitize=address,undefined -fno-omit-frame-pointer
ASANF   = -std=c23 -Wall -Wextra -Werror -Wno-format-overflow -O1 -g $(SAN)

HDR     = ast.h tokens.h reader.h lexer.h logger.h parser.h exec.h
SRC     = main.c ast.c lexer.c logger.c parser.c tokens.c exec.c reader.c 

bootstrap: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

bootstrap-asan: $(SRC) $(HDR)
	$(CC) $(ASANF) -o $@ $(SRC) -lm

clean:
	rm -f bootstrap bootstrap-asan
	rm -rf test/.work

tags: $(SRC) $(HDR)
	ctags -R --languages=C --exclude=.git --exclude=build -f tags .

check: bootstrap
	sh test/check.sh

check-asan: bootstrap-asan
	BIN="$(CURDIR)/bootstrap-asan" sh test/check.sh

check-vs: bootstrap
	@ref="$(REF)"; \
	trap 'rm -rf test/.work' EXIT; \
	if [ -z "$$ref" ]; then echo "usage: make check-vs REF=<ref>" >&2; exit 2; fi; \
	rm -rf test/.work/ref && mkdir -p test/.work/ref && \
	git archive "$$ref" | tar -x -C test/.work/ref && \
	$(CC) $(CFLAGS) -o test/.work/ref/bootstrap \
	  test/.work/ref/main.c test/.work/ref/ast.c test/.work/ref/lexer.c \
	  test/.work/ref/logger.c test/.work/ref/parser.c \
	  test/.work/ref/tokens.c test/.work/ref/exec.c \
	  test/.work/ref/reader.c -lm || exit 2; \
	if ! printf 'echo 1\n' | test/.work/ref/bootstrap --ast - 2>/dev/null | grep -q '^block'; then \
	  echo "check-vs: $$ref has no --ast flag CLI and cannot run this suite" >&2; \
	  exit 2; \
	fi; \
	echo "== ref: $$ref =="; \
	BIN="$(CURDIR)/test/.work/ref/bootstrap" sh test/check.sh; a=$$?; \
	echo; echo "== working tree =="; \
	sh test/check.sh; b=$$?; \
	echo; \
	sh test/bench.sh test/.work/ref/bootstrap bootstrap "$$ref" "working tree"; c=$$?; \
	[ $$a -eq 0 ] && [ $$b -eq 0 ] && [ $$c -eq 0 ]

.PHONY: clean tags check check-asan check-vs
