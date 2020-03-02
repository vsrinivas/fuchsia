// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_EXPRESSIONS_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_EXPRESSIONS_H_

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "src/developer/shell/interpreter/src/nodes.h"
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

  std::unique_ptr<Type> GetType() const override;
  void Dump(std::ostream& os) const override;

  void Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const override;

 private:
  // The absolute value for the integer.
  const uint64_t absolute_value_;
  // If true, this is a negative value (-absolute_value_).
  const bool negative_;
};

// Defines a string value.
class StringLiteral : public Expression {
 public:
  StringLiteral(Interpreter* interpreter, uint64_t file_id, uint64_t node_id,
                std::string_view value)
      : Expression(interpreter, file_id, node_id), string_(value) {}

  String* string() const { return string_.data(); }

  std::unique_ptr<Type> GetType() const override;
  void Dump(std::ostream& os) const override;

  void Compile(ExecutionContext* context, code::Code* code, const Type* for_type) const override;

 private:
  // The value for the string.
  StringContainer string_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_EXPRESSIONS_H_
