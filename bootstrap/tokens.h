#ifndef TOKENS_H
#define TOKENS_H

#include <stddef.h>

typedef enum TokenKind {
  TOK_UNDEF = 0,

  // literals
  TOK_NUMBER_L, // int literal 123
  TOK_STRING_L, // string literal "..."
  TOK_BOOL_L,   // boolean literal true, false

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
  TOK_ECHO, // bootstrap compiler only
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
  TOK_ID,
  TOK_IF,

  TOK_OPERATOR_BEGIN,

  // 4 char operators
  TOK_USHR_ASSIGN,

  // 3 char operators
  TOK_USHR,
  TOK_RANGE_LE,
  TOK_RANGE_LT,
  TOK_SHL_ASSIGN,
  TOK_SHR_ASSIGN,
  TOK_SHL_CY,
  TOK_SHR_CY,

  // 2 char operators
  TOK_SLC_START, // start of a single line comment //
  TOK_MLC_START, // start of a multi line comment  /*
  TOK_SWAP,
  TOK_DEFINE, // :=
  TOK_SHL,
  TOK_SHR,
  TOK_AND,
  TOK_OR,
  TOK_QQ, // ??
  TOK_LE,
  TOK_GE,
  TOK_EQ,
  TOK_NE,
  TOK_ARROW,    // ->
  TOK_FATARROW, // =>
  TOK_RECV,     // <-
  TOK_ADD_ASSIGN,
  TOK_SUB_ASSIGN,
  TOK_MUL_ASSIGN,
  TOK_DIV_ASSIGN,
  TOK_MOD_ASSIGN,
  TOK_AND_ASSIGN,
  TOK_OR_ASSIGN,
  TOK_XOR_ASSIGN,
  TOK_INC, // ++
  TOK_DEC, // --

  // 1 char oprators
  TOK_SPACE,
  TOK_NL,       // \n
  TOK_SHARP,    // #
  TOK_LPAREN,   // (
  TOK_RPAREN,   // )
  TOK_LBRACKET, // [
  TOK_RBRACKET, // ]
  TOK_LBRACE,   // {
  TOK_RBRACE,   // }
  TOK_COLON,
  TOK_SEMICOLON,
  TOK_AMP,   // &
  TOK_PIPE,  // |
  TOK_CARET, // ^
  TOK_TILDE,
  TOK_PLUS,
  TOK_MINUS,
  TOK_STAR, // *
  TOK_SLASH,
  TOK_PERCENT,
  TOK_BANG, // !
  TOK_QMARK,
  TOK_ASSIGN, // =
  TOK_LT,
  TOK_GT,
  TOK_DOT,
  TOK_OPERATOR_END,
} TokenKind;

typedef struct TokenDef {
  enum TokenKind kind;
  size_t len;
  const char *letters;
} TokenDef;

// filled in tokens.c
extern const TokenDef TOKEN_KEYWORDS[];
extern const size_t TOKEN_KEYWORDS_COUNT;

#define TOKEN_VALUE_MAX_SIZE 128

typedef struct Token {
  enum TokenKind kind;
  size_t line;
  size_t col;
  char value[];
} Token;

Token *token_new(TokenKind kind, size_t line, size_t col);

Token *token_new_v(TokenKind kind, size_t line, size_t col, char* buf, size_t buf_size);

void print_token(struct Token *tok);

const char *token_kind_to_string(enum TokenKind token_kind);

#endif
