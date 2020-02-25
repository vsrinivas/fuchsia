// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_INSTRUCTIONS_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_INSTRUCTIONS_H_

#include <memory>
#include <ostream>
#include <string>
#include <string_view>

#include "src/developer/shell/interpreter/src/code.h"
#include "src/developer/shell/interpreter/src/nodes.h"

namespace shell {
namespace interpreter {

// Defines a variable or a constant. Depending on the container, this can be a global or a local
// variable.
class VariableDefinition : public Instruction {
 public:
  VariableDefinition(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                     std::string_view name, std::unique_ptr<Type> type, bool is_mutable,
                     std::unique_ptr<Expression> initial_value = nullptr)
      : Instruction(interpreter, file_id, node_id),
        name_(name),
        type_(std::move(type)),
        is_mutable_(is_mutable),
        initial_value_(std::move(initial_value)) {}

  const std::string& name() const { return name_; }
  const Type* type() const { return type_.get(); }
  bool is_mutable() const { return is_mutable_; }
  const Expression* initial_value() const { return initial_value_.get(); }

  void Dump(std::ostream& os) const override;
  void Compile(ExecutionContext* context, code::Code* code) const override;

 private:
  // Name of the variable.
  const std::string name_;
  // Type of the variable. It can be the undefined type. In that case, the initial value must be
  // defined.
  std::unique_ptr<Type> type_;
  // True if the value associated with this variable can be modified after the variable creation.
  const bool is_mutable_;
  // The initial value for the variable. If the variable is not mutable or if the type is
  // undefined then the initial value must be specified.
  std::unique_ptr<Expression> initial_value_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_INSTRUCTIONS_H_
