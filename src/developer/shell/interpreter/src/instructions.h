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
  size_t index() const { return index_; }

  const VariableDefinition* AsVariableDefinition() const override { return this; }

  void Dump(std::ostream& os) const override;
  void Compile(ExecutionContext* context, code::Code* code) override;

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
  // Index (in bytes) of the variable relative to the execution scope which defines it.
  size_t index_ = 0;
};

// Emits a result. A value is computed and then sent back to the client using OnResult.
class EmitResult : public Instruction {
 public:
  EmitResult(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
             std::unique_ptr<Expression> expression)
      : Instruction(interpreter, file_id, node_id), expression_(std::move(expression)) {}

  const Expression* expression() const { return expression_.get(); }

  void Dump(std::ostream& os) const override;
  void Compile(ExecutionContext* context, code::Code* code) override;

 private:
  // The expression we want to compute and send back to the client.
  std::unique_ptr<Expression> expression_;
};

class Assignment : public Instruction {
 public:
  Assignment(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
             std::unique_ptr<Expression> destination, std::unique_ptr<Expression> source)
      : Instruction(interpreter, file_id, node_id),
        destination_(std::move(destination)),
        source_(std::move(source)) {}

  const Expression* destination() const { return destination_.get(); }
  const Expression* source() const { return source_.get(); }

  void Dump(std::ostream& os) const override;
  void Compile(ExecutionContext* context, code::Code* code) override;

 private:
  std::unique_ptr<Expression> destination_;
  std::unique_ptr<Expression> source_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_INSTRUCTIONS_H_
