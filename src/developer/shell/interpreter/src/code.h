// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_CODE_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_CODE_H_

#include <memory>
#include <set>
#include <vector>

#include "src/developer/shell/interpreter/src/nodes.h"
#include "src/developer/shell/interpreter/src/value.h"

namespace shell {
namespace interpreter {

namespace code {

// Defines all the operations the interpreter can execute.
enum class Opcode : uint64_t {
  // Nop: do nothing.
  kNop,
  // Pops a value from the stack and emits it (send it back to the client).
  kEmitResult,
  // Pops two 8 bit integers from the stack, adds them and pushes the result to the stack.
  kInt8Addition,
  // Pops two 16 bit integers from the stack, adds them and pushes the result to the stack.
  kInt16Addition,
  // Pops two 32 bit integers from the stack, adds them and pushes the result to the stack.
  kInt32Addition,
  // Pops two 64 bit integers from the stack, adds them and pushes the result to the stack.
  kInt64Addition,
  // Pushes a 64 bit literal to the thread's value stack.
  kLiteral64,
  // Loads a 8 bit global variable and pushes it to the stack.
  kLoadRaw8,
  // Loads a 16 bit global variable and pushes it to the stack.
  kLoadRaw16,
  // Loads a 32 bit global variable and pushes it to the stack.
  kLoadRaw32,
  // Loads a 64 bit global variable and pushes it to the stack.
  kLoadRaw64,
  // Loads a referenced counted value from a global variable and pushes it to the thread's value
  // stack.
  kLoadReferenceCounted,
  // Initializes an object.
  // Value stack before:
  //   <reference to uninitialized object>
  //   <initial value N>
  //   <initial value N-1>
  //   ...
  //   (where 1, 2, ..., N are values in the order they appear in the schema of the object)
  // Value stack after:
  //   <reference to initialized object>
  kObjectInit,
  // Allocates an object and sets its schema.
  // Value stack before:
  //   ...
  // Value stack after:
  //   <reference>
  //   ...
  kObjectNew,
  // Pushes a reference counted literal to the thread's value stack. Increment the reference count
  // for the object.
  kReferenceCountedLiteral,
  // Return from code execution. The execution goes back to the calling scope or stops if it was
  // the last execution scope.
  kRet,
  // Pops two 8 bit signed integers from the stack, adds them and pushes the result to the stack.
  // If an overflow or and underflow occur, an error is generated and the execution stops.
  kSint8AdditionWithExceptions,
  // Pops two 16 bit signed integers from the stack, adds them and pushes the result to the stack.
  // If an overflow or and underflow occur, an error is generated and the execution stops.
  kSint16AdditionWithExceptions,
  // Pops two 32 bit signed integers from the stack, adds them and pushes the result to the stack.
  // If an overflow or and underflow occur, an error is generated and the execution stops.
  kSint32AdditionWithExceptions,
  // Pops two 64 bit signed integers from the stack, adds them and pushes the result to the stack.
  // If an overflow or and underflow occur, an error is generated and the execution stops.
  kSint64AdditionWithExceptions,
  // Pops a value from the thread's value stack and stores it into a 8 bit global variable.
  kStoreRaw8,
  // Pops a value from the thread's value stack and stores it into a 16 bit global variable.
  kStoreRaw16,
  // Pops a value from the thread's value stack and stores it into a 32 bit global variable.
  kStoreRaw32,
  // Pops a value from the thread's value stack and stores it into a 64 bit global variable.
  kStoreRaw64,
  // Pops a value from the stack, releases the old value of the global variable and stores the new
  // value.
  kStoreReferenceCounted,
  // Pops several strings from the stack, concatenates them and pushes the result to the stack.
  kStringConcatenation,
  // Pops two 8 bit unsigned integers from the stack, adds them and pushes the result to the stack.
  // If an overflow occurs, an error is generated and the execution stops.
  kUint8AdditionWithExceptions,
  // Pops two 16 bit unsigned integers from the stack, adds them and pushes the result to the stack.
  // If an overflow occurs, an error is generated and the execution stops.
  kUint16AdditionWithExceptions,
  // Pops two 32 bit unsigned integers from the stack, adds them and pushes the result to the stack.
  // If an overflow occurs, an error is generated and the execution stops.
  kUint32AdditionWithExceptions,
  // Pops two 64 bit unsigned integers from the stack, adds them and pushes the result to the stack.
  // If an overflow occurs, an error is generated and the execution stops.
  kUint64AdditionWithExceptions,
};

// Defines some code. This can represent the code for one function or the code for the pending
// instructions of one execution context.
class Code {
 public:
  Code() = default;

  const std::vector<uint64_t>& code() const { return code_; }

  // Adds an emit result.
  void EmitResult(std::unique_ptr<Type> type) {
    emplace_back_opcode(Opcode::kEmitResult);
    code_.emplace_back(reinterpret_cast<uint64_t>(type.get()));
    types_.emplace_back(std::move(type));
  }

