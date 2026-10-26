#include "typetab.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

static Type *typetab_grow(TypeTable *tt) {
  if (tt->len >= tt->cap) {
    size_t ncap = tt->cap ? tt->cap * 2 : 64;
    Type *n = realloc(tt->types, ncap * sizeof(Type));
    assert(n);
    tt->types = n;
    tt->cap = ncap;
  }
  return &tt->types[tt->len++];
}

static void init_predefined_types(TypeTable *tt) {
  /* TYPE_NONE_ID */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_NONE;
    t->name = "none";
    TypeNameMap_put(tt->by_name, "none", (TypeId)(tt->len - 1));
  }
  /* TYPE_UNKNOWN_ID */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_ANY;
    t->name = "unknown";
    TypeNameMap_put(tt->by_name, "unknown", (TypeId)(tt->len - 1));
  }
  /* int */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_INT;
    t->name = "int";
    t->flags.is_immutable = true;
    t->flags.is_owning = true;
    t->object_size = 8;
    TypeNameMap_put(tt->by_name, "int", (TypeId)(tt->len - 1));
  }
  /* uint */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_UINT;
    t->name = "uint";
    t->flags.is_immutable = true;
    t->flags.is_owning = true;
    t->flags.is_unsigned = true;
    t->object_size = 8;
    TypeNameMap_put(tt->by_name, "uint", (TypeId)(tt->len - 1));
  }
  /* float */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_FLOAT;
    t->name = "float";
    t->object_size = 8;
    TypeNameMap_put(tt->by_name, "float", (TypeId)(tt->len - 1));
  }
  /* bool */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_BOOL;
    t->name = "bool";
    t->object_size = 1;
    TypeNameMap_put(tt->by_name, "bool", (TypeId)(tt->len - 1));
  }
  /* char */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_CHAR;
    t->name = "char";
    t->object_size = 4;
    TypeNameMap_put(tt->by_name, "char", (TypeId)(tt->len - 1));
  }
  /* byte */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_BYTE;
    t->name = "byte";
    t->object_size = 1;
    TypeNameMap_put(tt->by_name, "byte", (TypeId)(tt->len - 1));
  }
  /* string */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_STRING;
    t->name = "string";
    t->object_size = 8 * 2;
    TypeNameMap_put(tt->by_name, "string", (TypeId)(tt->len - 1));
  }
  /* bytes */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_BYTES;
    t->name = "bytes";
    t->object_size = 8 * 2;
    TypeNameMap_put(tt->by_name, "bytes", (TypeId)(tt->len - 1));
  }
  /* void */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_VOID;
    t->name = "void";
    t->object_size = 0;
    TypeNameMap_put(tt->by_name, "void", (TypeId)(tt->len - 1));
  }
  /* type */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_TYPE;
    t->name = "type";
    t->object_size = 8;
    TypeNameMap_put(tt->by_name, "type", (TypeId)(tt->len - 1));
  }
  /* any */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_ANY;
    t->name = "any";
    TypeNameMap_put(tt->by_name, "any", (TypeId)(tt->len - 1));
  }
}

Type *typetab_get(TypeTable *tt, TypeId id) {
  if (id >= tt->len) return NULL;
  return &tt->types[id];
}

