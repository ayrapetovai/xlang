#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "tokens.h"
#include "lexer.h"
#include "reader.h"

struct Lexer* new_lexer(struct Reader* reader) {
  if (reader == NULL)
    return NULL;
  struct Lexer *lexer = malloc(sizeof(Lexer));
  if (lexer == NULL)
    return NULL;
  lexer->reader = reader;
  lexer->line = 1;
  lexer->col = 1;
  memset(lexer->error, 0, sizeof lexer->error);
  return lexer;
}

bool strcmplen(const char* s, const char* t, size_t *size) {
  const char *t_start = t;
  while (*s != '\0' && *t != '\0' && *s == *t) {
    s++;
    t++;
  }
  bool are_equal = *s == '\0' && *t == '\0';
  while (*t++ != '\0');
  *size = t - t_start;
  return are_equal;
}

void process_word(char* buf, size_t buf_size, struct Token* tok) {
  int found_word_idx = -1;
  for (size_t i = 0; i < TOKEN_KEYWORDS_COUNT; i++) {
    size_t keyword_len;
    if (strcmplen(buf, TOKEN_KEYWORDS[i].letters, &keyword_len)) {
      found_word_idx = i;
      if (buf_size > keyword_len) break;
    }
  }
  if (found_word_idx < 0) {
    // word is not found in keywrds, it is an identifier
    tok->kind = TOK_ID;
    strcpy(tok->value, buf);
  } else {
    tok->kind = TOKEN_KEYWORDS[found_word_idx].kind;
  }
}

void process_number(char* buf, struct Token* tok) {
  tok->kind = TOK_INT_L;
  sprintf(tok->value, "%s", buf);
}

enum LexState lexer_next_token(struct Lexer* lexer, struct Token* tok) {
  if (lexer == NULL || tok == NULL) return LEX_PRG_ERROR;

  // bootstrap compiler cannot parse names longer than 64 chars
  char buf[64] = {0};
  size_t buf_idx = 0;

  tok->line = lexer->line;
  tok->col = lexer->col;

  typedef enum WatchState {
    WS_START,
    WS_WORD,
    WS_DIG,
    WS_OP,
  } WatchState;

  WatchState state = WS_START;

  while (true) {
    if (buf_idx > sizeof buf) {
      sprintf(lexer->error, "too long identifier: %ld", buf_idx);
      return LEX_ERROR;
    }

    char c = '\0';
    bool read = reader_getch(lexer->reader, &c);
    if (!read && lexer->reader->error[0] != '\0') {
      // if no read and error present then error, else end of file
      sprintf(lexer->error, "faild lexing: %s", lexer->reader->error);
      return LEX_ERROR;
    }

    lexer->col += 1;

    bool is_alpha = isalpha(c) || c == '_';
    bool is_digit = isdigit(c);
    bool is_punct = !is_alpha && !is_digit && read;

    switch (state) {
      case WS_START:
        if (!read) return LEX_EOF;
        buf[buf_idx++] = c;
        if (is_alpha) state = WS_WORD;
        if (is_digit) state = WS_DIG;
        if (is_punct) state = WS_OP;
        break;
      case WS_WORD:
        if (is_alpha) buf[buf_idx++] = c;
        else {
          reader_ungetch(lexer->reader, c);
          lexer->col -= 1;
          process_word(buf, buf_idx, tok);
          return LEX_OK;
        }
        break;
      case WS_DIG:
        if (is_digit) buf[buf_idx++] = c;
        else {
          reader_ungetch(lexer->reader, c);
          lexer->col -= 1;
          process_number(buf, tok);
          return LEX_OK;
        }
        break;
      case WS_OP:
        if (is_punct && c != '\n' && c != '.') buf[buf_idx++] = c;
        else {
          reader_ungetch(lexer->reader, c);
          lexer->col -= 1;
          process_word(buf, buf_idx, tok);
          if (tok->kind == TOK_NL) {
            lexer->line += 1;
            lexer->col = 1;
          }
          return LEX_OK;
        }
        break;
      default:
        sprintf(lexer->error, "lexer state is undefined");
        return LEX_ERROR;
    }
  }

  sprintf(lexer->error, "unexpected lexer condition");
  return LEX_ERROR;
}

void lexer_close(struct Lexer* lexer) {
  if (lexer == NULL) return;
  reader_close(lexer->reader);
  free(lexer);
}

void print_char(char c) {
  printf("0x%x ", c);
  if (c != '\n') {
    putchar(c);
  } else {
    printf("\\n");
  }
  putchar('\n');
}

