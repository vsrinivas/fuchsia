// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_CODE_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_CODE_H_

#include <memory>
#include <vector>

#include "src/developer/shell/interpreter/src/value.h"

namespace shell {
namespace interpreter {

namespace code {

// Defines all the operations the interpreter can execute.
enum class Opcode : uint64_t {
  // Nop: do nothing.
  kNop,
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
  // Pushes a reference counted literal to the thread's value stack. Increment the reference count
  // for the object.
  kReferenceCountedLiteral,
  // Return from code execution. The execution goes back to the calling scope or stops if it was
  // the last execution scope.
  kRet,
  // Pops a value from the thread's value stack and stores it into a 8 bit global variable.
  kStoreRaw8,
  // Pops a value from the thread's value stack and stores it into a 16 bit global variable.
  kStoreRaw16,
  // Pops a value from the thread's value stack and stores it into a 32 bit global variable.
  kStoreRaw32,
  // Pops a value from the thread's value stack and stores it into a 64 bit global variable.
  kStoreRaw64,
};

// Defines some code. This can represent the code for one function or the code for the pending
// instructions of one execution context.
class Code {
 public:
  Code() = default;

  const std::vector<uint64_t>& code() const { return code_; }

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

  // Adds a string literal operation.
  void StringLiteral(String* value) {
    strings_.emplace_back(value);
    emplace_back_opcode(Opcode::kReferenceCountedLiteral);
    code_.emplace_back(reinterpret_cast<uint64_t>(value));
  }

 private:
  void emplace_back_opcode(Opcode opcode) { code_.emplace_back(static_cast<uint64_t>(opcode)); }

  // Contains the operations. It's a mix of opcodes and arguments for the operations.
  std::vector<uint64_t> code_;

  // Keeps alive the string literals in the code.
  std::vector<StringContainer> strings_;
};

}  // namespace code
}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_CODE_H_