size_t typetab_render_type_buf(TypeTable *tt, TypeId typeId, char buf[], size_t buf_len) {
  assert(buf != NULL && buf_len > 0);
  Type *t = typetab_get(tt, typeId);
  char *it = buf;
  #define BUF_BYTES_LEFT (buf_len - (it - buf))
  if (t == NULL) {
    static const char not_found_type[] = "<not found>";
    snprintf(buf, BUF_BYTES_LEFT, "%s", not_found_type);
    return sizeof(not_found_type) - 1;
  }
  switch(t->base) {
    case TB_INT:
    case TB_UINT:
    case TB_FLOAT:
    case TB_BOOL:
      snprintf(buf, BUF_BYTES_LEFT, "%s", t->name);
      return strlen(t->name);
    case TB_FUNC:
      static const char func_prefix[] = "func(";
      static const char comma_spacer[] = ",";
      static const char closing_parent[] = ")";

      snprintf(it, BUF_BYTES_LEFT, func_prefix);
      it += sizeof(func_prefix) - 1;

      for (uint32_t i = 0; i < t->props.func.param_count; i++) {
        struct TypeParam *p = &t->props.func.params[i];
        it += typetab_render_type_buf(tt, p->type, it, BUF_BYTES_LEFT);
        if (i + 1 < t->props.func.param_count) {
          snprintf(it, BUF_BYTES_LEFT, comma_spacer);
          it += sizeof(comma_spacer) - 1;
        }
      }

      snprintf(it, BUF_BYTES_LEFT, closing_parent);
      it += sizeof(closing_parent) - 1;

      if (t->props.func.return_type != 0) {
        it += typetab_render_type_buf(tt, t->props.func.return_type, it, BUF_BYTES_LEFT);
      }
      return it - buf;
    default:
      static const char undefined_type[] = "<undefined>";
      snprintf(buf, BUF_BYTES_LEFT, "%s", undefined_type);
      return sizeof(undefined_type) - 1;
  }
}

const char *typetab_render_type(TypeTable *tt, TypeId typeId) {
  static char buf[128] = {};
  typetab_render_type_buf(tt, typeId, buf, 128);
  return buf;
}

static void init_func_types(TypeTable *tt) {
  /* declare function Int -> Int -> Int */
  {
    TypeId int_type_id = typetab_find_by_name(tt, "int");
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_FUNC;
    t->name = "f(i,i)i";
    t->props.func.param_count = 2;
    t->props.func.params = malloc(t->props.func.param_count * sizeof(*t->props.func.params));
    t->props.func.params[0] = (TypeParam) {.type = int_type_id, .default_expr = NULL};
    t->props.func.params[1] = (TypeParam) {.type = int_type_id, .default_expr = NULL};
    t->props.func.return_type = int_type_id;
    TypeNameMap_put(tt->by_name, t->name, (TypeId)(tt->len - 1));
  }
}

TypeTable *typetab_new(void) {
  TypeTable *tt = calloc(1, sizeof(TypeTable));
  assert(tt);
  tt->by_name = TypeNameMap_new(NULL);
  init_predefined_types(tt);
  init_func_types(tt);
  return tt;
}

// every type name has one-to-one mappring to type id
TypeId typetab_find_by_name(TypeTable *tt, const char* type_name) {
  assert(tt != NULL);
  return TypeNameMap_get(tt->by_name, type_name);
}

void typetab_free(TypeTable *tt) {
  if (tt == NULL) return;
  for (size_t i = 0; i < tt->len; i++) {
    struct Type t = tt->types[i];
    // TODO: free default_expr or delete the field
    if (t.base == TB_FUNC) free(t.props.func.params);
  }
  if (tt->types) free(tt->types);
  if (tt->by_name) TypeNameMap_free(tt->by_name);
  free(tt);
}

FuncTable *functab_new(TypeTable *tt) {
  FuncTable *ft = malloc(sizeof(FuncTable));
  ft->by_name = TypeNameMap_new(NULL);
  ft->by_qualified = NULL;

  TypeId op_plus_type_id = typetab_find_by_name(tt, "f(i,i)i");
  TypeNameMap_put(ft->by_name, "operator+", op_plus_type_id);
  return ft;
}

// TODO: we could find many functions with the same name and diffetent types
TypeId functab_find(FuncTable *ft, const char *func_name) {
  return TypeNameMap_get(ft->by_name, func_name);
}

void functab_free(FuncTable *ft) {
  if (ft == NULL) return;
  if (ft->by_name != NULL) TypeNameMap_free(ft->by_name);
  if (ft->by_qualified != NULL) TypeNameMap_free(ft->by_qualified);
  free(ft);
}
