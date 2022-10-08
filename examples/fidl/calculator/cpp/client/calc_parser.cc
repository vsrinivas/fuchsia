// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/fidl/calculator/cpp/client/calc_parser.h"

#include <iostream>

// A very brittle parser for input to the calculator, and not thread safe. This will eventually go
// away when we can use dynamic input to components, e.g. with `ffx component explore`
namespace calc {

// Default constructor that takes in a string with the input in the form:
// "<floating point number> <operator [+,-,/,^]> <floating point number>"
Expression::Expression(const std::string &input_text) {
  // Initialize the member variables
  left_ = 0;
  right_ = 0;
  operator_ = Operator::PlaceHolderError;

  // Don't attempt to parse if the string is empty
  if (input_text.empty() || input_text == " " || input_text == "\n") {
    return;
  }

  size_t index = 0;
  // Find the first space, which indicates the end of the first floating point number.  Don't
  // iterate past the end of the string.
  while (input_text[index] != ' ' && (index + 1) < input_text.length()) {
    index++;
  }
  // If the loop reached the end of the string, we had invalid input.
  if ((index + 1) >= input_text.length()) {
    operator_ = Operator::PlaceHolderError;
    left_ = 0;
    right_ = 0;
    return;
  }

  // Parse the floating point number
  left_ = atof(input_text.substr(0, index + 1).c_str());

  // Move forward to the operator
  index++;
  // Set the Operator
  switch (input_text[index]) {
    case '+':
      operator_ = Operator::Add;
      break;
    case '-':
      operator_ = Operator::Subtract;
      break;
    case '/':
      operator_ = Operator::Divide;
      break;
    case '^':
      operator_ = Operator::Pow;
      break;
    case '*':
      operator_ = Operator::Multiply;
      break;
    default:
      std::string error_note("unkown input character '");
      error_note += input_text[index];
      // Just print to stdout, as we can't return an error code and don't want to throw an exception
      std::cout << error_note << std::endl;
      operator_ = Operator::PlaceHolderError;
      left_ = 0;
      right_ = 0;
      return;
  }
  // Move forward to the right operand
  index++;
  auto right_start = index++;
  // Parse the right operand
  right_ = atof(input_text.substr(right_start, input_text.length()).c_str());
}

Expression::Expression(double left, Operator op, double right)
    : left_(left), operator_(op), right_(right) {}
}  // namespace calc
