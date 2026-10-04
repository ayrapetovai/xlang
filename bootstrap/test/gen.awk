# gen.awk -- deterministic test-input generator for the bootstrap compiler.
#
#   awk -v out=DIR -v snippets=DIR -v mode=corpus -v n=N -f gen.awk
#   awk -v out=DIR -v snippets=DIR -v mode=pairs  -f gen.awk
#
# corpus mode emits a systematic corpus over these axes:
#
#   statement pair   (S,S) and (S,next S)      -- same and adjacent
#   body variant     plain / blank-line / ";" / ";;;" / none / " ;;; " / single
#                    / single split once / single split twice
#   comment snippet  snippets/comments.txt -- mixes // and /* */ forms
#   placement        none / leading / before-last / trailing / end-of-line-1 /
#                    leading-with-trailing-spaces / front-of-line-1
#   final newline    present / absent
#
# A block-comment snippet on its own line is a distinct case from a // one: the
# parser skips to "*/" and then synthesises a TOK_NL, so /* */ separates
# statements the way a blank line does but never continues an expression.
#
# Placement 7 prefixes the comment to line 1 rather than appending it, so the
# comment swallows everything after it on that line. That is a distinct path
# through comment skipping -- the comment arrives before the code, not after --
# and the old corpus exercised it, so the axis stays.
#
# The statement axis pairs each snippet with itself and with its successor, and
# the "single" body variant emits one statement on its own. The old corpus used
# the full 9x9 cross product; every statement still appears in both positions,
# and only the identities of the two statements in a joined line differ, which
# is not a distinction the parser can observe. The single-statement variant is
# what makes "the comment is the last line" reachable -- with two statements the
# trailing placements always have code after them.
#
# The "split once" and "split twice" body variants break statement A at one or
# two of its continuation boundaries, which is how the corpus reaches the
# newline-continuation rule. See split_stmt() below.
#
# The full enumeration is emitted only when n covers it; a smaller n is an even
# (Bresenham) sample of the whole enumeration, so every axis stays represented
# instead of just the innermost loop.
#
# pairs mode emits, per expression shape, the joined form plus every legal
# split of it, and a manifest naming the files whose `ast` output must agree.
# A split is legal unless the two adjacent characters would lex as one token:
# two word characters (12 -> 1 2) or two operator characters (== -> = =).
#
# Both modes write a manifest of the files they emitted, so a runner can check
# a subset of a larger golden set without re-deriving the selection rule.

function die(msg) {
  print "gen.awk: " msg > "/dev/stderr"
  exit 1
}

