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

// - Type ------------------------------------------------------------------------------------------

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

bool Type::GenerateIntegerLiteral(ExecutionContext* context, code::Code* code,
                                  const IntegerLiteral* literal) const {
  std::stringstream ss;
  ss << "Can't create an integer literal of type " << *this << '.';
  context->EmitError(literal->id(), ss.str());
  return false;
}

bool Type::GenerateStringLiteral(ExecutionContext* context, code::Code* code,
                                 const StringLiteral* literal) const {
  std::stringstream ss;
  ss << "Can't create a string literal of type " << *this << '.';
  context->EmitError(literal->id(), ss.str());
  return false;
}

bool Type::GenerateVariable(ExecutionContext* context, code::Code* code, const NodeId& id,
                            const Variable* variable) const {
  std::stringstream ss;
  ss << "Can't use variable of type " << *this << ".";
  context->EmitError(id, ss.str());
  return false;
}

void Type::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  FX_LOGS(FATAL) << "Can't load variable of type " << *this;
}

// - Node ------------------------------------------------------------------------------------------

Node::Node(Interpreter* interpreter, uint64_t file_id, uint64_t node_id)
    : interpreter_(interpreter), id_(file_id, node_id) {
  interpreter->AddNode(file_id, node_id, this);
}

Node::~Node() { interpreter_->RemoveNode(id_.file_id, id_.node_id); }

}  // namespace interpreter
}  // namespace shell
