// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_EXPRESSIONS_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_EXPRESSIONS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "src/developer/shell/interpreter/src/nodes.h"
#include "src/developer/shell/interpreter/src/types.h"
#include "src/developer/shell/interpreter/src/value.h"

namespace shell {
namespace interpreter {

// Defines an integer value.
class IntegerLiteral : public Expression {
 public:
  IntegerLiteral(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                 uint64_t absolute_value, bool negative)
      : Expression(interpreter, file_id, node_id),
        absolute_value_(absolute_value),
        negative_(negative) {}

  uint64_t absolute_value() const { return absolute_value_; }
  bool negative() const { return negative_; }

  void Dump(std::ostream& os) const override;

  bool Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const override;

 private:
  // The absolute value for the integer.
  const uint64_t absolute_value_;
  // If true, this is a negative value (-absolute_value_).
  const bool negative_;
};

// Fields of objects.  Objects themselves are expressions.
// Located in this file for proximity to Objects.
class ObjectField : public Node {
 public:
  ObjectField(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
              std::unique_ptr<Expression> value)
      : Node(interpreter, file_id, node_id), expression_(std::move(value)) {}

  // Prints the field.
  void Dump(std::ostream& os) const;

 private:
  std::unique_ptr<Type> type_;
  std::unique_ptr<Expression> expression_;
};

inline std::ostream& operator<<(std::ostream& os, const ObjectField& field) {
  field.Dump(os);
  return os;
}

// Objects are Objects (whether builtin or FIDL).
class Object : public Expression {
 public:
  Object(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
         std::unique_ptr<TypeObject> type, std::vector<std::unique_ptr<ObjectField>>&& fields)
      : Expression(interpreter, file_id, node_id),
        type_(std::move(type)),
        fields_(std::move(fields)) {}

  // Prints the object.
  virtual void Dump(std::ostream& os) const override;

  // Compiles the instruction (performs the semantic checks and generates code).
  virtual bool Compile(ExecutionContext* context, code::Code* code,
                       const Type* for_type) const override;

 private:
  std::unique_ptr<TypeObject> type_;
  std::vector<std::unique_ptr<ObjectField>> fields_;
};

// Defines a string value.
class StringLiteral : public Expression {
 public:
  StringLiteral(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                std::string_view value)
      : Expression(interpreter, file_id, node_id), string_(value) {}

  String* string() const { return string_.data(); }

  void Dump(std::ostream& os) const override;

  bool Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const override;

 private:
  // The value for the string.
  StringContainer string_;
};

class ExpressionVariable : public Expression {
 public:
  ExpressionVariable(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                     NodeId variable_definition)
      : Expression(interpreter, file_id, node_id), variable_definition_(variable_definition) {}

  void Dump(std::ostream& os) const override;

  bool Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const override;

 private:
  const NodeId variable_definition_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_EXPRESSIONS_H_
