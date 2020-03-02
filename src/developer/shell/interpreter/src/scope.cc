// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/scope.h"

#include <cstdint>
#include <string>

#include "src/developer/shell/interpreter/src/code.h"
#include "src/developer/shell/interpreter/src/interpreter.h"

namespace shell {
namespace interpreter {

void ExecutionScope::Execute(ExecutionContext* context, Thread* thread,
                             std::unique_ptr<code::Code> code) {
  size_t pc = 0;
  for (;;) {
    FXL_DCHECK(pc < code->code().size());
    code::Opcode opcode = static_cast<code::Opcode>(code->code()[pc++]);
    switch (opcode) {
      case code::Opcode::kNop:
        break;
      case code::Opcode::kLiteral64: {
        uint64_t value = code->code()[pc++];
        thread->Push(value);
        break;
      }
      case code::Opcode::kReferenceCountedLiteral: {
        uint64_t value = code->code()[pc++];
        auto object = reinterpret_cast<ReferenceCountedBase*>(value);
        object->Use();
        thread->Push(value);
        break;
      }
      case code::Opcode::kStore64: {
        uint64_t index = code->code()[pc++];
        auto destination = reinterpret_cast<uint64_t*>(
            thread->isolate()->global_execution_scope()->Data(index, sizeof(uint64_t)));
        *destination = thread->Pop();
        break;
      }
      case code::Opcode::kRet:
        return;
    }
  }
}

}  // namespace interpreter
}  // namespace shell
