#include "tokens.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "string.h"

// must be reverse sorted or matching will read out of bounds
const TokenDef TOKEN_KEYWORDS[] = {
    {TOK_INTERFACE, "interface"},
    {TOK_CONTINUE, "continue"},
    {TOK_DEFAULT, "default"},
    {TOK_STRUCT, "struct"},
    {TOK_STRING, "string"},
    {TOK_SELECT, "select"},
    {TOK_RETURN, "return"},
    {TOK_MODULE, "module"},
    {TOK_IMPORT, "import"},
    {TOK_YIELD, "yield"},
    {TOK_WHILE, "while"},
    {TOK_SPAWN, "spawn"},
    {TOK_MATCH, "match"},
    {TOK_FLOAT, "float"},
    {TOK_FALSE, "false"},
    {TOK_ERROR, "error"},
    {TOK_DEFER, "defer"},
    {TOK_CONST, "const"},
    {TOK_CATCH, "catch"},
    {TOK_BYTES, "bytes"},
    {TOK_BREAK, "break"},
    {TOK_ECHO, "echo"},
    {TOK_VOID, "void"},
    {TOK_UINT, "uint"},
    {TOK_TRUE, "true"},
    {TOK_THEN, "then"},
    {TOK_LOOP, "loop"},
    {TOK_FUNC, "func"},
    {TOK_ENUM, "enum"},
    {TOK_ELSE, "else"},
    {TOK_CHAR, "char"},
    {TOK_BOOL, "bool"},
    {TOK_TRY, "try"},
    {TOK_INT, "int"},
    {TOK_IS, "is"},
    {TOK_IN, "in"},
    {TOK_IF, "if"},
    {TOK_DO, "do"},

    // punctuation
    {TOK_USHR_ASSIGN, ">>>="},

    {TOK_USHR, ">>>"},
    {TOK_RANGE_LE, "..="},
    {TOK_RANGE_LT, "..<"},
    {TOK_SHL_ASSIGN, "<<="},
    {TOK_SHR_ASSIGN, ">>="},
    {TOK_SHL_CY, "<<~"},
    {TOK_SHR_CY, ">>~"},

    {TOK_SLC_START, "//"},
    {TOK_MLC_START, "/*"},

    {TOK_SWAP, "<>"},
    {TOK_DEFINE, ":="},
    {TOK_SHL, "<<"},
    {TOK_SHR, ">>"},
    {TOK_AND, "&&"},
    {TOK_OR, "||"},
    {TOK_QQ, "??"},
    {TOK_LE, "<="},
    {TOK_GE, ">="},
    {TOK_EQ, "=="},
    {TOK_NE, "!="},
    {TOK_ARROW, "->"},
    {TOK_FATARROW, "=>"},
    {TOK_RECV, "<-"},
    {TOK_ADD_ASSIGN, "+="},
    {TOK_SUB_ASSIGN, "-="},
    {TOK_MUL_ASSIGN, "*="},
    {TOK_DIV_ASSIGN, "/="},
    {TOK_MOD_ASSIGN, "%="},
    {TOK_AND_ASSIGN, "&="},
    {TOK_OR_ASSIGN, "|="},
    {TOK_XOR_ASSIGN, "^="},
    {TOK_INC, "++"},
    {TOK_DEC, "--"},

    {TOK_NL, "\n"},
    {TOK_SHARP, "#"},
    {TOK_LPAREN, "("},
    {TOK_RPAREN, ")"},
    {TOK_LBRACKET, "["},
    {TOK_RBRACKET, "]"},
    {TOK_LBRACE, "{"},
    {TOK_RBRACE, "}"},
    {TOK_COLON, ":"},
    {TOK_SEMICOLON, ";"},
    {TOK_AMP, "&"},
    {TOK_PIPE, "|"},
    {TOK_CARET, "^"},
    {TOK_TILDE, "~"},
    {TOK_PLUS, "+"},
    {TOK_MINUS, "-"},
    {TOK_STAR, "*"},
    {TOK_SLASH, "/"},
    {TOK_PERCENT, "%"},
    {TOK_BANG, "!"},
    {TOK_QMARK, "?"},
    {TOK_ASSIGN, "="},
    {TOK_LT, "<"},
    {TOK_GT, ">"},
    {TOK_SPACE, " "},
    {TOK_DOT, "."},
};

const size_t TOKEN_KEYWORDS_COUNT =
    sizeof TOKEN_KEYWORDS / sizeof TOKEN_KEYWORDS[0];

Token *token_new(TokenKind kind, size_t line, size_t col) {
  Token *tok = malloc(sizeof(*tok) + 1); // + 1 for '\0'
  tok->line = line;
  tok->col = col;
  tok->value[0] = '\0';
  tok->kind = kind;
  return tok;
}

Token *token_new_v(TokenKind kind, size_t line, size_t col, char* buf, size_t buf_size) {
  Token *tok = malloc(sizeof(*tok) + buf_size + 1); // + 1 for '\0'
  tok->line = line;
  tok->col = col;
  tok->kind = kind;
  strcpy(tok->value, buf);
  return tok;
}

void print_token(struct Token *tok) {
  printf("kind=%3d:%20s, value=%10s, line=%3ld, col=%3ld\n", tok->kind,
         token_kind_to_string(tok->kind), tok->value, tok->line, tok->col);
}

