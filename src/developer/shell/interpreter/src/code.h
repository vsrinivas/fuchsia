// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_CODE_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_CODE_H_

#include <memory>
#include <vector>

namespace shell {
namespace interpreter {

namespace code {

// Defines all the operations the interpreter can execute.
enum class Opcode : uint64_t {
  // Nop: do nothing.
  kNop,
  // Pushes a 64 bit literal to the thread's value stack.
  kLiteral64,
  // Stores a 64 bit value popped from the thread's value stack into a global variable.
  kStore64,
  // Return from code execution. The execution goes back to the calling scope or stops if it was
  // the last execution scope.
  kRet
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

  // Adds a 64 bit store within the global scope.
  void Store64(uint64_t index) {
    emplace_back_opcode(Opcode::kStore64);
    code_.emplace_back(index);
  }

  // Adds a ret operation.
  void Ret() { emplace_back_opcode(Opcode::kRet); }

 private:
  void emplace_back_opcode(Opcode opcode) { code_.emplace_back(static_cast<uint64_t>(opcode)); }

  // Contains the operations. It's a mix of opcodes and arguments for the operations.
  std::vector<uint64_t> code_;
};

}  // namespace code
}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_CODE_H_
