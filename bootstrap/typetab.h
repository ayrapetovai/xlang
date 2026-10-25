#ifndef TYPETAB_H
#define TYPETAB_H

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

typedef struct {
  unsigned is_immutable : 1;
  unsigned is_owning : 1;
  unsigned is_addressable : 1;
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
  struct HashMap *by_name;
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
  struct HashMap *by_name;
  struct HashMap *by_qualified;
} FuncTable;

// create type table and initialize it with integral types and default functions
// Type table contains type names to type descriptors
TypeTable *typetab_new(void);

// lookup for the type by human readable name for integral types, mangeled name for functions
TypeId typetab_find_by_name(TypeTable *tt, const char* name);

// borrow type descriptor
Type *typetab_get(TypeTable *tt, TypeId id);

// create a type representation, a string, buf owend by caller.
size_t typetab_render_type_buf(TypeTable *tt, TypeId typeId, char buf[], size_t buf_len);

// create a type representation, a astring, returned string owned by the function (static buf)
const char *typetab_render_type(TypeTable *tt, TypeId typeId);

// deallocate type table
void typetab_free(TypeTable *tt);

// Function table contains function names to type ids
FuncTable *functab_new(TypeTable *tt);

// find function type by function name (human readable): operator+, dispose...
TypeId functab_find(FuncTable *ft, const char *name);

void functab_free(FuncTable *ft);

#endif

