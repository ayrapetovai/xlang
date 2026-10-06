#!/bin/sh
# Time two bootstrap binaries over one workload held in memory, and check that
# they agree on it byte for byte. Used by `make check-vs REF=...`.
#
#   sh bench.sh <bin-a> <bin-b> <label-a> <label-b>
#
# REP (default 20) repeats the workload per timed round; TIME_ROUNDS is fixed
# at 3 below. Both binaries must already exist.
#
# This used to generate 25106 inputs onto disk (plus a "pairs" set) purely to
# give the timing something to chew on, and then compare digests. The digests
# are gone -- check.sh pins every case exactly -- and the corpus is now built
# as a string here and piped in, so a benchmark run writes no files at all.

set -u
LC_ALL=C; export LC_ALL

if [ "$#" -ne 4 ]; then
  echo "usage: sh bench.sh <bin-a> <bin-b> <label-a> <label-b>" >&2
  exit 2
fi

# Resolve to absolute paths BEFORE cd'ing into test/, so that a path relative
# to the caller's cwd (./bootstrap, "$(CURDIR)/bootstrap") still points at the
# right file once we are in a different directory.
A=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
B=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
LA=$3
LB=$4

cd "$(dirname "$0")" || exit 1

if [ ! -x "$A" ] || [ ! -x "$B" ]; then
  echo "bench.sh: both binaries must exist and be executable" >&2
  echo "  A=$A" >&2
  echo "  B=$B" >&2
  exit 2
fi

# The workload: a multi-line block comment to exercise the skip path, then 400
# statements with nested parens, mixed operators and a trailing comment -- a
# few hundred tokens deep, so it crosses the lexer's read window rather than
# sitting inside one. Held in a variable and re-piped per run.
W=$(awk 'BEGIN {
  print "/* a block comment"
  print "   over a few lines */"
  for (i = 0; i < 400; i++)
    printf "echo (1 + 2) * (3 - 4) - %d + %d // expr %d\n", i, i * 2, i
}')
lines=$(printf '%s\n' "$W" | wc -l)
echo "workload: $lines lines, ast+lex, both directions"

# --- agreement ------------------------------------------------------------
# stdout and stderr are captured in separate runs rather than merged: merging
# them would compare the interleaving of two differently-buffered streams,
# which is noise. Any difference here is a real behavioural difference between
# the two builds and is reported before any timing, because a speedup that
# comes from doing less work is not a speedup.
agree=1
for mode in ast lex; do
  a_out=$(printf '%s\n' "$W" | "$A" "--$mode" - 2>/dev/null); a_rc=$?
  a_err=$(printf '%s\n' "$W" | "$A" "--$mode" - 2>&1 1>/dev/null)
  b_out=$(printf '%s\n' "$W" | "$B" "--$mode" - 2>/dev/null); b_rc=$?
  b_err=$(printf '%s\n' "$W" | "$B" "--$mode" - 2>&1 1>/dev/null)
  if [ "$a_rc" -ne "$b_rc" ]; then
    echo "  DIFFERS --$mode: exit status $LA=$a_rc $LB=$b_rc"
    agree=0
  fi
  if [ "$a_out" != "$b_out" ]; then
    echo "  DIFFERS --$mode: stdout ($LA=$(printf '%s' "$a_out" | wc -c) bytes, $LB=$(printf '%s' "$b_out" | wc -c) bytes)"
    agree=0
  fi
  if [ "$a_err" != "$b_err" ]; then
    echo "  DIFFERS --$mode: stderr ($LA=[$a_err] $LB=[$b_err])"
    agree=0
  fi
done
if [ "$agree" -eq 1 ]; then
  echo "agreement: identical stdout, stderr and exit status in both modes"
fi

# --- timing ---------------------------------------------------------------
# run <binary>: both modes, REP times, output discarded. The printf that
# supplies stdin is the same cost on both sides, and the workload is large
# enough that the compiler dominates it. One pass is ~20ms, which is too short
# to time against a scheduler tick, hence the repetition.
REP=${REP:-20}
run() {
  i=0
  while [ "$i" -lt "$REP" ]; do
    for mode in ast lex; do
      printf '%s\n' "$W" | "$1" "--$mode" - >/dev/null 2>&1
    done
    i=$((i + 1))
  done
}

# POSIX `time` writes "real<TAB>0m12.345s", so the value is parsed back out of
# that rather than read from $SECONDS (not in POSIX sh) or TIMEFMT (a zsh/ksh
# extension). /usr/bin/time is not installed here either.
elapsed() {
  { time "$@" >/dev/null 2>&1 ; } 2>&1 | awk '
    $1 == "real" {
      v = $2
      m = 0
      if (match(v, /^[0-9]+m/)) {
        m = substr(v, 1, RLENGTH - 1) + 0
        v = substr(v, RLENGTH + 1)
      }
      sub(/s$/, "", v)
      printf "%.3f\n", m * 60 + v
    }'
}

echo
echo "timings (best of 3, alternating):"
t_a=999; t_b=999
i=1
while [ "$i" -le 3 ]; do
  r=$(elapsed run "$A")
  s=$(elapsed run "$B")
  echo "  round $i: $LA ${r}s   $LB ${s}s"
  t_a=$(awk -v a="$t_a" -v b="$r" 'BEGIN{print (b<a)?b:a}')
  t_b=$(awk -v a="$t_b" -v b="$s" 'BEGIN{print (b<a)?b:a}')
  i=$((i + 1))
done

echo
awk -v a="$t_a" -v b="$t_b" -v la="$LA" -v lb="$LB" 'BEGIN{
  printf "  best: %s %.2fs   %s %.2fs\n", la, a, lb, b
  if (a <= 0 || b <= 0) { print "  cannot compare"; exit }
  r = (a - b) / a * 100
  if (r > 0)     printf "  %s is %.1f%% faster\n", lb, r
  else if (r < 0) printf "  %s is %.1f%% faster\n", la, -r
  else           printf "  no measurable difference\n"
}'

echo
echo "note: a large jump here is worth suspecting before it is believed --"
echo "      both builds must use the same compiler flags and the same log"
echo "      level, and neither side may have failed the agreement check above."
exit $((1 - agree))
