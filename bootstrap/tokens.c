#include "tokens.h"

/* Human-readable names for the token dump (`bootstrap <file>`). */
const char *token_kind_name(TokKind kind) {
  switch (kind) {
    case T_EOF: return "eof";
    case T_SEP: return "sep";
    case T_ID: return "id";
    case T_NUM: return "num";
    case T_CHAR: return "char";
    case T_STR: return "str";
    case T_UNDEF: return "undef";

    case T_KW_IF: return "if";
    case T_KW_THEN: return "then";
    case T_KW_ELSE: return "else";
    case T_KW_LOOP: return "loop";
    case T_KW_DO: return "do";
    case T_KW_UNTIL: return "until";
    case T_KW_MATCH: return "match";
    case T_KW_SELECT: return "select";
    case T_KW_DEFAULT: return "default";
    case T_KW_TRY: return "try";
    case T_KW_CATCH: return "catch";
    case T_KW_DEFER: return "defer";
    case T_KW_SPAWN: return "spawn";
    case T_KW_RETURN: return "return";
    case T_KW_BREAK: return "break";
    case T_KW_CONTINUE: return "continue";
    case T_KW_YIELD: return "yield";
    case T_KW_IN: return "in";
    case T_KW_IS: return "is";
    case T_KW_CONST: return "const";
    case T_KW_MODULE: return "module";
    case T_KW_IMPORT: return "import";
    case T_KW_STRUCT: return "struct";
    case T_KW_ENUM: return "enum";
    case T_KW_FUNC: return "func";
    case T_KW_INTERFACE: return "interface";
    case T_KW_ERROR: return "error";
    case T_KW_VOID: return "void";
    case T_KW_BYTE: return "byte";
    case T_KW_CHAR: return "char";
    case T_KW_INT: return "int";
    case T_KW_UINT: return "uint";
    case T_KW_FLOAT: return "float";
    case T_KW_BOOL: return "bool";
    case T_KW_STRING: return "string";
    case T_KW_BYTES: return "bytes";
    case T_KW_TRUE: return "true";
    case T_KW_FALSE: return "false";

    case T_HASH: return "#";
    case T_LPAREN: return "(";
    case T_RPAREN: return ")";
    case T_LBRACKET: return "[";
    case T_RBRACKET: return "]";
    case T_LBRACE: return "{";
    case T_RBRACE: return "}";
    case T_DOT: return ".";
    case T_COLON: return ":";

    case T_OP_ASSIGN: return "=";
    case T_OP_DEFINE: return ":=";
    case T_OP_SWAP: return "<>";
    case T_OP_AMP: return "&";
    case T_OP_PIPE: return "|";
    case T_OP_CARET: return "^";
    case T_OP_TILDE: return "~";
    case T_OP_PLUS: return "+";
    case T_OP_MINUS: return "-";
    case T_OP_STAR: return "*";
    case T_OP_SLASH: return "/";
    case T_OP_PERCENT: return "%";
    case T_OP_SHL: return "<<";
    case T_OP_SHR: return ">>";
    case T_OP_USHR: return ">>>";
    case T_OP_SHL_CY: return "<<~";
    case T_OP_SHR_CY: return ">>~";
    case T_OP_AND: return "&&";
    case T_OP_OR: return "||";
    case T_BANG: return "!";
    case T_OP_QMARK: return "?";
    case T_OP_QQ: return "??";
    case T_OP_LT: return "<";
    case T_OP_GT: return ">";
    case T_OP_LE: return "<=";
    case T_OP_GE: return ">=";
    case T_OP_EQ: return "==";
    case T_OP_NE: return "!=";
    case T_OP_RANGE_LE: return "..=";
    case T_OP_RANGE_LT: return "..<";
    case T_OP_ARROW: return "->";
    case T_OP_FATARROW: return "=>";
    case T_OP_RECV: return "<-";
    case T_OP_ADD_ASSIGN: return "+=";
    case T_OP_SUB_ASSIGN: return "-=";
    case T_OP_MUL_ASSIGN: return "*=";
    case T_OP_DIV_ASSIGN: return "/=";
    case T_OP_MOD_ASSIGN: return "%=";
    case T_OP_SHL_ASSIGN: return "<<=";
    case T_OP_SHR_ASSIGN: return ">>=";
    case T_OP_AND_ASSIGN: return "&=";
    case T_OP_OR_ASSIGN: return "|=";
    case T_OP_XOR_ASSIGN: return "^=";
    case T_OP_INC: return "++";
    case T_OP_DEC: return "--";

    case T_KIND_COUNT: break;
  }
  return "?";
}