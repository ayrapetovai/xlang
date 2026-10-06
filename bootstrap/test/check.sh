#!/bin/sh
# Regression harness for the bootstrap compiler. Run via `make check`.
#
#   sh check.sh          cases + stdin + properties
#   VERBOSE=1            also print the actual block of every failing case
#
# Five layers, and none of them reads a stored digest of compiler output:
#
#   1. cases        every cases/*.x against expect.txt -- rc, exact stdout,
#                   exact stderr. This is the only layer that says what the
#                   compiler *should* do, so it is the only one a human reads.
#   2. stdin        the same case fed via '-' must match the same case fed via
#                   its filename, byte for byte on both channels and in rc.
#   3. properties   assertions that need no stored answer: determinism,
#                   comments do not change meaning, a newline where a space
#                   was does not change meaning, statuses are legal, and error
#                   positions point into the source.
#   4. snippets     the hand-written material in snippets/ checked directly:
#                   every statement stands alone, every expression parses
#                   inside an echo, and every comment form is inert by itself
#                   and invisible in front of a real statement.
#   5. completeness cases/ and expect.txt must agree 1:1, so neither can gain
#                   a silent extra.
#
# Cross-version regression detection is deliberately not here: that is
# `make check-vs REF=<ref>`, which runs this same suite against a ref build.
# The stored table of output hashes was removed because it had to be
# regenerated after every intended change -- which is exactly when it stops
# catching unintended ones.
#
# POSIX sh on purpose: zsh does not word-split unquoted expansions.
set -u
LC_ALL=C; export LC_ALL
cd "$(dirname "$0")" || exit 1

# BIN may be overridden (make check-vs REF=... sets it to a ref build) so the
# same suite can be pointed at any binary without copying the harness.
BIN=${BIN:-../bootstrap}
CASES=cases
EXPECT=expect.txt
VERBOSE=${VERBOSE:-0}

if [ ! -x "$BIN" ]; then
  echo "check.sh: $BIN is missing or not executable" >&2
  exit 1
fi

