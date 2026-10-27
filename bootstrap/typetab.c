#include "typetab.h"
#include <stddef.h>
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

static inline void init_predefined_types(Environment*env) {
  struct TypeTable *tt = &env->type_table;
  /* TYPE_NONE_ID */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_NONE;
    t->name = "none";
    TypeByNameMap_put(tt->by_name, "none", (TypeId)(tt->len - 1));
  }
  /* TYPE_UNKNOWN_ID */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_ANY;
    t->name = "unknown";
    TypeByNameMap_put(tt->by_name, "unknown", (TypeId)(tt->len - 1));
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
    t->flags.is_primitive = true;
    TypeByNameMap_put(tt->by_name, "int", (TypeId)(tt->len - 1));
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
    t->flags.is_primitive = true;
    TypeByNameMap_put(tt->by_name, "uint", (TypeId)(tt->len - 1));
  }
  /* float */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_FLOAT;
    t->name = "float";
    t->object_size = 8;
    t->flags.is_primitive = true;
    TypeByNameMap_put(tt->by_name, "float", (TypeId)(tt->len - 1));
  }
  /* bool */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_BOOL;
    t->name = "bool";
    t->object_size = 1;
    t->flags.is_primitive = true;
    TypeByNameMap_put(tt->by_name, "bool", (TypeId)(tt->len - 1));
  }
  /* char */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_CHAR;
    t->name = "char";
    t->object_size = 4;
    t->flags.is_primitive = true;
    TypeByNameMap_put(tt->by_name, "char", (TypeId)(tt->len - 1));
  }
  /* byte */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_BYTE;
    t->name = "byte";
    t->object_size = 1;
    t->flags.is_primitive = true;
    TypeByNameMap_put(tt->by_name, "byte", (TypeId)(tt->len - 1));
  }
  /* string */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_STRING;
    t->name = "string";
    t->object_size = 8 * 2;
    TypeByNameMap_put(tt->by_name, "string", (TypeId)(tt->len - 1));
  }
  /* bytes */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_BYTES;
    t->name = "bytes";
    t->object_size = 8 * 2;
    TypeByNameMap_put(tt->by_name, "bytes", (TypeId)(tt->len - 1));
  }
  /* void */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_VOID;
    t->name = "void";
    t->object_size = 0;
    t->flags.is_primitive = true;
    TypeByNameMap_put(tt->by_name, "void", (TypeId)(tt->len - 1));
  }
  /* type */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_TYPE;
    t->name = "type";
    t->object_size = 8;
    TypeByNameMap_put(tt->by_name, "type", (TypeId)(tt->len - 1));
  }
  /* any */
  {
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_ANY;
    t->name = "any";
    TypeByNameMap_put(tt->by_name, "any", (TypeId)(tt->len - 1));
  }
}

Type *env_get_type_by_id(Environment *env, TypeId id) {
  TypeTable *tt = &env->type_table;
  if (id >= tt->len) return NULL;
  return &tt->types[id];
}

size_t env_render_type_buf(Environment *env, TypeId typeId, char buf[], size_t buf_len) {
  assert(buf != NULL && buf_len > 0);
  Type *t = env_get_type_by_id(env, typeId);
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
        it += env_render_type_buf(env, p->type, it, BUF_BYTES_LEFT);
        if (i + 1 < t->props.func.param_count) {
          snprintf(it, BUF_BYTES_LEFT, comma_spacer);
          it += sizeof(comma_spacer) - 1;
        }
      }

      snprintf(it, BUF_BYTES_LEFT, closing_parent);
      it += sizeof(closing_parent) - 1;

      if (t->props.func.return_type != 0) {
        it += env_render_type_buf(env, t->props.func.return_type, it, BUF_BYTES_LEFT);
      }
      return it - buf;
    default:
      static const char undefined_type[] = "<undefined>";
      snprintf(buf, BUF_BYTES_LEFT, "%s", undefined_type);
      return sizeof(undefined_type) - 1;
  }
}

const char *env_render_type(Environment *env, TypeId typeId) {
  static char buf[128] = {};
  env_render_type_buf(env, typeId, buf, 128);
  return buf;
}

