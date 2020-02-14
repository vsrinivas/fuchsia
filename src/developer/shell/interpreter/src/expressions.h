// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_EXPRESSIONS_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_EXPRESSIONS_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "src/developer/shell/interpreter/src/nodes.h"

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

 private:
  // The absolute value for the integer.
  const uint64_t absolute_value_;
  // If true, this is a negative value (-absolute_value_).
  const bool negative_;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_EXPRESSIONS_H_
