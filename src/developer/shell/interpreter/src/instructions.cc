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

void VariableDefinition::Compile(ExecutionContext* context) const {
  // Currently, we only create the variable within the global scope.
  const Type* inferred_type = type_.get();
  std::unique_ptr<Type> new_type;
  if (type_->IsUndefined()) {
    if (initial_value_ == nullptr) {
      context->EmitError(id(),
                         "At least the type or the initial value must defined for a variable.");
      return;
    }
    new_type = initial_value_->GetType();
    if (new_type->IsUndefined()) {
      context->EmitError(id(), "Can't infer type for this variable.");
      return;
    }
    inferred_type = new_type.get();
  }
  Variable* existing = context->interpreter()->isolate()->global_scope()->GetVariable(name_);
  if (existing != nullptr) {
    context->EmitError(id(), "Variable '" + name_ + "' already defined.");
    context->EmitError(existing->id(), "First definition.");
    return;
  }
  inferred_type->CreateVariable(context, context->interpreter()->isolate()->global_scope(), id(),
                                name_);
}

}  // namespace interpreter
}  // namespace shell
