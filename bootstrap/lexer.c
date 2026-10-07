#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "lexer.h"
#include "reader.h"
#include "tokens.h"

// private functions

static Token *process_word(size_t line, size_t col, char *buf, size_t buf_size);
static bool could_be_operator(char *buf, size_t buf_size, char next);
static void build_ranges(void);

// public methods

#define RESERVED_WORD_RANGE_LEN 256
typedef struct { unsigned short start, end; } Range;
static Range chr_range[RESERVED_WORD_RANGE_LEN];            // indexed by (unsigned char)buf[0]

struct Lexer *lexer_new(FILE *src) {
  struct Lexer *lexer = malloc(sizeof(Lexer));
  if (lexer == NULL) return NULL;

  memset(lexer->error, '\0', sizeof lexer->error);
  lexer->line = 1;
  lexer->col = 1;
  lexer->reader = NULL;
  lexer->steps = NULL;
  lexer->steps_len = 0;
  lexer->steps_cap = 0;
  lexer->steps_pos = 0;
  lexer->at_eof = false;

  struct Reader *reader = new_reader(src);
  if (reader == NULL) {
    sprintf(lexer->error, "reader is NULL");
    return lexer;
  }
  if (strlen(reader->error) != 0) {
    sprintf(lexer->error, "reading error: %s\n", reader->error);
    reader_close(reader);
  } else
    lexer->reader = reader;

  build_ranges();
  return lexer;
}

static bool lexer_log_grow(struct Lexer *lexer) {
  if (lexer->steps_len < lexer->steps_cap) return true;

  size_t new_cap = lexer->steps_cap == 0 ? 64 : lexer->steps_cap * 2;
  struct LexStep *grown = realloc(lexer->steps, new_cap * sizeof *grown);
  if (grown == NULL) {
    sprintf(lexer->error, "out of memory growing the token log");
    return false;
  }

  lexer->steps = grown;
  lexer->steps_cap = new_cap;
  return true;
}

static bool lexer_log_push(struct Lexer *lexer, struct Token *tok) {
  if (!lexer_log_grow(lexer)) return false;

  lexer->steps[lexer->steps_len].tok = tok;
  lexer->steps[lexer->steps_len].failed = false;
  lexer->steps[lexer->steps_len].line = lexer->line;
  lexer->steps[lexer->steps_len].col = lexer->col;
  lexer->steps_len += 1;
  return true;
}

static void lexer_log_seal(struct Lexer *lexer, bool replayed) {
  if (replayed) return;
  assert(lexer->steps_len > 0);
  lexer->steps[lexer->steps_len - 1].line = lexer->line;
  lexer->steps[lexer->steps_len - 1].col = lexer->col;
}

static void lexer_log_mark_failed(struct Lexer *lexer) {
  assert(lexer->steps_len > 0);
  lexer->steps[lexer->steps_len - 1].failed = true;
}

// Append a token the Parser invents rather than lexes -- the newline a block
// comment stands for -- so that a rewind replays it like any other.
static enum LexState lexer_log_token(struct Lexer *lexer, struct Token **tok) {
  if (!lexer_log_push(lexer, *tok)) {
    if (*tok != NULL) free(*tok);
    *tok = NULL;
    return LEX_ERROR;
  }
  lexer->steps_pos = lexer->steps_len;
  return LEX_OK;
}

size_t lexer_mark(const struct Lexer *lexer) {
  return lexer->steps_pos;
}

void lexer_rewind_to(struct Lexer *lexer, size_t mark) {
  assert(mark <= lexer->steps_len);
  lexer->steps_pos = mark;
}

enum LexState lexer_next_significant(struct Lexer *lexer, struct Token **tok) {
  assert(lexer != NULL);
  *tok = NULL;
  if (lexer->at_eof && lexer->steps_pos >= lexer->steps_len) {
    assert(lexer->steps_len > 0);
    *tok = lexer->steps[lexer->steps_len - 1].tok;
    return LEX_EOF;
  }

