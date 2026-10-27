#ifndef TYPETAB_H
#define TYPETAB_H

#include "hmap.h"
#include "vec.h"
#include <stddef.h>
#include <inttypes.h>
#include <stdbool.h>

typedef uint32_t TypeId;
struct ASTNode;

typedef enum {
  TB_NONE = 0,
  TB_INT,
  TB_UINT,
  TB_FLOAT,
  TB_BOOL,
  TB_CHAR,
  TB_BYTE,
  TB_STRING,
  TB_BYTES,
  TB_VOID,
  TB_TYPE,
  TB_ANY,
  TB_STRUCT,
  TB_ENUM,
  TB_ARRAY,
  TB_PTR,
  TB_FUNC,
  TB_INTERFACE,
  TB_ERROR,
} TypeBase;

#define TYPE_NONE_ID 0
#define TYPE_UNKNOWN_ID 1

DEFINE_VEC(TypeId, TypeIdVec);
DEFINE_HMAP(TypeIdVec*, TypesOfNameMap);

DEFINE_HMAP(TypeId, TypeByNameMap);

typedef struct {
  unsigned is_immutable : 1;
  unsigned is_owning : 1;
  unsigned is_addressable : 1;
  unsigned is_primitive: 1;
  unsigned is_nullable : 1;
  unsigned is_failable : 1;
  unsigned is_unsigned : 1;
  unsigned is_disposable : 1;
  unsigned is_view : 1;
} TypeFlags;

typedef struct TypeField {
  const char *name;
  TypeId type;
  unsigned is_private : 1;
} TypeField;

typedef struct TypeParam {
  TypeId type;
  struct ASTNode *default_expr;
} TypeParam;

typedef struct Type {
  TypeBase base;
  TypeFlags flags;
  struct ASTNode *expression;  // type expression found in source code, this field does not own the ASTNode
  size_t object_size;
  const char *namespace;
  const char *name;
  union {
    struct {
      TypeId element_type;
      size_t size;
    } array;
    struct {
      TypeField *fields;
      size_t field_count;
    } structure;
    struct {
      TypeParam *params;
      uint32_t param_count;
      TypeId return_type;
    } func;
    struct {
      TypeId inner;
    } wrap;
  } props;
} Type;

typedef struct TypeTable {
  Type *types;
  size_t len;
  size_t cap;
  struct TypeByNameMap *by_name;
} TypeTable;

typedef struct FuncEntry {
  const char *name;
  const char *namespace;
  TypeId type_id;
  TypeId ret_type;
  TypeParam *params;
  size_t param_count;
  const struct ASTNode *func_node;
} FuncEntry;

typedef struct FuncTable {
  struct TypesOfNameMap *by_name;
  struct TypesOfNameMap *by_qualified;
} FuncTable;

typedef struct Environment {
  TypeTable type_table;
  FuncTable func_table;
} Environment;

// create type table and initialize it with integral types and default functions
// Type table contains type names to type descriptors
// Function table contains function names to type ids
Environment *env_new(void);

// lookup for the type by human readable name for integral types, mangeled name for functions
TypeId env_find_typeid_by_name(Environment *env, const char* name);

// borrow type descriptor
Type *env_get_type_by_id(Environment *env, TypeId id);

// create a type representation, a string, buf owend by caller.
size_t env_render_type_buf(Environment *env, TypeId typeId, char buf[], size_t buf_len);

// create a type representation, a astring, returned string owned by the function (static buf)
const char *env_render_type(Environment *env, TypeId typeId);

// find all types that the overloaded function name has
TypeIdVec *env_find_func_types_by_name(Environment *env, const char *func_name);

#define env_find_func_type_name_params(env, operator_name, ...) \
  env_find_func_type_name_n_params(env, operator_name, (sizeof((TypeId[]){__VA_ARGS__}) / sizeof(TypeId)), (TypeId[]){__VA_ARGS__})
// find function type by function name (human readable): operator+, dispose...
TypeId env_find_func_type_name_n_params(Environment *env, const char *func_name, uint32_t argc, TypeId param_type_ids[]);

void env_free(Environment *env);

#endif