static void init_func_types(Environment *env) {
  struct TypeTable *tt = &env->type_table;
  /* declare function Int -> Int -> Int */
  {
    TypeId int_type_id = env_find_typeid_by_name(env, "int");
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_FUNC;
    t->name = "f(i,i)i";
    t->props.func.param_count = 2;
    t->props.func.params = malloc(t->props.func.param_count * sizeof(*t->props.func.params));
    t->props.func.params[0] = (TypeParam) {.type = int_type_id, .default_expr = NULL};
    t->props.func.params[1] = (TypeParam) {.type = int_type_id, .default_expr = NULL};
    t->props.func.return_type = int_type_id;
    TypeByNameMap_put(tt->by_name, t->name, (TypeId)(tt->len - 1));
  }
  {
    TypeId int_type_id = env_find_typeid_by_name(env, "int");
    TypeId bool_type_id = env_find_typeid_by_name(env, "bool");
    Type *t = typetab_grow(tt);
    memset(t, 0, sizeof(Type));
    t->base = TB_FUNC;
    t->name = "f(i,i)b";
    t->props.func.param_count = 2;
    t->props.func.params = malloc(t->props.func.param_count * sizeof(*t->props.func.params));
    t->props.func.params[0] = (TypeParam) {.type = int_type_id, .default_expr = NULL};
    t->props.func.params[1] = (TypeParam) {.type = int_type_id, .default_expr = NULL};
    t->props.func.return_type = bool_type_id;
    TypeByNameMap_put(tt->by_name, t->name, (TypeId)(tt->len - 1));
  }
}

// every type name has one-to-one mappring to type id
TypeId env_find_typeid_by_name(Environment *env, const char* type_name) {
  TypeTable *tt = &env->type_table;
  assert(tt != NULL);
  return TypeByNameMap_get(tt->by_name, type_name);
}

static inline TypeIdVec *create_TypeId_vector(const char*) {
  return TypeIdVec_new(1, NULL);
}

static inline void create_operator(FuncTable *ft, const char* funct_name, TypeId type) {
  TypeIdVec *all_accociated_types = TypesOfNameMap_compute_if_absent(ft->by_name, funct_name, create_TypeId_vector);
  TypeIdVec_append(all_accociated_types, type);
}

static void init_func_table(Environment* env) {
  FuncTable *ft = &env->func_table;
  TypeId func_int_int_int_type_id = env_find_typeid_by_name(env, "f(i,i)i");
  create_operator(ft, "operator+", func_int_int_int_type_id);
  create_operator(ft, "operator-", func_int_int_int_type_id);
  create_operator(ft, "operator/", func_int_int_int_type_id);
  create_operator(ft, "operator*", func_int_int_int_type_id);

  TypeId func_int_int_bool_type_id = env_find_typeid_by_name(env, "f(i,i)b");
  create_operator(ft, "operator<", func_int_int_bool_type_id);
  create_operator(ft, "operator>", func_int_int_bool_type_id);
}

TypeIdVec *env_find_func_types_by_name(Environment *env, const char *func_name) {
  return TypesOfNameMap_get(env->func_table.by_name, func_name);
}

// find the first matching type of the function with given name
// the FuncTable must not contains duplicating TypeId for one name
// TODO: hset (hash set)?
TypeId env_find_func_type_name_n_params(Environment *env, const char *func_name, uint32_t argc, TypeId param_type_ids[]) {
  FuncTable *ft = &env->func_table;
  TypeIdVec *all_accociated_types = TypesOfNameMap_get(ft->by_name, func_name);
  if (all_accociated_types == NULL) return TYPE_NONE_ID;
  size_t len = TypeIdVec_len(all_accociated_types);

  for (size_t i = 0; i < len; i++) {
    TypeId func_type_id = all_accociated_types->values[i];
    Type *func_type = env_get_type_by_id(env, func_type_id);
    if (func_type->base != TB_FUNC) continue;
    if (func_type->props.func.param_count == argc) {
      for (uint32_t j = 0; j < argc; j++)
        if (func_type->props.func.params[j].type != param_type_ids[j]) goto next_iteration;
      // all types matched
      return func_type_id;
    }
  next_iteration:
  }
  return TYPE_NONE_ID;
}

Environment *env_new(void) {
  Environment *env = malloc(sizeof(Environment));
  env->type_table.len = 0;
  env->type_table.cap = 0;
  env->type_table.types = NULL;
  env->type_table.by_name = TypeByNameMap_new(NULL);

  env->func_table.by_name = TypesOfNameMap_new(TypeIdVec_free);
  env->func_table.by_qualified = NULL;

  init_predefined_types(env);
  init_func_types(env);
  init_func_table(env);

  return env;
}

void env_free(Environment *env) {
  if (env == NULL) return;

  TypeTable *tt = &env->type_table;
  FuncTable *ft = &env->func_table;

  for (size_t i = 0; i < tt->len; i++) {
    struct Type t = tt->types[i];
    // TODO: free default_expr or delete the field
    if (t.base == TB_FUNC) free(t.props.func.params);
  }
  if (tt->types) free(tt->types);
  if (tt->by_name) TypeByNameMap_free(tt->by_name);

  if (ft->by_name != NULL) TypesOfNameMap_free(ft->by_name);
  if (ft->by_qualified != NULL) TypesOfNameMap_free(ft->by_qualified);
  free(env);
}

