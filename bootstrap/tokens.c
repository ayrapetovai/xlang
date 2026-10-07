#include "tokens.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOKDEF(n, l) {n, strlen(l), l}

// must be reverse sorted or matching will read out of bounds
const TokenDef TOKEN_KEYWORDS[] = {
    TOKDEF(TOK_INTERFACE, "interface"),
    TOKDEF(TOK_CONTINUE,  "continue"),
    TOKDEF(TOK_DEFAULT,   "default"),
    TOKDEF(TOK_STRUCT,    "struct"),
    TOKDEF(TOK_STRING,    "string"),
    TOKDEF(TOK_SELECT,    "select"),
    TOKDEF(TOK_RETURN,    "return"),
    TOKDEF(TOK_MODULE,    "module"),
    TOKDEF(TOK_IMPORT,    "import"),
    TOKDEF(TOK_YIELD,     "yield"),
    TOKDEF(TOK_WHILE,     "while"),
    TOKDEF(TOK_SPAWN,     "spawn"),
    TOKDEF(TOK_MATCH,     "match"),
    TOKDEF(TOK_FLOAT,     "float"),
    TOKDEF(TOK_FALSE,     "false"),
    TOKDEF(TOK_ERROR,     "error"),
    TOKDEF(TOK_DEFER,     "defer"),
    TOKDEF(TOK_CONST,     "const"),
    TOKDEF(TOK_CATCH,     "catch"),
    TOKDEF(TOK_BYTES,     "bytes"),
    TOKDEF(TOK_BREAK,     "break"),
    TOKDEF(TOK_ECHO,      "echo"),
    TOKDEF(TOK_VOID,      "void"),
    TOKDEF(TOK_UINT,      "uint"),
    TOKDEF(TOK_TRUE,      "true"),
    TOKDEF(TOK_THEN,      "then"),
    TOKDEF(TOK_LOOP,      "loop"),
    TOKDEF(TOK_FUNC,      "func"),
    TOKDEF(TOK_ENUM,      "enum"),
    TOKDEF(TOK_ELSE,      "else"),
    TOKDEF(TOK_CHAR,      "char"),
    TOKDEF(TOK_BOOL,      "bool"),
    TOKDEF(TOK_TRY,       "try"),
    TOKDEF(TOK_INT,       "int"),
    TOKDEF(TOK_IS,        "is"),
    TOKDEF(TOK_IN,        "in"),
    TOKDEF(TOK_IF,        "if"),
    TOKDEF(TOK_DO,        "do"),

    // punctuation
    TOKDEF(TOK_USHR_ASSIGN, ">>>="),

    TOKDEF(TOK_USHR,       ">>>"),
    TOKDEF(TOK_RANGE_LE,   "..="),
    TOKDEF(TOK_RANGE_LT,   "..<"),
    TOKDEF(TOK_SHL_ASSIGN, "<<="),
    TOKDEF(TOK_SHR_ASSIGN, ">>="),
    TOKDEF(TOK_SHL_CY,     "<<~"),
    TOKDEF(TOK_SHR_CY,     ">>~"),

    TOKDEF(TOK_SLC_START,  "//"),
    TOKDEF(TOK_MLC_START,  "/*"),

    TOKDEF(TOK_SWAP,       "<>"),
    TOKDEF(TOK_DEFINE,     ":="),
    TOKDEF(TOK_SHL,        "<<"),
    TOKDEF(TOK_SHR,        ">>"),
    TOKDEF(TOK_AND,        "&&"),
    TOKDEF(TOK_OR,         "||"),
    TOKDEF(TOK_QQ,         "??"),
    TOKDEF(TOK_LE,         "<="),
    TOKDEF(TOK_GE,         ">="),
    TOKDEF(TOK_EQ,         "=="),
    TOKDEF(TOK_NE,         "!="),
    TOKDEF(TOK_ARROW,      "->"),
    TOKDEF(TOK_FATARROW,   "=>"),
    TOKDEF(TOK_RECV,       "<-"),
    TOKDEF(TOK_ADD_ASSIGN, "+="),
    TOKDEF(TOK_SUB_ASSIGN, "-="),
    TOKDEF(TOK_MUL_ASSIGN, "*="),
    TOKDEF(TOK_DIV_ASSIGN, "/="),
    TOKDEF(TOK_MOD_ASSIGN, "%="),
    TOKDEF(TOK_AND_ASSIGN, "&="),
    TOKDEF(TOK_OR_ASSIGN, "|="),
    TOKDEF(TOK_XOR_ASSIGN, "^="),
    TOKDEF(TOK_INC,        "++"),
    TOKDEF(TOK_DEC,        "--"),

    TOKDEF(TOK_NL,        "\n"),
    TOKDEF(TOK_SHARP,     "#"),
    TOKDEF(TOK_LPAREN,    "("),
    TOKDEF(TOK_RPAREN,    ")"),
    TOKDEF(TOK_LBRACKET,  "["),
    TOKDEF(TOK_RBRACKET,  "]"),
    TOKDEF(TOK_LBRACE,    "{"),
    TOKDEF(TOK_RBRACE,    "}"),
    TOKDEF(TOK_COLON,     ":"),
    TOKDEF(TOK_SEMICOLON, ";"),
    TOKDEF(TOK_AMP,       "&"),
    TOKDEF(TOK_PIPE,      "|"),
    TOKDEF(TOK_CARET,     "^"),
    TOKDEF(TOK_TILDE,     "~"),
    TOKDEF(TOK_PLUS,      "+"),
    TOKDEF(TOK_MINUS,     "-"),
    TOKDEF(TOK_STAR,      "*"),
    TOKDEF(TOK_SLASH,     "/"),
    TOKDEF(TOK_PERCENT,   "%"),
    TOKDEF(TOK_BANG,      "!"),
    TOKDEF(TOK_QMARK,     "?"),
    TOKDEF(TOK_ASSIGN,    "="),
    TOKDEF(TOK_LT,        "<"),
    TOKDEF(TOK_GT,        ">"),
    TOKDEF(TOK_SPACE,     " "),
    TOKDEF(TOK_DOT,       "."),
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
  case TOK_BOOL_L:
    return "boolean literal";
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
    return "false"; // unreachable, TOK_BOOL_L instead
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
    return "true"; // unreachable, TOK_BOOL_L instead
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

  case TOK_ID:
    return "identifier";
  default:
    assert(false);
  }
}
