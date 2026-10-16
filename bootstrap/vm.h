#ifndef VM_H
#define VM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Virtual Machine facilities

typedef enum : uint8_t {
  BC_HALT = 0,                   // stop execution
  BC_ECHOI,                      // prints signed integer value on the operand stack
  BC_ECHOS,                      // prints string, using the value on the operand stack as a pointer to the beginning to the srting
// pop operands, pushes the result
  BC_NOT,
  BC_ADD,
  BC_SUB,
  BC_MUL,
  BC_DIV,
  BC_DUP,                        // duplicates value on the operand stack and pushes it back

  BC_CALL,                       // <slot_count> <address> ; pushes return address on the operand stack, fills stack frame pointer and transfers execution to address
  BC_RETURN,                     // operates on the stack frame pointer, transferes exection to the return address
  BC_JUMP,                       // <offset> ; unconditional transfer of execution
  BC_JUMPF,                      //          ; jump if false is on the top of the stack, pops operand before jump
  BC_LOAD,                       // <slot>   ; borrow slot x onto the operand stack
  BC_LOAD_FIELD,                 // <slot> <offset> ; laods the address of the value of a field of a structure
  BC_STORE,                      // <slot>   ; move the result from the operand stack into slot
  BC_CONSTA,                     // <offset> ; puts an address of the offset in the constant pool on the operand stack
  BC_ARRBEG,                     //          ; pops an array's reference from the operand stack and pushes an address of the first element
  BC_CONST,                      // <offset> ; loads a 8 byte value from constant pool to the operand stack
  BC_PUSH,                       // <uint64> ; pushes value on the operand stack
  BC_MAKE,                       // <slot> <kind> ; create a new owned value in a slot
  BC_MOVE,                       // <slot1> <slot2> transfer ownership from slot1 to slot2, invalidate slot1
  BC_DROP,                       // <slot>   ; destroy the stack frame slot's value
  BC_ENUM_SIZE
} BCCommandKind;

typedef struct {
  BCCommandKind kind;
  uint8_t op_len;
  uint64_t ops[2];
} VMCommand;

// never could be less then 64bits, it stores 64bits addresses
typedef uint64_t VMStackSlot;
typedef uint8_t  VMCodeSlot;
//typedef uint64_t VMOffset;

typedef struct {
  VMStackSlot *slots;
  VMCodeSlot *ret_addr;
} VMFrame;

typedef struct {
  size_t pool_len;           // count elements, not bytes
  uint8_t *pool;             // storage for strings
  size_t add_pool_idx;       // index of the first free place in pool of constants

  VMCodeSlot *code_end;      // the upped bound for instruction pointer
  VMCodeSlot *code;          // instructions
  VMCodeSlot *ip;            // instruction pointer, grows up, pints to the code
  VMCodeSlot *add_code_ptr;  // pointer to the first free palce for the next command to add

  VMStackSlot *stack_end;    // the upper bound on the stack pointer
  VMStackSlot *stack;        // function frames with slots and temporary operands
  VMStackSlot *sp;           // stack pointer, grows up, points to the operand stack
  VMFrame      sfp;          // stack frame pointer, grows up, pionts to the operand stack

  VMCodeSlot *entry_point;   // execution must start from here

  bool stopped;              // if true the vm can operate no longer
  char error[1024];          // filled with message if error occured in runtime
} VMEngine;

typedef enum {
  VM_STATE_RUNNING,
  VM_STATE_STOPPED,
  VM_STATE_ERROR,
} VMState;

// array with embedded data
// layout is {length(8 bytes), datum1(byte), datum2(byte), …}
typedef struct {
  uint64_t length;
  uint8_t data[];  // arbitrary size of elements expected
} VMArray;

// All *_len in element count.
VMEngine *vm_new(size_t pool_len, size_t code_len, size_t stack_len);

void      vm_add_command(VMEngine *vm, VMCommand cmd);

// returns the offset of the string in pool
size_t    vm_add_string(VMEngine *vm, const char *s, size_t len);

VMCodeSlot *vm_add_function(VMEngine *vm);

VMState   vm_execute(VMEngine *vm, size_t steps);

void      vm_free(VMEngine *vm);

#endif
