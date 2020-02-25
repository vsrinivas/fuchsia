// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/nodes.h"

#include <sstream>
#include <string>

#include "src/developer/shell/interpreter/src/expressions.h"
#include "src/developer/shell/interpreter/src/interpreter.h"

namespace shell {
namespace interpreter {

Variable* Type::CreateVariable(ExecutionContext* context, Scope* scope, NodeId id,
                               const std::string& name) const {
  std::stringstream ss;
  ss << "Can't create variable '" << name << "' of type " << *this << " (not implemented yet).";
  context->EmitError(id, ss.str());
  return nullptr;
}

void Type::GenerateDefaultValue(ExecutionContext* context, code::Code* code) const {
  std::stringstream ss;
  ss << "Can't create default value of type " << *this << " (not implemented yet).";
  context->EmitError(ss.str());
}

void Type::GenerateIntegerLiteral(ExecutionContext* context, code::Code* code,
                                  const IntegerLiteral* literal) const {
  std::stringstream ss;
  ss << "Can't create an integer literal of type " << *this << '.';
  context->EmitError(literal->id(), ss.str());
}

void Type::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  FXL_LOG(FATAL) << "Can't load variable of type " << *this;
}

Node::Node(Interpreter* interpreter, uint64_t file_id, uint64_t node_id)
    : interpreter_(interpreter), id_(file_id, node_id) {
  interpreter->AddNode(file_id, node_id, this);
}

Node::~Node() { interpreter_->RemoveNode(id_.file_id, id_.node_id); }

}  // namespace interpreter
}  // namespace shell
