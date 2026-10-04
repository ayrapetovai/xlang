#!/bin/sh
# Regression harness for the bootstrap compiler. Run via `make check`.
#
#   sh check.sh                quick pass: 600 corpus inputs + all cases
#   FULL=1 sh check.sh         whole corpus (11664 inputs)
#   VERBOSE=1 sh check.sh      also print the output of every mismatching input
#   WRITE_GOLDEN=1 sh check.sh rewrite golden.sha256 instead of checking it
#
# These are environment variables, not arguments: `sh check.sh FULL=1` passes
# "FULL=1" as $1 and quietly runs the quick pass.
#
# Three independent layers:
#
#   1. cases       hand-written canaries checked against expect.txt
#   2. corpus      generated corpus; per-file sha256 of `ast` and `lex` output
#                  compared against golden.sha256
#   3. invariants  every legal split of an expression must produce the same AST
#                  as the joined form -- no stored baseline, so this layer keeps
#                  testing the multi-line rule as the grammar grows
#
# POSIX sh on purpose: zsh does not word-split unquoted expansions, which
# silently collapses `for s in $splits` into one iteration.

set -u

cd "$(dirname "$0")" || exit 1

BIN=../bootstrap
SNIPPETS=snippets
WORK=.work
EXPECT=expect.txt
GOLDEN=golden.sha256

FULL=${FULL:-0}
VERBOSE=${VERBOSE:-0}
# not $GOLDEN: that name is taken by the golden file's path below
WRITE_GOLDEN=${WRITE_GOLDEN:-0}

# n=0 means "the whole enumeration" to gen.awk
if [ "$FULL" = "1" ]; then CORPUS_N=0; else CORPUS_N=600; fi

if [ ! -x "$BIN" ]; then
  echo "check.sh: $BIN is not built -- run 'make bootstrap' first" >&2
  exit 1
fi

# A log line starts with an ISO timestamp: "2026-10-05T01:09:09+0300 DEBUG ...".
# see LOG_LINE below.
LOG_LINE='^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]'

# run <mode> <input> <output-file>
#
# The output file starts with the exit status so that the golden records
# accept-vs-reject as well as the text: a grammar change that turns a rejected
# input into an accepted one shows up as a golden diff, not just as different
# words. A non-zero status is a legitimate result here -- the corpus contains
# deliberately invalid inputs -- so it is recorded, never flagged.
#
# Log lines are stripped. They carry a wall-clock timestamp and the source line
# of the LOG_* call, so they differ between two runs of the *same* binary and
# between two builds of the *same* behaviour -- a golden over them could never
# match. The AST dump is the contract under test, not the tracing. Which level
# main.c sets is the developer's business, so this must hold at DEBUG too.
#
# </dev/null keeps the compiler from eating our own stdin, and any output on
# stderr is a failure: the compiler reports everything on stdout.
#
# run cannot return the status directly -- its last command is `rm` -- so the
# status is left in RUN_RC for the caller.
RUN_RC=0
run() {
  RUN_RC=0
  "$BIN" "$1" "$2" > "$3.body" 2> "$3.err" </dev/null || RUN_RC=$?
  if [ -s "$3.err" ]; then
    note_fail "$1 $2 wrote to stderr: $(head -n 1 "$3.err")"
  fi
  printf 'rc=%s\n' "$RUN_RC" > "$3"
  grep -avE "$LOG_LINE" "$3.body" >> "$3" || true
  rm -f "$3.body" "$3.err"
}

fails=0
note_fail() { fails=$((fails + 1)); echo "  FAIL $*"; }

rm -rf "$WORK"
mkdir -p "$WORK/corpus" "$WORK/pairs" "$WORK/out/ast/corpus" "$WORK/out/ast/pairs" \
         "$WORK/out/lex/corpus" "$WORK/out/lex/pairs"

# ---------------------------------------------------------------- 1. cases
echo "== cases =="
n_cases=0
n_missing=0
# The must-contain field is the rest of the line, so it may contain spaces.
while read -r file want_rc want_stmts want_sub; do
  case "$file" in ''|\#*) continue ;; esac

  if [ ! -f "$file" ]; then
    n_missing=$((n_missing + 1))
    note_fail "$file listed in $EXPECT but missing"
    continue
  fi

  n_cases=$((n_cases + 1))
  name=$(basename "$file" .x)
  run ast "$file" "$WORK/case.out"
  got_rc=$RUN_RC

  if [ "$got_rc" != "$want_rc" ]; then
    note_fail "$name: rc=$got_rc want=$want_rc"
    continue
  fi

  if [ "$want_sub" != "-" ] && ! grep -aqF "$want_sub" "$WORK/case.out"; then
    note_fail "$name: output does not contain '$want_sub'"
    continue
  fi

  got_stmts=$(grep -ac 'stms' "$WORK/case.out")
  if [ "$got_stmts" -lt "$want_stmts" ]; then
    note_fail "$name: $got_stmts statements, want >= $want_stmts"
  fi
