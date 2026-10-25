What is a type in this system.

```c
struct Type {
  enum {
    TB_INT, TB_FLOAT, TB_STRUCT, TB_ARRAY, TB_FUNC, TB_bytes, TB_STRING, TB_VOID, TB_TYPE,
  } base;
  struct {
    char is_immutable:1;      // x const int := 1
    char is_owning:1;         // u User = {name="username", id=123}
    char is_addressable:1;    // someArray[i]
    char is_nullable:1;       // int?, User?
    char is_failable:1;       // int!, User!
    char is_unsigned:1;
    char is_disposable:1;     // the dispose function is defined for this type.
    char is_private:1;        // the type can be used only inside the parent module it is defined in
  } flags;
  ASTNode *expression;
  size_t obejct_size;         // object is a continuous memory span for the instance of the type
  const char namespace[];     // codebase + module + [function + block_number]. Fully qualified name = namespace + name;
                              // Here "codebase" is a library name or executable name.
                              // Default module name is "main", default codebase is the name of the source file.
  const char name[];          // readable name of the type: User, int, float.
  const char name_mangeled[]; // ?
  union {
    struct {
      TypeId element_type;
      size_t size;
    } array;
    struct {
      struct {
        const char *name;
        TypeId field_type;
      } StructField fields[];
    } structure;
    struct {
      struct {
        const char *name;
        TypeId param_type;
      } StructParam params[];
      TypeId return_type;
    } func;
  } props;
};
```

```c xlang
y float := 3.14
x := float.from(1) + y
echo string.from(x)
conn.send(bytes.from("Hello, world!"))
conn.send("Hello, world!")
from func (bytes, u *User) const bytes // readonly (writable if copy)?
from func (b *bytes) User // writable
u2 User := bytes.from(u1).from()
```

How to use this Type to check is its instance is eligible to be passed to a function call?
Check steps:
1. Equality check: name and mutability checks: const int -> int (fail); int -> const int (ok).
    The name and namespace of the types must be equal.
2. If Equality fails -> Convergence check: int + float -/-> float (float.from(int) + float -> float); int + int -> int; x = any -> void.
    If names are not equals lookup "from" function with first argument - name of the type of the first operand. If present -> ??? (can be converted, not an error?)
3. Ownership check: 
4. Lifetime  check: 

If Equality check succeeds no Convergence check is needed.
Equality may fail if Convergence succeeds, then success.
Ownership must succeed.
Lifetime must succeed.

Equality criteria: Type1 is equal to Type2 when: all it's (non-pointer?) fields are equals, except flags 'is_owning'.
Convergence criteria: Type1 is convergent to Type2 if in current context lookup can find a function `from func (Type1) Type2` (types may be const).
Ownership criteria: (legend arrow -> is ownership transit, T - owns, *T - view, &T - owning pointer). OK: T -> *T; T -> &T; T -> T; &T -> T. FAIL: *T -> T; *T -> &T.
Lifetime criteria: *T cannot leave longer than it's T or &T. At the all exits from current scope (including `return` and `break`), consider all *T, if *T dies then T must about to die too.

Some checks cannot be performed before the whole code will be checked.
