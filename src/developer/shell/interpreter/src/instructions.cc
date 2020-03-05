// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/instructions.h"

#include <memory>
#include <ostream>

#include "src/developer/shell/interpreter/src/interpreter.h"

namespace shell {
namespace interpreter {

void VariableDefinition::Dump(std::ostream& os) const {
  os << (is_mutable_ ? "var " : "const ") << name_;
  if (!type_->IsUndefined()) {
    os << ": " << *type_;
  }
  if (initial_value_ != nullptr) {
    os << " = " << *initial_value_;
  }
  os << '\n';
}

void VariableDefinition::Compile(ExecutionContext* context, code::Code* code) {
  // The sever only creates a VariableDefinition if the type is defined.
  FX_DCHECK(!type_->IsUndefined());
  // Currently, we only create the variable within the global scope.
  const Variable* existing = context->interpreter()->isolate()->global_scope()->GetVariable(name_);
  if (existing != nullptr) {
    context->EmitError(id(), "Variable '" + name_ + "' already defined.");
    context->EmitError(existing->id(), "First definition.");
    return;
  }
  Variable* variable = type_->CreateVariable(
      context, context->interpreter()->isolate()->global_scope(), id(), name_);
  if (variable == nullptr) {
    return;
  }
  if (initial_value_ == nullptr) {
    type_->GenerateDefaultValue(context, code);
  } else {
    initial_value_->Compile(context, code, type_.get());
  }
  index_ = variable->index();
  code->StoreRaw(index_, type_->Size());
}

}  // namespace interpreter
}  // namespace shell
