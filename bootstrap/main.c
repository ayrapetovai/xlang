#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "ast.h"
#include "exec.h"
#include "lexer.h"
#include "logger.h"
#include "parser.h"
#include "tokens.h"

/*
Bootstrap compiler does not support:
- coroutins (longjump?)
- UTF-8
- libraries
- compile-time code execution
- panicless output
- processing commentaries (skip only)
- string interpolation
- floating point numbers
- custom memory allocators
*/

int run_exec(FILE* src) {
  struct Lexer *lexer = lexer_new(src);
  if (strlen(lexer->error) != 0) {
    fprintf(stderr, "lexer initialization: %s\n", lexer->error);
    lexer_close(lexer);
    return 1;
  }

  struct Parser* parser = parser_new(lexer);
  if (parser == NULL) {
    lexer_close(lexer);
    return 2;
  }
  if (strlen(parser->error) != 0) {
    fprintf(stderr, "parsing error: %s\n", parser->error);
    parser_close(parser);
    lexer_close(lexer);
    return 3;
  }

  ASTNode *parse_result = parser_parse(parser);
  if (parse_result == NULL) {
    fprintf(stderr, "execution failed: %s\n", parser->error);
    parser_close(parser);
    lexer_close(lexer);
    return 4;
  }

  parser_close(parser);
  lexer_close(lexer);

  int rc = exec(parse_result);
  node_free(parse_result);
  return rc;
}

int run_lex(FILE* src) {
  LOG_DEBUG("minimal token size %db", sizeof(Token));
  struct Lexer *lexer = lexer_new(src);
  if (strlen(lexer->error) != 0) {
    fprintf(stderr, "lexer initialization: %s\n", lexer->error);
    lexer_close(lexer);
    return 1;
  }
  int error_code = 0;

  size_t token_count = 0;
  size_t value_bytes = 0;
  while (true) {
    Token *t = NULL;
    enum LexState state = lexer_next_token(lexer, &t);
    if (state == LEX_EOF) {
      free(t);
      break;
    } else if (state == LEX_ERROR) {
      free(t);
      fprintf(stderr, "lexing error: %s\n", lexer->error);
      error_code = 2;
      break;
    }
    print_token(t);

    // skip commentaries
    switch (t->kind) {
    case TOK_SLC_START: lexer_skip_line(lexer); break;
    case TOK_MLC_START: lexer_skip_until(lexer, "*/"); break;
    default:
    }
    token_count++;
    value_bytes += strlen(t->value);
    free(t);
  }

  lexer_close(lexer);
  LOG_DEBUG("memory used for %d tokens %db", token_count, token_count * sizeof(Token) + value_bytes);
  return error_code;
}

int run_ast(FILE* src) {
  struct Lexer *lexer = lexer_new(src);
  if (strlen(lexer->error) != 0) {
    fprintf(stderr, "lexer initialization: %s\n", lexer->error);
    lexer_close(lexer);
    return 1;
  }

  struct Parser* parser = parser_new(lexer);
  if (parser == NULL) {
    lexer_close(lexer);
    return 2;
  }
  if (strlen(parser->error) != 0) {
    fprintf(stderr, "parser initialization: %s\n", parser->error);
    parser_close(parser);
    lexer_close(lexer);
    return 3;
  }

  ASTNode *parse_result = parser_parse(parser);
  if (parse_result == NULL) {
    fprintf(stderr, "parsing failed: %s\n", parser->error);
    parser_close(parser);
    lexer_close(lexer);
    return 4;
  }

  parser_close(parser);
  lexer_close(lexer);

  ast_dump(parse_result);
  node_free(parse_result);
  return 0;
}

typedef enum {
  RUN,
  AST,
  LEX,
} ProgramMode;

int main(int argc, char **argv) {
  logger_set_level(LOG_LEVEL_OFF);
  ProgramMode mode = RUN;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--log-level", argv[i]) == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "to few arguments, --log-level neads a value\n");
        return 1;
      }
      const char* level = argv[i + 1];
      if (strcmp("info", level) == 0)
        logger_set_level(LOG_LEVEL_INFO);
      else if (strcmp("debug", level) == 0)
        logger_set_level(LOG_LEVEL_DEBUG);
      else if (strcmp("warn", level) == 0)
        logger_set_level(LOG_LEVEL_WARN);
      else if (strcmp("error", level) == 0)
        logger_set_level(LOG_LEVEL_ERROR);
      else if (strcmp("fatal", level) == 0)
        logger_set_level(LOG_LEVEL_FATAL);
      else if (strcmp(argv[i], "off") != 0) {
        fprintf(stderr, "wrong argument for --log-level, expected: debug, info, warn, error, fatal, off\n");
        return 1;
      }
      i++;
    } else if (strcmp("--ast", argv[i]) == 0){
      mode = AST;
    } else if (strcmp("--lex", argv[i]) == 0){
      mode = LEX;
    } else if (strcmp("--", argv[i]) == 0 && strlen(argv[i]) == 2) {
      i++;
      break;
    } else {
      break;
    }
  }
  int last_param = i;
  
  if (last_param >= argc) {
    fprintf(stderr, "file name missed\n");
    return 1;
  }

  const char *filename = argv[last_param];

  FILE* src;
  if (strcmp(filename, "-") == 0) {
    src = stdin;
  } else {
    src = fopen(filename, "ra"); // read only as text
    if (src == NULL) {
      fprintf(stderr, "failed to open file %s: %s\n", filename, strerror(errno));
      return 1;
    }
  }

  switch (mode) {
    case RUN: return run_exec(src);
    case LEX: return run_lex(src);
    case AST: return run_ast(src);
    default:
      fprintf(stderr, "unknown mode\n");
      return 1;
  }
}