# One scratch directory, holding one file for stderr, reused for every
# invocation and removed on exit. POSIX sh cannot separate a command's stdout
# from its stderr in memory without process substitution. Nothing else is
# written: every input is a committed case file, every generated variant is
# piped straight into the compiler.
WORK=$(mktemp -d "${TMPDIR:-/tmp}/bootstrap-check.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' 0 1 2 15
ERRF="$WORK/err"

fails=0
note_fail() { fails=$((fails + 1)); echo "  FAIL $*"; }

# Every run_* sets R_RC, R_OUT, R_ERR. Command substitution strips the
# output's trailing newlines; block() puts exactly one back, and
# expected_block() is built line by line, so both sides are normalised alike.
run_file()  { R_OUT=$("$BIN" --ast "$1" 2>"$ERRF"); R_RC=$?; R_ERR=$(cat "$ERRF"); }
run_stdin() { R_OUT=$("$BIN" --ast - <"$1" 2>"$ERRF"); R_RC=$?; R_ERR=$(cat "$ERRF"); }
run_str()   { R_OUT=$(printf '%s\n' "$1" | "$BIN" --ast - 2>"$ERRF"); R_RC=$?; R_ERR=$(cat "$ERRF"); }

# run_wrapped <file> <prefix> <suffix>: feed prefix + file bytes + suffix on
# stdin. Both are printf '%b' formats. The bytes go down a pipe rather than
# through a shell variable, so a source that lacks a final newline keeps
# lacking it -- which is what edge_no_trailing_nl exists to check.
run_wrapped() {
  R_OUT=$( { printf '%b' "$2"; cat "$1"; printf '%b' "$3"; } | "$BIN" --ast - 2>"$ERRF" )
  R_RC=$?; R_ERR=$(cat "$ERRF")
}

# run_split <file> <offset>: replace the space at that 0-based byte offset
# with a newline and pipe the result in. head -c takes bytes [0, offset), so
# tail starts after the space at offset+1, i.e. -c +(offset+2).
run_split() {
  R_OUT=$( { head -c "$2" "$1"; printf '\n'; tail -c +$(( $2 + 2 )) "$1"; } | "$BIN" --ast - 2>"$ERRF" )
  R_RC=$?; R_ERR=$(cat "$ERRF")
}

# block <rc> <stdout> <stderr>: the canonical shape of one run, and the same
# shape expect.txt stores, so every comparison is plain string equality.
block() {
  printf 'rc=%s\n' "$1"
  [ -n "$2" ] && { printf '%s\n' '--- out'; printf '%s\n' "$2"; }
  [ -n "$3" ] && { printf '%s\n' '--- err'; printf '%s\n' "$3"; }
  return 0
}
run_block() { block "$R_RC" "$R_OUT" "$R_ERR"; }

# expected_block <name>: the expect.txt block for a case, with its header line
# rewritten from `=== name rc=N` to `rc=N` so it matches run_block(). A block
# ends at the next case or at a group comment; trailing blanks are the
# separator between entries, so they are dropped -- which is also what happens
# to the run side inside $( ).
expected_block() {
  awk -v n="$1" '
    $1 == "===" && $2 == n { grab = 1; sub(/^=== [^ ]+ rc=/, "rc="); b[++c] = $0; next }
    grab && ($1 == "===" || substr($0, 1, 1) == "#") { exit }
    grab { b[++c] = $0 }
    END { while (c > 0 && b[c] == "") c--; for (i = 1; i <= c; i++) print b[i] }
  ' "$EXPECT"
}

# space_offsets <file>: 0-based byte offset of every space that is NOT inside a
# comment. Splitting a line at a space is supposed to be a no-op, but a space
# inside `// x` is part of the comment: replacing it yields `//` + a stray line
# starting with `x`, which is a different program. So the scanner tracks the
# comment state the way the lexer does -- line comments end at the newline,
# block comments nest.
#
# awk's i is 1-based within the current line, and p is the bytes of every
# previous line including its newline.
space_offsets() {
  awk '
    {
      n = length($0)
      for (i = 1; i <= n; i++) {
        c = substr($0, i, 1); two = substr($0, i, 2)
        if (in_line) continue
        if (in_block) {
          if (two == "/*") { in_block++; i++ }
          else if (two == "*/") { in_block--; i++ }
          continue
        }
        if (two == "//") { in_line = 1; i++ }
        else if (two == "/*") { in_block = 1; i++ }
        else if (c == " ") print p + i - 1
      }
      p += n + 1
      in_line = 0
    }
  ' "$1"
}

echo "== completeness =="
n_files=0
for f in "$CASES"/*.x; do
  [ -e "$f" ] || continue
  n_files=$((n_files + 1))
  name=$(basename "$f" .x)
  grep -q "^=== $name rc=" "$EXPECT" || note_fail "$f has no entry in $EXPECT"
done
n_entries=$(grep -c '^=== ' "$EXPECT")
if [ "$n_entries" -ne "$n_files" ]; then
  note_fail "$EXPECT has $n_entries entries but $CASES/ has $n_files cases"
fi
echo "  $n_files cases, $n_entries expectations"

echo "== cases =="
n_acc=0
n_rej=0
for f in "$CASES"/*.x; do
  [ -e "$f" ] || continue
  name=$(basename "$f" .x)
  run_file "$f"
  want=$(expected_block "$name")
  got=$(run_block)

  if [ "$want" != "$got" ]; then
    note_fail "$name differs from $EXPECT"
    [ "$VERBOSE" = "1" ] && { printf '%s\n' "$want" | sed 's/^/    want /'; printf '%s\n' "$got" | sed 's/^/    got  /'; }
    continue
  fi

  # Anything other than accept (0) or reject (4) is a crash or a signal.
  if [ "$R_RC" -ne 0 ] && [ "$R_RC" -ne 4 ]; then
    note_fail "$name exited $R_RC (reject is 4, crash is >= 128)"
    continue
  fi

  # The stdin path must be indistinguishable from the file path. This is what
  # guards the reader change: if reading from '-' ever disagrees with reading
  # a filename, every case reports it at once.
  run_stdin "$f"
  if [ "$(run_block)" != "$want" ]; then
    note_fail "$name: stdin and file disagree"
    [ "$VERBOSE" = "1" ] && { printf '%s\n' "$want" | sed 's/^/    file  /'; run_block | sed 's/^/    stdin /'; }
    continue
  fi

  # Determinism: the same input run again must give the same answer.
  run_file "$f"
  if [ "$(run_block)" != "$want" ]; then
    note_fail "$name is not deterministic"
    continue
  fi

  if [ "$R_RC" -eq 4 ]; then
    # A diagnostic position must land inside the source. Reporting a line past
    # the end, or a column past the line, is the "col 4 when it meant col 8"
    # family -- a wrong message that still reads as plausible.
    pos=$(printf '%s\n' "$R_ERR" | sed -n 's/.* at line \([0-9]*\), col \([0-9]*\).*/\1 \2/p')
    if [ -n "$pos" ]; then
      el=${pos%% *}
      ec=${pos#* }
      src_lines=$(awk 'END { print NR }' "$f")
      if [ "$el" -lt 1 ] || [ "$el" -gt "$src_lines" ]; then
        note_fail "$name: error on line $el, source has $src_lines lines"
      else
        line_len=$(awk -v n="$el" 'NR == n { print length($0) }' "$f")
        if [ "$ec" -lt 1 ] || { [ -n "$line_len" ] && [ "$ec" -gt "$line_len" ]; }; then
          note_fail "$name: error at col $ec of line $el, which is $line_len bytes"
        fi
      fi
    fi
    n_rej=$((n_rej + 1))
  else
    n_acc=$((n_acc + 1))
  fi
done
echo "  $n_acc accepted, $n_rej rejected, all matched"

echo "== properties =="
n_comment=0
n_split=0
n_prop=0

for f in "$CASES"/*.x; do
  [ -e "$f" ] || continue
  name=$(basename "$f" .x)
  run_file "$f"
  # Rejections are pinned exactly by expect.txt instead. Prepending a comment
  # would shift the line numbers inside the very diagnostic being asserted, so
  # "the message changed" and "the position moved" would be indistinguishable.
  [ "$R_RC" -ne 0 ] && continue
  want=$(run_block)

  # A comment on its own line before the source, and a block comment around it,
  # must not change the result. The old generated corpus approximated this with
  # 77 combinations per input; stated as a property it needs no baseline.
  run_wrapped "$f" '// c\n' ''
  n_comment=$((n_comment + 1))
  if [ "$(run_block)" != "$want" ]; then
    n_prop=$((n_prop + 1))
    note_fail "$name: a leading line comment changed the result"
    [ "$VERBOSE" = "1" ] && { printf '%s\n' "$want" | sed 's/^/    plain /'; run_block | sed 's/^/    cmt  /'; }
  fi

  run_wrapped "$f" '/* c */\n' '\n/* c */\n'
  n_comment=$((n_comment + 1))
  if [ "$(run_block)" != "$want" ]; then
    n_prop=$((n_prop + 1))
    note_fail "$name: a surrounding block comment changed the result"
    [ "$VERBOSE" = "1" ] && { printf '%s\n' "$want" | sed 's/^/    plain /'; run_block | sed 's/^/    cmt  /'; }
  fi

  # The multi-line rule: at every space outside a comment, a newline may go
  # instead without changing the parse -- checked at every such space, not a
  # hand-picked few.
  n_sp=$(tr -cd ' ' < "$f" | wc -c)
  offs=$(space_offsets "$f")
  n_off=$(printf '%s' "$offs" | grep -c .)
  # Self-check the scanner: with no comment markers in the source it must find
  # every space, so a broken scanner cannot silently run zero variants.
  if ! grep -qE '//|/\*' "$f"; then
    [ "$n_off" -ne "$n_sp" ] && note_fail "$name: scanner found $n_off of $n_sp spaces and the source has no comments"
  elif [ "$n_off" -gt "$n_sp" ]; then
    note_fail "$name: scanner found $n_off offsets for $n_sp spaces"
  fi
  for off in $offs; do
    run_split "$f" "$off"
    n_split=$((n_split + 1))
    if [ "$(run_block)" != "$want" ]; then
      n_prop=$((n_prop + 1))
      note_fail "$name: newline at byte $off changed the parse"
      [ "$VERBOSE" = "1" ] && { printf '%s\n' "$want" | sed 's/^/    plain /'; run_block | sed 's/^/    split /'; }
      break
    fi
  done
done
echo "  $n_comment comment variants, $n_split split variants, $n_prop violations"

echo "== snippets =="
# snippets/ is the hand-written material that used to feed gen.awk. With the
# generator gone it is checked directly instead of being mixed into a corpus of
# thousands and compared against a hash. Each claim here needs no stored answer.
n_stmt=0
n_expr=0
n_cmt=0

# ok_parse <label>: what it means for a snippet that should compile.
ok_parse() {
  if [ "$R_RC" -ne 0 ]; then note_fail "$1: rc=$R_RC, expected 0"; return 1; fi
  if [ -n "$R_ERR" ]; then note_fail "$1: stderr not empty: $R_ERR"; return 1; fi
  case "$R_OUT" in
    block*) return 0 ;;
    *) note_fail "$1: output did not start with 'block'"; return 1 ;;
  esac
}

