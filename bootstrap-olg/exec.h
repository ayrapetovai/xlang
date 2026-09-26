#pragma once

#include <stdint.h>
#include <stddef.h>
#include "parser.h"

/*
 * Bootstrap exec pass — the tree-walking interpreter (decision C24).
 *
 * The bootstrap parses the whole program into the arena AST and then
 * *executes* it by walking the tree; it never generates code.  It runs
 * only one program of consequence — the self-hosted compiler's compile of
 * its own source — plus whatever example witnesses we verify along the
 * way.  Performance is irrelevant; fidelity to the AST shapes is not.
 *
 * Bootstrap-scope limits (README abstract, decisions C18/C24): no
 * coroutines, no compile-time code execution.  Channel operations
 * (send/recv/select), `spawn`, `yield`, and generic instantiation are
 * diagnosed as "not executable in the bootstrap phase".
 *
 * Execution model:
 *   - Values are a tagged union living in the exec arena (records are
 *     cells; arrays are value headers sharing a heap element array, so
 *     `a[i] = v` writes through).
 *   - Scope is a chain of frames; the language forbids shadowing, so a
 *     frame lookup is first-match.  Statements within a block run in a
 *     fresh frame popped when the block ends (a block is an arena: one
 *     lifetime).
 *   - Functions are VALUES: user functions are closures over their
 *     declaratior environment (top-level ones capture the global frame);
 *     builtins (out.println/print, the `X.from` cast family, …) are C
 *     pointers.  A function whose body is a block returns the value of its
 *     last statement (implicit tail value) unless an explicit `return`
 *     fired.
 *   - Errors: an expression may yield V_ERR without raising (fallible).
 *     `x!` raises it, `x?` tests it, and `try … catch name { … }` catches
 *     raises with a setjmp frame.  `?? def` settles a failure with a
 *     fallback.  A raised error with no catch frame is a fatal runtime
 *     diagnostic.
 */

/* Runs the parsed program: applies top-level declarations, then calls
 * `main`.  Returns main's int result (0 when the file has no main).
 * Returns -1 on a fatal runtime error, with the diagnostic written into
 * `msg` (`msgn` bytes) and the failing node's line/col into *line and
 * *col. */
int exec_run(const char *src, Node *root, char *msg, size_t msgn,
             int *line, int *col);