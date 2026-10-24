#ifndef AST_DUMP_H
#define AST_DUMP_H

#include "walker.h"
#include <stddef.h>

typedef struct PrintNode PrintNode;

// This is one string (wihtout indention symbols) representation.
// The next field is a next string to be printed.
// This is basically a linked list.
typedef struct PrintNode {
  char *text;
  size_t indent;
  PrintNode *next;
} PrintNode;

// A chain of PrintNodes, head..tail with tail->next == NULL.
// The tail is stored here, next to the head, instead of inside every node:
// a node's tail becomes stale as soon as the chain is extended after a new
// head was wrapped around it. Keeping it on the traversal stack makes appends
// O(1) and impossible to invalidate through an interior node.
typedef struct {
  PrintNode *head;
  PrintNode *tail;
} PrintChain;

typedef struct {
  WalkerContextHeader header;
  size_t  stack_len;
  PrintChain *stack;
  size_t  sp;
  bool    colored;
  size_t  depth;
} ASTDump;

ASTDump *astdump_new(size_t max_stack_size);

void astdump_print(ASTDump *dmp);

void astdump_free(ASTDump *dmp);

#endif

