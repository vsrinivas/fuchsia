// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.g

#ifndef EXAMPLES_FIDL_CALCULATOR_CPP_CLIENT_CALC_PARSER_H_
#define EXAMPLES_FIDL_CALCULATOR_CPP_CLIENT_CALC_PARSER_H_

#include <string>
#include <vector>

namespace calc {

// An enum to indicate which operation to perform. PlaceHolderError indicates parsing failed.
enum class Operator {
  Add,
  Subtract,
  Multiply,
  Divide,
  Pow,
  PlaceHolderError,
};

// A very brittle parser for input to the calculator, and not thread safe. This will eventually go
// away when we can use dynamic input to components, e.g. with `ffx component explore`
class Expression {
 public:
  explicit Expression(const std::string &input_text);
  Expression(double left, Operator op, double right);

  double GetLeft() const { return left_; }
  Operator GetOperator() const { return operator_; }
  double GetRight() const { return right_; }

 private:
  double left_;
  Operator operator_;
  double right_;
};

}  // namespace calc

#endif  // EXAMPLES_FIDL_CALCULATOR_CPP_CLIENT_CALC_PARSER_H_
