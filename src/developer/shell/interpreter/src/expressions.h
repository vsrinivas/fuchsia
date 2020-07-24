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

  bool IsConstant() const override { return true; }

  std::unique_ptr<Type> InferType(ExecutionContext* context) const override;

  bool Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const override;

 private:
  // The absolute value for the integer.
  const uint64_t absolute_value_;
  // If true, this is a negative value (-absolute_value_).
  const bool negative_;
};

// Fields of objects.  Objects themselves are expressions.
// Located in this file for proximity to Objects.
class ObjectDeclarationField : public Node {
 public:
  ObjectDeclarationField(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                         std::shared_ptr<ObjectFieldSchema> field_schema,
                         std::unique_ptr<Expression> value)
      : Node(interpreter, file_id, node_id),
        field_schema_(std::move(field_schema)),
        expression_(std::move(value)) {}

  // Prints the field.
  void Dump(std::ostream& os) const;

  const ObjectFieldSchema* schema() const { return field_schema_.get(); }

  bool Compile(ExecutionContext* context, code::Code* code, const Type* for_type) {
    return expression_->Compile(context, code, for_type);
  }

 private:
  std::shared_ptr<ObjectFieldSchema> field_schema_;
  std::unique_ptr<Expression> expression_;
};

inline std::ostream& operator<<(std::ostream& os, const ObjectDeclarationField& field) {
  field.Dump(os);
  return os;
}

// ObjectDeclarations are Objects (whether builtin or FIDL).
class ObjectDeclaration : public Expression {
 public:
  ObjectDeclaration(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                    std::shared_ptr<ObjectSchema> object_schema,
                    std::vector<std::unique_ptr<ObjectDeclarationField>>&& fields);

  const std::vector<std::unique_ptr<ObjectDeclarationField>>& fields() const { return fields_; }

  // Prints the object.
  virtual void Dump(std::ostream& os) const override;

  std::unique_ptr<Type> InferType(ExecutionContext* context) const override;

  // Compiles the instruction (performs the semantic checks and generates code).
  virtual bool Compile(ExecutionContext* context, code::Code* code,
                       const Type* for_type) const override;

 private:
  std::shared_ptr<ObjectSchema> object_schema_;

  // fields_ are stored in the same order the ObjectSchemaFields are found in the object_schema_.
  std::vector<std::unique_ptr<ObjectDeclarationField>> fields_;
};

// Defines a string value.
class StringLiteral : public Expression {
 public:
  StringLiteral(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                std::string_view value)
      : Expression(interpreter, file_id, node_id), string_(interpreter, value) {}

  String* string() const { return string_.data(); }

  void Dump(std::ostream& os) const override;

  bool IsConstant() const override { return true; }

  std::unique_ptr<Type> InferType(ExecutionContext* context) const override;

  bool Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const override;

 private:
  // The value for the string.
  StringContainer string_;
};

class ExpressionVariable : public Expression {
 public:
  ExpressionVariable(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                     std::string&& name)
      : Expression(interpreter, file_id, node_id), name_(std::move(name)) {}

  void Dump(std::ostream& os) const override;

  std::unique_ptr<Type> InferType(ExecutionContext* context) const override;

  bool Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const override;

  void Assign(ExecutionContext* context, code::Code* code) const override;

 private:
  const std::string name_;
};

class BinaryOperation : public Expression {
 public:
  BinaryOperation(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                  std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
      : Expression(interpreter, file_id, node_id),
        left_(std::move(left)),
        right_(std::move(right)) {}

  const Expression* left() const { return left_.get(); }
  const Expression* right() const { return right_.get(); }

  bool IsConstant() const override { return left_->IsConstant() && right_->IsConstant(); }

 private:
  std::unique_ptr<Expression> left_;
  std::unique_ptr<Expression> right_;
};

class Addition : public BinaryOperation {
 public:
  Addition(Interpreter* interpreter, uint64_t file_id, uint64_t node_id, bool with_exceptions,
           std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
      : BinaryOperation(interpreter, file_id, node_id, std::move(left), std::move(right)),
        with_exceptions_(with_exceptions) {}

  bool with_exceptions() const { return with_exceptions_; }

  void Dump(std::ostream& os) const override;

  std::unique_ptr<Type> InferType(ExecutionContext* context) const override;

  bool Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const override;

  size_t GenerateStringTerms(ExecutionContext* context, code::Code* code,
                             const Type* for_type) const override;

 private:
  bool with_exceptions_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_EXPRESSIONS_H_