while IFS= read -r line || [ -n "$line" ]; do
  case "$line" in ''|'#'*) continue ;; esac
  n_stmt=$((n_stmt + 1))
  run_str "$line"
  ok_parse "statements.txt: [$line]"
done < snippets/statements.txt

while IFS= read -r line || [ -n "$line" ]; do
  case "$line" in ''|'#'*) continue ;; esac
  n_expr=$((n_expr + 1))
  run_str "echo $line"
  ok_parse "exprs.txt: [echo $line]"
done < snippets/exprs.txt

# A comment has to be inert alone (the program it sits in is empty) and
# invisible in front of real code. Eleven hand-written forms rather than the
# two the case layer tries, including one full of slashes and operators.
run_str 'echo 1'
cmt_want=$(run_block)
while IFS= read -r line || [ -n "$line" ]; do
  case "$line" in ''|'#'*) continue ;; esac
  n_cmt=$((n_cmt + 1))
  run_str "$line"
  if [ "$R_RC" -ne 0 ] || [ "$R_OUT" != block ] || [ -n "$R_ERR" ]; then
    note_fail "comments.txt: [$line] alone is not an empty program (rc=$R_RC, out=[$R_OUT])"
  fi
  run_str "$(printf '%s\necho 1' "$line")"
  if [ "$(run_block)" != "$cmt_want" ]; then
    note_fail "comments.txt: [$line] in front of 'echo 1' changed the result"
    [ "$VERBOSE" = "1" ] && { printf '%s\n' "$cmt_want" | sed 's/^/    plain /'; run_block | sed 's/^/    with  /'; }
  fi
done < snippets/comments.txt
echo "  $n_stmt statements, $n_expr expressions, $n_cmt comments checked twice"

echo
if [ "$fails" -eq 0 ]; then
  echo "PASS"
  exit 0
fi
echo "FAIL ($fails)"
exit 1
