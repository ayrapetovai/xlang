#ifndef TOKENS_H
#define TOKENS_H

#include <stddef.h>

typedef enum TokenKind {
  TOK_UNDEF = 0,

// numbers
  TOK_INT_L,    // int literal
  TOK_FLOAT_L,  // float literal

// reserved words
  TOK_INTERFACE,
  TOK_CONTINUE,
  TOK_DEFAULT,
  TOK_IMPORT,
  TOK_MODULE,
  TOK_SELECT,
  TOK_STRING,
  TOK_STRUCT,
  TOK_RETURN,
  TOK_FALSE,
  TOK_BYTES,
  TOK_MATCH,
  TOK_WHILE,
  TOK_CONST,
  TOK_FLOAT,
  TOK_ERROR,
  TOK_BREAK,
  TOK_YIELD,
  TOK_SPAWN,
  TOK_DEFER,
  TOK_CATCH,
  TOK_CHAR,
  TOK_THEN,
  TOK_ELSE,
  TOK_BOOL,
  TOK_LOOP,
  TOK_ENUM,
  TOK_FUNC,
  TOK_VOID,
  TOK_TRUE,
  TOK_UINT,
  TOK_TRY,
  TOK_INT,
  TOK_DO,
  TOK_IS,
  TOK_IN,
  TOK_SEP,
  TOK_ID,
  TOK_IF,

// separators
  TOK_SPACE,
  TOK_NL,
  TOK_DOT,
} TokenKind;

typedef struct {
  enum TokenKind kind;
  const char* letters;
} TokenDef;

// filled in tokens.c
extern const TokenDef TOKEN_KEYWORDS[];
extern const size_t TOKEN_KEYWORDS_COUNT;

typedef struct Token {
  enum TokenKind kind;
  char value[128];
  size_t line;
  size_t col;
} Token;

void print_token(struct Token*);

#endif