done < "$EXPECT"
echo "  $n_cases cases, $n_missing missing"

# A truncated or mis-parsed expect.txt would otherwise pass by testing nothing.
if [ "$n_cases" -lt 50 ]; then
  note_fail "only $n_cases cases were checked -- is $EXPECT intact?"
fi

# ------------------------------------------------------- 2. generated corpus
echo "== corpus =="
awk -v out="$WORK/corpus" -v snippets="$SNIPPETS" -v mode=corpus \
    -v n="$CORPUS_N" -f gen.awk || exit 1
awk -v out="$WORK/pairs" -v snippets="$SNIPPETS" -v mode=pairs \
    -f gen.awk || exit 1

n_inputs=0
for dir in corpus pairs; do
  for f in "$WORK/$dir"/*.x; do
    [ -e "$f" ] || continue
    base="$dir/$(basename "$f")"
    for mode in ast lex; do
      run "$mode" "$f" "$WORK/out/$mode/$base"
    done
    n_inputs=$((n_inputs + 1))
  done
done
echo "  $n_inputs inputs x 2 modes"

# How many the compiler accepts, so a corpus that silently degenerated into
# all-valid or all-invalid shows up in the log instead of passing quietly.
n_ok=$(grep -l '^rc=0$' "$WORK/out/ast"/*/* 2>/dev/null | wc -l)
echo "  $n_ok of $n_inputs accepted, $((n_inputs - n_ok)) rejected"

# The golden covers the whole corpus, but a quick run generates only part of it,
# so restrict the check to the inputs this run actually produced.
: > "$WORK/wanted.txt"
while read -r gen; do
  echo "ast/corpus/$gen" >> "$WORK/wanted.txt"
  echo "lex/corpus/$gen" >> "$WORK/wanted.txt"
  echo "ast/pairs/$gen" >> "$WORK/wanted.txt"
  echo "lex/pairs/$gen" >> "$WORK/wanted.txt"
done < "$WORK/manifest.txt"

( cd "$WORK/out" && find . -type f | sed 's|^\./||' | LC_ALL=C sort \
    | xargs sha256sum ) > "$WORK/golden.new" || exit 1

if [ "$WRITE_GOLDEN" = "1" ]; then
  cp "$WORK/golden.new" "$GOLDEN"
  echo "  wrote $GOLDEN ($(wc -l < "$GOLDEN") entries)"
else
  if [ ! -f "$GOLDEN" ]; then
    note_fail "$GOLDEN missing -- run 'make golden' to create it"
  else
    grep -Ff "$WORK/wanted.txt" "$GOLDEN" > "$WORK/golden.want" || true
    n_want=$(wc -l < "$WORK/golden.want")
    if [ "$n_want" -eq 0 ]; then
      note_fail "no entry in $GOLDEN matches the inputs this run generated"
      note_fail "the corpus moved -- run 'make golden' and commit the result"
    fi

    ( cd "$WORK/out" && sha256sum -c "$WORK/golden.want" ) > "$WORK/sha.log" 2>&1
    n_bad=$(grep -c 'FAILED' "$WORK/sha.log")
    if [ "$n_bad" -gt 0 ]; then
      note_fail "$n_bad of $n_want outputs differ from $GOLDEN"
      grep 'FAILED' "$WORK/sha.log" | sed 's/^/    /' | head -20
      if [ "$VERBOSE" = "1" ]; then
        grep 'FAILED' "$WORK/sha.log" | awk '{print $2}' | while read -r p; do
          echo "    --- output $p"
          sed 's/^/      /' "$WORK/out/$p"
          echo "    --- input ${p#*/}"
          sed 's/^/      /' "$WORK/${p#*/}"
        done
      fi
    else
      echo "  all $n_want outputs match $GOLDEN"
    fi
  fi
fi

# ------------------------------------------------------------ 3. invariants
echo "== invariants: every split must equal its joined form =="
n_joined=0
n_splits=0
n_bad_splits=0
while read -r joined splits; do
  [ -n "$joined" ] || continue
  n_joined=$((n_joined + 1))
  run ast "$WORK/pairs/$joined" "$WORK/joined.out"

  for s in $splits; do
    n_splits=$((n_splits + 1))
    run ast "$WORK/pairs/$s" "$WORK/split.out"
    if ! cmp -s "$WORK/joined.out" "$WORK/split.out"; then
      n_bad_splits=$((n_bad_splits + 1))
      if [ "$n_bad_splits" -le 10 ]; then
        echo "  FAIL split $s differs from $joined"
        fails=$((fails + 1))
      fi
    fi
  done
done < "$WORK/pairs.manifest"
echo "  $n_splits splits across $n_joined expressions"

if [ "$n_joined" -lt 2 ] || [ "$n_splits" -lt 20 ]; then
  note_fail "only $n_splits splits across $n_joined expressions -- did gen.awk run?"
fi

# ------------------------------------------------------------------ verdict
echo
if [ "$fails" -eq 0 ]; then
  echo "PASS"
  exit 0
fi
echo "FAIL ($fails)"
exit 1
