// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/interpreter.h"

#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace shell {
namespace interpreter {

void ExecutionContext::EmitError(std::string error_message) {
  interpreter_->EmitError(this, std::move(error_message));
}

void ExecutionContext::EmitError(NodeId node_id, std::string error_message) {
  interpreter_->EmitError(this, node_id, std::move(error_message));
}

void ExecutionContext::Dump() {
  for (const auto& instruction : pending_instructions_) {
    std::stringstream ss;
    instruction->Dump(ss);
    interpreter_->TextResult(this, ss.str());
  }
  interpreter_->DumpDone(this);
}

void ExecutionContext::Execute() {
  if (pending_instructions_.empty() || has_errors()) {
    if (!has_errors()) {
      interpreter_->EmitError(this, "No pending instruction to execute.");
    }
    interpreter_->ContextDoneWithAnalysisError(this);
  } else {
    // Currently we only do the compilation.
    Compile();
    if (has_errors()) {
      interpreter_->ContextDoneWithAnalysisError(this);
    } else {
      interpreter_->ContextDone(this);
    }
  }
  interpreter_->EraseContext(id_);
}

void ExecutionContext::Compile() {
  for (const auto& instruction : pending_instructions_) {
    instruction->Compile(this);
  }
}

ExecutionContext* Interpreter::AddContext(uint64_t context_id) {
  auto context = GetContext(context_id);
  if (context != nullptr) {
    EmitError(nullptr, "Execution context " + std::to_string(context_id) + " is already in use.");
    return nullptr;
  }
  auto new_context = std::make_unique<ExecutionContext>(this, context_id);
  auto returned_value = new_context.get();
  contexts_.emplace(context_id, std::move(new_context));
  return returned_value;
}

void Interpreter::AddNode(uint64_t file_id, uint64_t node_id, Node* node) {
  nodes_[std::make_pair(file_id, node_id)] = node;
}

void Interpreter::RemoveNode(uint64_t file_id, uint64_t node_id) {
  nodes_.erase(std::make_pair(file_id, node_id));
}

}  // namespace interpreter
}  // namespace shell