  while (true) {
    bool replayed = lexer->steps_pos < lexer->steps_len;
    if (replayed) {
      struct LexStep *step = &lexer->steps[lexer->steps_pos++];
      *tok = step->tok;

      // this line and col are not like in token, col points after the token
      lexer->line = step->line;
      lexer->col = step->col;

      if (step->failed) return LEX_ERROR;

      if ((*tok)->kind == TOK_UNDEF) { // the end-of-file token
        lexer->at_eof = true;
        return LEX_EOF;
      }
    } else {
      enum LexState lex_state = lexer_next_token(lexer, tok);
      if (lex_state == LEX_ERROR) {
        if (*tok != NULL) free(*tok);
        *tok = NULL;
        return LEX_ERROR;
      }
      if (lex_state == LEX_EOF) {
        lexer->at_eof = true;
        if (lexer_log_token(lexer, tok) == LEX_ERROR) return LEX_ERROR;
        lexer_log_seal(lexer, replayed);
        return LEX_EOF;
      }
      if (lexer_log_token(lexer, tok) == LEX_ERROR) return LEX_ERROR;
    }

    if ((*tok)->kind == TOK_SLC_START) {
      if (!replayed) {
        if (lexer_skip_line(lexer) == LEX_ERROR) {
          lexer_log_mark_failed(lexer);
          return LEX_ERROR;
        }
        lexer_log_seal(lexer, replayed);
      }
      continue;
    }

    if ((*tok)->kind == TOK_MLC_START) {
      if (!replayed) {
        if (lexer_skip_until(lexer, "*/") == LEX_ERROR) {
          lexer_log_mark_failed(lexer);
          return LEX_ERROR;
        }
        struct Token *nl = token_new(TOK_NL, lexer->line, lexer->col);
        if (lexer_log_token(lexer, &nl) == LEX_ERROR) return LEX_ERROR;
        *tok = nl;
      } else {
        assert(lexer->steps_pos < lexer->steps_len);
        *tok = lexer->steps[lexer->steps_pos++].tok;
        assert((*tok)->kind == TOK_NL);
        lexer->line = lexer->steps[lexer->steps_pos - 1].line;
        lexer->col = lexer->steps[lexer->steps_pos - 1].col;
      }
      lexer_log_seal(lexer, replayed);
      continue;
    }

    if ((*tok)->kind == TOK_SPACE) {
      lexer_log_seal(lexer, replayed);
      continue;
    }
    lexer_log_seal(lexer, replayed);
    return LEX_OK;
  }
}

enum LexState lexer_next_token(struct Lexer *lexer, struct Token **tok) {
  assert(lexer != NULL);

  // bootstrap compiler cannot parse names and string literals longer than TOKEN_VALUE_MAX_SIZE chars
  char buf[TOKEN_VALUE_MAX_SIZE] = {0};
  size_t buf_idx = 0;

  const size_t line = lexer->line;
  const size_t col = lexer->col;

  typedef enum WatchState {
    WS_START,
    WS_WORD,
    WS_DIG,
    WS_OP,
    WS_STRING,
  } WatchState;

  WatchState state = WS_START;

  while (true) {
    if (buf_idx >= sizeof buf) {
      sprintf(lexer->error, "too long identifier: %ld, max %d", buf_idx, TOKEN_VALUE_MAX_SIZE);
      return LEX_ERROR;
    }

    char c = '\0';
    bool read = reader_getch(lexer->reader, &c);
    if (!read && lexer->reader->error[0] != '\0') {
      // if no read and error present then error, else end of file
      sprintf(lexer->error, "faild lexing next token: %s", lexer->reader->error);
      return LEX_ERROR;
    }

    lexer->col += 1;

    bool is_alpha = isalpha(c) || c == '_';
    bool is_digit = isdigit(c);
    bool is_punct = !is_alpha && !is_digit && read;

    switch (state) {
    case WS_START:
      if (!read) {
        // hand back a fresh token: the caller stores the pointer and may rewind
        // onto it, so *tok must never keep the previous token's value
        *tok = token_new(TOK_UNDEF, line, col);
        return LEX_EOF;
      }
      if (c == '"') {
        state = WS_STRING;
        break;
      }

      buf[buf_idx++] = c;
      if (is_alpha)
        state = WS_WORD;
      else if (is_digit)
        state = WS_DIG;
      else if (is_punct)
        state = WS_OP;
      break;
    case WS_WORD:
      if (is_alpha || is_digit)
        buf[buf_idx++] = c;
      else {
        reader_ungetch(lexer->reader, c);
        lexer->col -= 1;
        *tok = process_word(line, col, buf, buf_idx);
        return LEX_OK;
      }
      break;
    case WS_DIG:
      if (is_digit)
        buf[buf_idx++] = c;
      else {
        reader_ungetch(lexer->reader, c);
        lexer->col -= 1;
        *tok = token_new_v(TOK_NUMBER_L, line, col, buf, buf_idx);
        return LEX_OK;
      }
      break;
    case WS_OP:
      if (is_punct && could_be_operator(buf, buf_idx, c))
        buf[buf_idx++] = c;
      else {
        reader_ungetch(lexer->reader, c);
        lexer->col -= 1;
        *tok = process_word(line, col, buf, buf_idx);
        if ((*tok)->kind == TOK_NL) {
          lexer->line += 1;
          lexer->col = 1;
        }
        return LEX_OK;
      }
      break;
    case WS_STRING:
      if (c == '"') {
        *tok = token_new_v(TOK_STRING_L, line, col, buf, buf_idx);
        return LEX_OK;
      } else buf[buf_idx++] = c;
      if (c == '\n') {
        lexer->line += 1;
        lexer->col = 1;
      }
      break;
    default:
      sprintf(lexer->error, "lexer parse state is undefined");
      return LEX_ERROR;
    }
  }