  // Adds an integer addition.
  void IntegerAddition(bool with_exceptions, size_t size, bool is_signed) {
    switch (size) {
      case 1:
        emplace_back_opcode(with_exceptions ? (is_signed ? Opcode::kSint8AdditionWithExceptions
                                                         : Opcode::kUint8AdditionWithExceptions)
                                            : Opcode::kInt8Addition);
        break;
      case 2:
        emplace_back_opcode(with_exceptions ? (is_signed ? Opcode::kSint16AdditionWithExceptions
                                                         : Opcode::kUint16AdditionWithExceptions)
                                            : Opcode::kInt16Addition);
        break;
      case 4:
        emplace_back_opcode(with_exceptions ? (is_signed ? Opcode::kSint32AdditionWithExceptions
                                                         : Opcode::kUint32AdditionWithExceptions)
                                            : Opcode::kInt32Addition);
        break;
      case 8:
        emplace_back_opcode(with_exceptions ? (is_signed ? Opcode::kSint64AdditionWithExceptions
                                                         : Opcode::kUint64AdditionWithExceptions)
                                            : Opcode::kInt64Addition);
        break;
      default:
        FX_LOGS(FATAL) << "Bad integer size " << size;
    }
  }

  // Adds a 64 bit literal operation.
  void Literal64(uint64_t value) {
    emplace_back_opcode(Opcode::kLiteral64);
    code_.emplace_back(value);
  }

  // Adds a 64 bit literal operation.
  void LoadReferenceCounted(size_t index) {
    emplace_back_opcode(Opcode::kLoadReferenceCounted);
    code_.emplace_back(index);
  }

  // Adds a global variable load operation.
  void LoadRaw(size_t index, size_t size) {
    switch (size) {
      case 1:
        emplace_back_opcode(Opcode::kLoadRaw8);
        break;
      case 2:
        emplace_back_opcode(Opcode::kLoadRaw16);
        break;
      case 4:
        emplace_back_opcode(Opcode::kLoadRaw32);
        break;
      case 8:
        emplace_back_opcode(Opcode::kLoadRaw64);
        break;
      default:
        FX_LOGS(FATAL) << "Bad builtin size " << size;
    }
    code_.emplace_back(index);
  }

  // Adds a ret operation.
  void Ret() { emplace_back_opcode(Opcode::kRet); }

  // Adds a global variable store operation.
  void StoreRaw(uint64_t index, uint64_t size) {
    switch (size) {
      case 1:
        emplace_back_opcode(Opcode::kStoreRaw8);
        break;
      case 2:
        emplace_back_opcode(Opcode::kStoreRaw16);
        break;
      case 4:
        emplace_back_opcode(Opcode::kStoreRaw32);
        break;
      case 8:
        emplace_back_opcode(Opcode::kStoreRaw64);
        break;
      default:
        FX_LOGS(FATAL) << "Bad builtin size " << size;
    }
    code_.emplace_back(index);
  }

  // Adds a reference counted global variable store operation.
  void StoreReferenceCounted(uint64_t index) {
    emplace_back_opcode(Opcode::kStoreReferenceCounted);
    code_.emplace_back(index);
  }

  // Adds a string concatenation operation.
  void StringConcatenation(size_t string_count) {
    emplace_back_opcode(Opcode::kStringConcatenation);
    code_.emplace_back(string_count);
  }

  // Adds a string literal operation.
  void StringLiteral(String* value) {
    strings_.emplace_back(value);
    emplace_back_opcode(Opcode::kReferenceCountedLiteral);
    code_.emplace_back(reinterpret_cast<uint64_t>(value));
  }

  // Adds an operation that allocates an object and leaves a reference to it on the stack.
  void ObjectPush(const std::shared_ptr<ObjectSchema>& object_schema) {
    emplace_back_opcode(Opcode::kObjectNew);
    auto ref = object_schemas_.insert(object_schema);
    // We store the shared_ptr so that the object that later gets allocated can ref and deref it.
    const std::shared_ptr<ObjectSchema>* ptr = &(*(ref.first));
    code_.emplace_back(reinterpret_cast<uint64_t>(ptr));
  }

  // Adds an operation that initializes an object.
  void ObjectInit() { emplace_back_opcode(Opcode::kObjectInit); }

 private:
  void emplace_back_opcode(Opcode opcode) { code_.emplace_back(static_cast<uint64_t>(opcode)); }

  struct SchemaPtrLT {
    constexpr bool operator()(const std::shared_ptr<ObjectSchema>& lhs,
                              const std::shared_ptr<ObjectSchema>& rhs) const {
      return lhs.get() < rhs.get();
    }
  };

  // Tracking for the schemas used as arguments by the code.  Because schemas are managed by
  // shared_ptrs, and code_ only stores opaque values.
  std::set<std::shared_ptr<ObjectSchema>, SchemaPtrLT> object_schemas_;

  // Contains the operations. It's a mix of opcodes and arguments for the operations.
  std::vector<uint64_t> code_;

  // Keeps alive the string literals in the code.
  std::vector<StringContainer> strings_;

  // Keeps alive the types in the code.
  std::vector<std::unique_ptr<Type>> types_;
};

}  // namespace code
}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_CODE_H_
