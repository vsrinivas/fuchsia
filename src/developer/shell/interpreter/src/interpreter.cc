// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/interpreter.h"

#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "src/developer/shell/interpreter/src/schema.h"

namespace shell {
namespace interpreter {

void ExecutionContext::AddObjectSchema(std::shared_ptr<ObjectSchema> object_schema) {
  object_schemas_[std::make_pair<uint64_t, uint64_t>(object_schema->node_id(),
                                                     object_schema->file_id())] = object_schema;
}

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
    auto code = std::make_unique<code::Code>();
    Compile(code.get());
    if (has_errors()) {
      interpreter_->ContextDoneWithAnalysisError(this);
    } else {
      interpreter_->isolate()->AllocateGlobals();
      interpreter_->isolate()->Execute(this, std::move(code));
    }
  }
  interpreter_->EraseContext(id_);
}

void ExecutionContext::Compile(code::Code* code) {
  for (const auto& instruction : pending_instructions_) {
    instruction->Compile(this, code);
  }
  code->Ret();
}

void Interpreter::Shutdown(std::vector<std::string>* errors) {
  // Destroy any pending execution context.
  contexts_.clear();
  // Shutdown the isolate. That frees all the global data.
  isolate_->Shutdown();
  // Checks that we don't have any undeleted object (would be a memory leak).
  if (string_count_ != 0) {
    if (string_count_ == 1) {
      (*errors).emplace_back("1 string not freed.");
    } else {
      (*errors).emplace_back(std::to_string(string_count_) + " strings not freed.");
    }
  }
  if (object_count_ != 0) {
    if (object_count_ == 1) {
      (*errors).emplace_back("1 object not freed.");
    } else {
      (*errors).emplace_back(std::to_string(object_count_) + " objects not freed.");
    }
  }
  if (object_schema_count_ != 0) {
    if (object_schema_count_ == 1) {
      (*errors).emplace_back("1 object schema not freed.");
    } else {
      (*errors).emplace_back(std::to_string(object_schema_count_) + " object schemas not freed.");
    }
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