  sprintf(lexer->error, "unexpected lexer condition");
  return LEX_ERROR;
}

enum LexState lexer_skip_line(struct Lexer *lexer) {
  assert(lexer != NULL);

  while (true) {
    char c = '\0';
    bool read = reader_getch(lexer->reader, &c);
    if (!read && lexer->reader->error[0] != '\0') {
      sprintf(lexer->error, "failed lexing line comment: %s", lexer->reader->error);
      return LEX_ERROR;
    }

    if (!read) return LEX_OK; // EOF before the newline

    if (c == '\n') {
      reader_ungetch(lexer->reader, c);
      return LEX_OK;
    }

    lexer->col += 1;
  }
}

enum LexState lexer_skip_until(struct Lexer *lexer, const char *anchor) {
  assert(lexer != NULL && anchor != NULL);

  typedef enum WatchState {
    WS_SKIPPING,
    WS_STOPPING,
  } WatchState;

  size_t anchor_ith = 0;
  WatchState state = WS_SKIPPING;
  while (true) {
    char c = '\0';
    bool read = reader_getch(lexer->reader, &c);
    if (!read && lexer->reader->error[0] != '\0') {
      // if no read and error present then error, else end of file
      sprintf(lexer->error, "faild lexing skip: %s", lexer->reader->error);
      return LEX_ERROR;
    }

    if (!read) { // EOF
      sprintf(lexer->error, "skip failed to reach anchor %s", anchor);
      return LEX_ERROR;
    }

    lexer->col += 1;

    switch (state) {
    case WS_SKIPPING:
      if (c == '\n') {
        lexer->line += 1;
        lexer->col = 1;
      }
      if (c == anchor[0]) {
        anchor_ith = 0;
        state = WS_STOPPING;
      }
      break;
    case WS_STOPPING:
      anchor_ith++;
      if (c != anchor[anchor_ith]) {
        if (anchor[anchor_ith] == '\0') {
          reader_ungetch(lexer->reader, c);
          lexer->col -= 1;
          return LEX_OK;
        } else { // that was not the anchor in parsed text
          anchor_ith = 0;
          state = WS_SKIPPING;
        }
      }
      if (c == '\n') {
        lexer->line += 1;
        lexer->col = 1;
      }
      break;
    default:
      sprintf(lexer->error, "lexer skip state is undefined");
      return LEX_ERROR;
    }
  }
  return LEX_OK;
}

void lexer_close(struct Lexer *lexer) {
  if (lexer == NULL) return;

  for (size_t i = 0; i < lexer->steps_len; i++) free(lexer->steps[i].tok);
  free(lexer->steps);

  reader_close(lexer->reader);
  free(lexer);
}

Token *process_word(size_t line, size_t col, char *buf, size_t buf_size) {
  unsigned short c = (unsigned char)buf[0];
  if (c < RESERVED_WORD_RANGE_LEN) {
    for (size_t i = chr_range[c].start; i < chr_range[c].end; i++) {
      const TokenDef *d = &TOKEN_KEYWORDS[i];
      if (d->len < buf_size) break;                          // sorted desc: nothing later can match
      if (d->len == buf_size && memcmp(buf, d->letters, buf_size) == 0) {
        if (d->kind == TOK_TRUE || d->kind == TOK_FALSE)
          return token_new_v(TOK_BOOL_L, line, col, buf, buf_size);
        else
          return token_new(d->kind, line, col);
      }
    }
  }
  return token_new_v(TOK_ID, line, col, buf, buf_size);
}

bool could_be_operator(char *buf, size_t buf_size, char next) {
  char tmp[8] = {0}; // now max size of operator is 4 chars + 1 for \0
  memcpy(tmp, buf, buf_size);
  tmp[buf_size] = next;
  size_t tmp_size = buf_size + 1;

  unsigned short c = (unsigned char)tmp[0];
  if (c < RESERVED_WORD_RANGE_LEN) {
    for (size_t i = chr_range[c].start; i < chr_range[c].end; i++) {
      const TokenDef *d = &TOKEN_KEYWORDS[i];
      if (d->len < tmp_size) break;
      if (d->len == tmp_size && memcmp(tmp, d->letters, tmp_size) == 0) {
        return TOK_OPERATOR_BEGIN < d->kind && d->kind < TOK_OPERATOR_END;
      }
    }
  }
  return false;
}

static void build_ranges(void) {
  for (size_t i = 0; i < RESERVED_WORD_RANGE_LEN; i++) { chr_range[i].start = TOKEN_KEYWORDS_COUNT; chr_range[i].end = 0; }
  for (size_t i = 0; i < TOKEN_KEYWORDS_COUNT; i++) {
    size_t c = (unsigned char)TOKEN_KEYWORDS[i].letters[0];
    if (chr_range[c].start > i) chr_range[c].start = i;
    if (chr_range[c].end   < i + 1) chr_range[c].end = i + 1;
  }
}