# Reads one snippet per line. Blank lines and `#` lines are dropped, so the
# snippet files can be commented; a line that is only whitespace would be a
# legitimate but useless snippet, so it is treated as blank.
#
# One line per snippet means a snippet cannot contain a newline. That is why
# statements.txt holds only single-line statements and split_stmt() inserts the
# breaks instead.
function slurp(path, target,   line, cnt) {
  cnt = 0
  while ((getline line < path) > 0) {
    if (line == "" || line ~ /^[ \t]*#/) continue
    target[++cnt] = line
  }
  close(path)
  if (cnt == 0) die("no snippets read from " path)
  return cnt
}

function mkdirp(dir,   cmd) {
  cmd = "mkdir -p " dir
  if (system(cmd) != 0) die("cannot create " dir)
}

function write_case(name, text, final_nl,   path) {
  path = out "/" name ".x"
  printf "%s%s", text, (final_nl ? "\n" : "") > path
  if (close(path) != 0) die("cannot write " path)
  print name ".x" >> manifest
}

# ------------------------------------------------------- splitting a statement
# This is the axis the old corpus lacked entirely. Every statement it generated
# was complete on one line, so an injected bug in the parser's
# newline-continuation rule (nl_probe in parser.c) left all 5100 golden hashes
# unchanged while the canaries and the split invariant both went red. The corpus
# has to be able to finish a statement on the next line, or the golden cannot
# see the rule the parser mostly exists to implement.
#
# Only boundaries where the rule actually engages are used: the left side must
# end in an operator, or the right side must begin with one or with ")". A break
# after `echo`, say, engages nothing -- the parser is still at statement start --
# so it is not what this axis is for. (It is still generated, as a rejected
# input, by the `;` / `none` body variants.)
#
# split_stmt <text> <which> fills LINES[1..split_nl] with <text> broken at its
# which-th such boundary, 1-based, so variants 8 and 9 reach different depths of
# the same expression. Returns 0 and fills nothing if there is no which-th
# boundary, which is the case for `echo 1`.
function split_stmt(text, which,   i, cand, n) {
  cand = 0
  for (i = 1; i < length(text); i++) {
    if (!split_is_legal(text, i)) continue
    a = substr(text, i, 1)
    b = substr(text, i + 1, 1)
    if (!(is_opish(a) || is_opish(b) || b == ")")) continue
    if (++cand != which) continue

    LINES[1] = substr(text, 1, i)
    LINES[2] = substr(text, i + 1)
    split_nl = 2
    return 1
  }
  return 0
}

# ---------------------------------------------------------------- body shapes
# Fills LINES[1..nl] with the body for statement pair p under variant v.
function build_body(p, v,   nl, tmp, i, k) {
  delete LINES
  nl = 0
  if (v == 1) {                                   # one statement per line
    LINES[++nl] = STMT[PAIR_A[p]]
    LINES[++nl] = STMT[PAIR_B[p]]
  } else if (v == 2) {                            # blank line between
    LINES[++nl] = STMT[PAIR_A[p]]
    LINES[++nl] = ""
    LINES[++nl] = ""
    LINES[++nl] = STMT[PAIR_B[p]]
  } else if (v == 3) {                            # ";" with no space
    LINES[++nl] = STMT[PAIR_A[p]] ";" STMT[PAIR_B[p]]
  } else if (v == 4) {                            # separator run on its own line
    LINES[++nl] = STMT[PAIR_A[p]] ";;;"
    LINES[++nl] = STMT[PAIR_B[p]]
  } else if (v == 5) {                            # no separator at all
    LINES[++nl] = STMT[PAIR_A[p]] STMT[PAIR_B[p]]
  } else if (v == 6) {                            # separator run, same line
    LINES[++nl] = STMT[PAIR_A[p]] " ;;; " STMT[PAIR_B[p]]
  } else if (v == 7) {                            # single statement
    LINES[++nl] = STMT[PAIR_A[p]]
  } else if (v == 8) {                            # single, split once
    if (!split_stmt(STMT[PAIR_A[p]], 1)) LINES[++nl] = STMT[PAIR_A[p]]
    else nl = split_nl
  } else {                                        # single, split twice
    if (!split_stmt(STMT[PAIR_A[p]], 2)) LINES[++nl] = STMT[PAIR_A[p]]
    else nl = split_nl
  }
  return nl
}

# Places comment snippet c into the body according to placement pl, returning
# the new line count. The body is copied into OUTLINES first.
function place_comment(c, pl,   nl, i, out_nl) {
  delete OUTLINES
  nl = 0
  for (i = 1; i <= BODY_NL; i++) OUTLINES[++nl] = LINES[i]

  if (pl == 1) {                                  # none
    # nothing added
  } else if (pl == 2 || pl == 6) {                # leading line
    for (i = nl; i >= 1; i--) OUTLINES[i + 1] = OUTLINES[i]
    OUTLINES[1] = (pl == 6) ? COMMENT[c] "   " : COMMENT[c]
  } else if (pl == 3) {                           # before the last line
    OUTLINES[++nl] = COMMENT[c]
  } else if (pl == 4) {                           # after everything
    OUTLINES[++nl] = COMMENT[c]
  } else if (pl == 5) {                           # appended to line 1
    OUTLINES[1] = OUTLINES[1] " " COMMENT[c]
  } else {                                        # prefixed to line 1
    OUTLINES[1] = COMMENT[c] OUTLINES[1]
  }

  out_nl = nl
  delete LINES
  for (i = 1; i <= out_nl; i++) LINES[i] = OUTLINES[i]
  return out_nl
}

function join_lines(nl,   text, i) {
  text = ""
  for (i = 1; i <= nl; i++) text = text (i > 1 ? "\n" : "") LINES[i]
  return text
}

# ------------------------------------------------------------------- helpers
function is_word(c) {
  return (c >= "0" && c <= "9") || (c >= "a" && c <= "z") || \
         (c >= "A" && c <= "Z") || c == "_"
}

function is_opish(c) {
  return index("+-*/%<>=!&|^~?:.@#", c) > 0
}

function split_is_legal(e, i,   a, b) {
  a = substr(e, i - 1, 1)
  b = substr(e, i, 1)
  return !(is_word(a) && is_word(b)) && !(is_opish(a) && is_opish(b))
}

BEGIN {
  if (out == "") die("out is required")
  if (snippets == "") snippets = "snippets"
  if (mode == "") die("mode is required")

  ns = slurp(snippets "/statements.txt", STMT)
  nc = slurp(snippets "/comments.txt", COMMENT)

  mkdirp(out)
  manifest = out "/../manifest.txt"
  pairs_manifest = out "/../pairs.manifest"
  printf "%s", "" > manifest # truncate once; every later write appends
  close(manifest)

  if (mode == "corpus") {
    if (n == "") n = 0

    for (i = 1; i <= ns; i++) {
      PAIR_A[++np] = i
      PAIR_B[np] = i
      PAIR_A[++np] = i
      PAIR_B[np] = (i % ns) + 1
    }

    nvariant = 9
    nplace = 7
    total = np * nvariant * nc * nplace * 2
    if (n == 0 || n > total) n = total

    idx = 0
    for (pi = 1; pi <= np; pi++)
      for (v = 1; v <= nvariant; v++)
        for (c = 1; c <= nc; c++)
          for (pl = 1; pl <= nplace; pl++)
            for (fn = 1; fn <= 2; fn++) {
              idx++
              # even sample: keep exactly n of total, spread across all axes
              if (int(idx * n / total) == int((idx - 1) * n / total)) continue

              BODY_NL = build_body(pi, v)
              body_nl = place_comment(c, pl)
              text = join_lines(body_nl)

              name = "c" sprintf("%06d", idx)
              write_case(name, text, (fn == 1))
              emitted++
            }

    printf "gen.awk: corpus %d of %d enumerated combinations -> %s\n", \
           emitted, total, out > "/dev/stderr"
    exit 0
  }

  if (mode == "pairs") {
    ne = slurp(snippets "/exprs.txt", EXPR)
    printf "%s", "" > pairs_manifest
    close(pairs_manifest)

    seq = 0
    for (ei = 1; ei <= ne; ei++) {
      body = EXPR[ei]
      jname = sprintf("p%03d_joined", ei)
      write_case(jname, "echo " body, 1)

      split_list = ""
      for (i = 1; i < length(body); i++) {
        if (!split_is_legal(body, i)) continue

        left = substr(body, 1, i)
        right = substr(body, i + 1)

        seq++
        n1 = sprintf("p%03d_s%02d_a", ei, seq)
        write_case(n1, "echo " left "\n" right, 1)
        split_list = split_list " " n1 ".x"

        seq++
        n2 = sprintf("p%03d_s%02d_b", ei, seq)
        write_case(n2, "echo " left "\n\n" right, 1)
        split_list = split_list " " n2 ".x"

        seq++
        n3 = sprintf("p%03d_s%02d_c", ei, seq)
        write_case(n3, "echo " left "\n// c\n" right, 1)
        split_list = split_list " " n3 ".x"
      }

      printf "%s.x%s\n", jname, split_list >> pairs_manifest
    }

    printf "gen.awk: %d expression shapes -> %d files in %s\n", \
           ne, seq + ne, out > "/dev/stderr"
    exit 0
  }

  die("unknown mode '" mode "'")
}
