#!/bin/sh
# Time two bootstrap binaries over the same generated corpus. Used by
# `make check-vs REF=...`.
#
#   sh bench.sh <bin-a> <bin-b> <label-a> <label-b>
#
# Both binaries must already exist. The corpus is generated once and reused, so
# the only difference measured is the compiler itself.

set -u

cd "$(dirname "$0")" || exit 1

if [ "$#" -ne 4 ]; then
  echo "usage: sh bench.sh <bin-a> <bin-b> <label-a> <label-b>" >&2
  exit 2
fi

# Resolve to absolute paths: this script cd's into test/, and the caller may
# well have passed a path relative to bootstrap/ -- notably "$(CURDIR)/bootstrap".
A=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
B=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
LA=$3
LB=$4
WORK=.work/bench

if [ ! -x "$A" ] || [ ! -x "$B" ]; then
  echo "bench.sh: both binaries must exist and be executable" >&2
  exit 2
fi

rm -rf "$WORK"
mkdir -p "$WORK/corpus" "$WORK/pairs"

awk -v out="$WORK/corpus" -v snippets=snippets -v mode=corpus -v n=0 \
    -f gen.awk || exit 1
awk -v out="$WORK/pairs" -v snippets=snippets -v mode=pairs \
    -f gen.awk || exit 1

n=0
for d in corpus pairs; do
  n=$((n + $(ls "$WORK/$d" | wc -l)))
done
echo "corpus: $n inputs, modes ast+lex"

# run <binary>
#
# Debug-level tracing is filtered for the same reason check.sh strips it: the
# timestamps would otherwise dominate the measurement, and a build with a
# different log level is not comparable at all.
run() {
  for mode in ast lex; do
    for f in "$WORK/corpus"/*.x "$WORK/pairs"/*.x; do
      "$1" "$mode" "$f" 2>/dev/null </dev/null \
        | grep -avE '^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:' \
        > /dev/null
    done
  done
}

# Three rounds each, alternating, so a warm cache or a busy machine biases both
# sides roughly equally rather than whichever ran first.
#
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
echo "      check that both builds use the same log level and the same"
echo "      compiler flags (make check-vs passes the working-tree main.c to"
echo "      both sides for exactly this reason)."