const char *token_kind_to_string(enum TokenKind token_kind) {
  switch (token_kind) {
  case TOK_UNDEF:
    return "undefined";
  case TOK_NUMBER_L:
    return "number literal";
  case TOK_STRING_L:
    return "string literal";
  case TOK_INTERFACE:
    return "interface";
  case TOK_CONTINUE:
    return "continue";
  case TOK_DEFAULT:
    return "default";
  case TOK_IMPORT:
    return "import";
  case TOK_MODULE:
    return "module";
  case TOK_SELECT:
    return "select";
  case TOK_STRING:
    return "string";
  case TOK_STRUCT:
    return "struct";
  case TOK_RETURN:
    return "return";
  case TOK_FALSE:
    return "false";
  case TOK_BYTES:
    return "bytes";
  case TOK_MATCH:
    return "match";
  case TOK_WHILE:
    return "while";
  case TOK_CONST:
    return "const";
  case TOK_FLOAT:
    return "float";
  case TOK_ERROR:
    return "error";
  case TOK_BREAK:
    return "break";
  case TOK_YIELD:
    return "yield";
  case TOK_SPAWN:
    return "spawn";
  case TOK_DEFER:
    return "defer";
  case TOK_CATCH:
    return "catch";
  case TOK_ECHO:
    return "echo";
  case TOK_CHAR:
    return "char";
  case TOK_THEN:
    return "then";
  case TOK_ELSE:
    return "else";
  case TOK_BOOL:
    return "bool";
  case TOK_LOOP:
    return "loop";
  case TOK_ENUM:
    return "enum";
  case TOK_FUNC:
    return "func";
  case TOK_VOID:
    return "void";
  case TOK_TRUE:
    return "true";
  case TOK_UINT:
    return "uint";
  case TOK_TRY:
    return "try";
  case TOK_INT:
    return "int";
  case TOK_DO:
    return "do";
  case TOK_IS:
    return "is";
  case TOK_IN:
    return "in";
  case TOK_IF:
    return "if";
  case TOK_USHR_ASSIGN:
    return ">>>=";
  case TOK_USHR:
    return ">>>";
  case TOK_RANGE_LE:
    return "..=";
  case TOK_RANGE_LT:
    return "..<";
  case TOK_SHL_ASSIGN:
    return "<<=";
  case TOK_SHR_ASSIGN:
    return ">>=";
  case TOK_SHL_CY:
    return "<<~";
  case TOK_SHR_CY:
    return ">>~";
  case TOK_SLC_START:
    return "//";
  case TOK_MLC_START:
    return "/*";
  case TOK_SWAP:
    return "<>";
  case TOK_DEFINE:
    return ":=";
  case TOK_SHL:
    return "<<";
  case TOK_SHR:
    return ">>";
  case TOK_AND:
    return "&&";
  case TOK_OR:
    return "||";
  case TOK_QQ:
    return "??";
  case TOK_LE:
    return "<=";
  case TOK_GE:
    return ">=";
  case TOK_EQ:
    return "==";
  case TOK_NE:
    return "!=";
  case TOK_ARROW:
    return "->";
  case TOK_FATARROW:
    return "=>";
  case TOK_RECV:
    return "<-";
  case TOK_ADD_ASSIGN:
    return "+=";
  case TOK_SUB_ASSIGN:
    return "-=";
  case TOK_MUL_ASSIGN:
    return "*=";
  case TOK_DIV_ASSIGN:
    return "/=";
  case TOK_MOD_ASSIGN:
    return "%=";
  case TOK_AND_ASSIGN:
    return "&=";
  case TOK_OR_ASSIGN:
    return "|=";
  case TOK_XOR_ASSIGN:
    return "^=";
  case TOK_INC:
    return "++";
  case TOK_DEC:
    return "--";
  case TOK_SPACE:
    return "space";
  case TOK_NL:
    // return "new line";
    return "\\n";
  case TOK_SHARP:
    return "#";
  case TOK_LPAREN:
    return "(";
  case TOK_RPAREN:
    return ")";
  case TOK_LBRACKET:
    return "[";
  case TOK_RBRACKET:
    return "]";
  case TOK_LBRACE:
    return "{";
  case TOK_RBRACE:
    return "}";
  case TOK_COLON:
    return ":";
  case TOK_SEMICOLON:
    return ";";
  case TOK_AMP:
    return "&";
  case TOK_PIPE:
    return "|";
  case TOK_CARET:
    return "^";
  case TOK_TILDE:
    return "~";
  case TOK_PLUS:
    return "+";
  case TOK_MINUS:
    return "-";
  case TOK_STAR:
    return "*";
  case TOK_SLASH:
    return "/";
  case TOK_PERCENT:
    return "%";
  case TOK_BANG:
    return "!";
  case TOK_QMARK:
    return "?";
  case TOK_ASSIGN:
    return "=";
  case TOK_LT:
    return "<";
  case TOK_GT:
    return ">";
  case TOK_DOT:
    return ".";

  case TOK_OPERATOR_BEGIN:
  case TOK_OPERATOR_END:
    assert(false);

    // no spelling is registered for these kinds (the loop version fell through)
  case TOK_ID:
  default:
    return "identifier";
  }
}
