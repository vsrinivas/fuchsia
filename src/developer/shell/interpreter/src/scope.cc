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

void StringConcatenation(Thread* thread, uint64_t count) {
  FX_DCHECK(thread->stack_size() >= count);
  size_t string_size = 0;
  for (size_t i = 0; i < count; ++i) {
    auto value = reinterpret_cast<String*>(thread->Value(i));
    string_size += value->size();
  }
  std::string string;
  string.reserve(string_size);
  for (size_t i = count; i > 0;) {
    --i;
    auto value = reinterpret_cast<String*>(thread->Value(i));
    string += value->value();
    value->Release();
  }
  thread->Consume(count);
  auto result = new String(std::move(string));
  thread->Push(reinterpret_cast<uint64_t>(result));
}

template <typename S, typename U>
bool SAddWithExceptions(ExecutionContext* context, Thread* thread, const std::string& type_name) {
  U right = static_cast<U>(thread->Pop());
  U left = static_cast<U>(thread->Pop());
  U result = left + right;
  int shift = sizeof(U) * 8 - 1;
  if ((right >> shift) == (left >> shift)) {
    // The two operands have the same sign.
    if ((right >> shift) != (result >> shift)) {
      // The result doesn't have the same sign.
      if ((right >> shift) == 0) {
        // The operands are positive => overflow.
        context->EmitError(type_name + " overflow when adding " +
                           std::to_string(static_cast<S>(left)) + " and " +
                           std::to_string(static_cast<S>(right)) + ".");
      } else {
        // The operands are negative => underflow.
        context->EmitError(type_name + " underflow when adding " +
                           std::to_string(static_cast<S>(left)) + " and " +
                           std::to_string(static_cast<S>(right)) + ".");
      }
      return false;
    }
  }
  thread->Push(result);
  return true;
}

template <typename U>
bool UAddWithExceptions(ExecutionContext* context, Thread* thread, const std::string& type_name) {
  U right = static_cast<U>(thread->Pop());
  U left = static_cast<U>(thread->Pop());
  U result = left + right;
  if (result < right) {
    context->EmitError(type_name + " overflow when adding " + std::to_string(left) + " and " +
                       std::to_string(right) + ".");
    return false;
  }
  thread->Push(result);
  return true;
}

void ExecutionScope::Execute(ExecutionContext* context, Thread* thread,
                             std::unique_ptr<code::Code> code) {
  size_t pc = 0;
  for (;;) {
    FX_DCHECK(pc < code->code().size());
    code::Opcode opcode = static_cast<code::Opcode>(code->code()[pc++]);
    switch (opcode) {
      case code::Opcode::kNop:
        break;
      case code::Opcode::kInt8Addition: {
        uint8_t right = static_cast<uint8_t>(thread->Pop());
        uint8_t left = static_cast<uint8_t>(thread->Pop());
        thread->Push(left + right);
        break;
      }
      case code::Opcode::kInt16Addition: {
        uint16_t right = static_cast<uint16_t>(thread->Pop());
        uint16_t left = static_cast<uint16_t>(thread->Pop());
        thread->Push(left + right);
        break;
      }
      case code::Opcode::kInt32Addition: {
        uint32_t right = static_cast<uint32_t>(thread->Pop());
        uint32_t left = static_cast<uint32_t>(thread->Pop());
        thread->Push(left + right);
        break;
      }
      case code::Opcode::kInt64Addition: {
        uint64_t right = thread->Pop();
        uint64_t left = thread->Pop();
        thread->Push(left + right);
        break;
      }
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
      case code::Opcode::kSint8AdditionWithExceptions:
        if (!SAddWithExceptions<int8_t, uint8_t>(context, thread, "Int8")) {
          return;
        }
        break;
      case code::Opcode::kSint16AdditionWithExceptions:
        if (!SAddWithExceptions<int16_t, uint16_t>(context, thread, "Int16")) {
          return;
        }
        break;
      case code::Opcode::kSint32AdditionWithExceptions:
        if (!SAddWithExceptions<int32_t, uint32_t>(context, thread, "Int32")) {
          return;
        }
        break;
      case code::Opcode::kSint64AdditionWithExceptions:
        if (!SAddWithExceptions<int64_t, uint64_t>(context, thread, "Int64")) {
          return;
        }
        break;
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
      case code::Opcode::kStringConcatenation: {
        StringConcatenation(thread, code->code()[pc++]);
        break;
      }
      case code::Opcode::kUint8AdditionWithExceptions:
        if (!UAddWithExceptions<uint8_t>(context, thread, "Uint8")) {
          return;
        }
        break;
      case code::Opcode::kUint16AdditionWithExceptions:
        if (!UAddWithExceptions<uint16_t>(context, thread, "Uint16")) {
          return;
        }
        break;
      case code::Opcode::kUint32AdditionWithExceptions:
        if (!UAddWithExceptions<uint32_t>(context, thread, "Uint32")) {
          return;
        }
        break;
      case code::Opcode::kUint64AdditionWithExceptions:
        if (!UAddWithExceptions<uint64_t>(context, thread, "Uint64")) {
          return;
        }
        break;
    }
  }
}

}  // namespace interpreter
}  // namespace shell
