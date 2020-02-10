// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/interpreter.h"

#include <memory>
#include <string>

namespace shell {
namespace interpreter {

void Interpreter::AddContext(uint64_t context_id) {
  auto context = GetContext(context_id);
  if (context != nullptr) {
    EmitError(nullptr, "Execution context " + std::to_string(context_id) + " is already in use.");
  } else {
    contexts_.emplace(context_id, std::make_unique<ExecutionContext>(this, context_id));
  }
}

void Interpreter::ExecuteContext(uint64_t context_id) {
  auto context = GetContext(context_id);
  if (context == nullptr) {
    EmitError(nullptr, "Execution context " + std::to_string(context_id) + " not defined.");
  } else {
    // Currently, there is no way to define an instruction. That means that the execution always
    // returns the same error (and does nothing).
    EmitError(context, "No pending instruction to execute.");
    ContextDoneWithAnalysisError(context);
    contexts_.erase(context_id);
  }
}

}  // namespace interpreter
}  // namespace shell
