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
    FX_DCHECK(pc < code->code().size());
    code::Opcode opcode = static_cast<code::Opcode>(code->code()[pc++]);
    switch (opcode) {
      case code::Opcode::kNop:
        break;
      case code::Opcode::kLiteral64: {
        uint64_t value = code->code()[pc++];
        thread->Push(value);
        break;
      }
      case code::Opcode::kLoadRaw8: {
        uint64_t index = code->code()[pc++];
        auto source = thread->isolate()->global_execution_scope()->Data<uint8_t>(index);
        thread->Push(static_cast<uint64_t>(*source));
        break;
      }
      case code::Opcode::kLoadRaw16: {
        uint64_t index = code->code()[pc++];
        auto source = thread->isolate()->global_execution_scope()->Data<uint16_t>(index);
        thread->Push(static_cast<uint64_t>(*source));
        break;
      }
      case code::Opcode::kLoadRaw32: {
        uint64_t index = code->code()[pc++];
        auto source = thread->isolate()->global_execution_scope()->Data<uint32_t>(index);
        thread->Push(static_cast<uint64_t>(*source));
        break;
      }
      case code::Opcode::kLoadRaw64: {
        uint64_t index = code->code()[pc++];
        auto source = thread->isolate()->global_execution_scope()->Data<uint64_t>(index);
        thread->Push(*source);
        break;
      }
      case code::Opcode::kLoadReferenceCounted: {
        uint64_t index = code->code()[pc++];
        auto source =
            thread->isolate()->global_execution_scope()->Data<ReferenceCountedBase*>(index);
        ReferenceCountedBase* value = *source;
        value->Use();
        thread->Push(reinterpret_cast<uint64_t>(value));
        break;
      }
      case code::Opcode::kReferenceCountedLiteral: {
        uint64_t value = code->code()[pc++];
        auto object = reinterpret_cast<ReferenceCountedBase*>(value);
        object->Use();
        thread->Push(value);
        break;
      }
      case code::Opcode::kRet:
        return;
      case code::Opcode::kStoreRaw8: {
        uint64_t index = code->code()[pc++];
        auto destination = thread->isolate()->global_execution_scope()->Data<uint8_t>(index);
        *destination = static_cast<uint8_t>(thread->Pop());
        break;
      }
      case code::Opcode::kStoreRaw16: {
        uint64_t index = code->code()[pc++];
        auto destination = thread->isolate()->global_execution_scope()->Data<uint16_t>(index);
        *destination = static_cast<uint16_t>(thread->Pop());
        break;
      }
      case code::Opcode::kStoreRaw32: {
        uint64_t index = code->code()[pc++];
        auto destination = thread->isolate()->global_execution_scope()->Data<uint32_t>(index);
        *destination = static_cast<uint32_t>(thread->Pop());
        break;
      }
      case code::Opcode::kStoreRaw64: {
        uint64_t index = code->code()[pc++];
        auto destination = thread->isolate()->global_execution_scope()->Data<uint64_t>(index);
        *destination = thread->Pop();
        break;
      }
    }
  }
}

}  // namespace interpreter
}  // namespace shell
