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

# Same sources under ASan+UBSan. test/check.sh reads ../bootstrap, so a
# sanitizer run has to put the binary there and put it back afterwards.
bootstrap-asan: $(SRC) $(HDR)
	$(CC) $(ASANF) -o $@ $(SRC) -lm

clean:
	rm -f bootstrap bootstrap-asan
	rm -rf test/.work

tags: $(SRC) $(HDR)
	ctags -R --languages=C --exclude=.git --exclude=build -f tags .

# ---------------------------------------------------------------- tests
# test/check.sh has three layers: hand-written canaries from test/expect.txt,
# a generated corpus compared against test/golden.sha256, and an invariant
# that every legal line-split of an expression yields the joined form's AST.
#
#   make check        600 corpus inputs          ~11s
#   make check-full   the whole corpus, 25106    ~3.5min
#   make check-asan   the quick pass under ASan+UBSan
#   make check-vs REF compare HEAD (or any ref) against the working tree
#   make golden       rewrite golden.sha256 -- only when a change is intended

check: bootstrap
	sh test/check.sh

check-full: bootstrap
	FULL=1 sh test/check.sh

check-asan: bootstrap-asan
	@cp bootstrap test/.bootstrap.saved 2>/dev/null || true
	@cp bootstrap-asan bootstrap
	@sh test/check.sh; rc=$$?; \
	  if [ -f test/.bootstrap.saved ]; then \
	    mv test/.bootstrap.saved bootstrap; \
	  else \
	    rm -f bootstrap; \
	  fi; \
	  exit $$rc

# Time the same corpus on REF and on the working tree. REF sources come from
# git archive, but main.c is taken from the working tree on purpose: it only
# chooses the log level, and a different level produces a phantom speedup.
check-vs: bootstrap
	@ref="$(REF)"; \
	if [ -z "$$ref" ]; then echo "usage: make check-vs REF=<ref>" >&2; exit 2; fi; \
	rm -rf test/.work/ref && mkdir -p test/.work/ref && \
	git archive "$$ref" | tar -x -C test/.work/ref && \
	cp main.c test/.work/ref/main.c && \
	$(CC) $(CFLAGS) -o test/.work/ref/bootstrap \
	  test/.work/ref/main.c test/.work/ref/ast.c test/.work/ref/lexer.c \
	  test/.work/ref/logger.c test/.work/ref/parser.c \
	  test/.work/ref/tokens.c test/.work/ref/exec.c \
	  test/.work/ref/reader.c -lm && \
	echo "built $$ref -> test/.work/ref/bootstrap" && \
	sh test/bench.sh "$(CURDIR)/test/.work/ref/bootstrap" \
	                "$(CURDIR)/bootstrap" "$$ref" "working tree"

golden: bootstrap
	FULL=1 WRITE_GOLDEN=1 sh test/check.sh

.PHONY: clean tags check check-full check-asan check-vs golden
