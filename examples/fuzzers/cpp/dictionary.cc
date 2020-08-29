// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A fuzzer that uses a dictionary finds a divide-by-zero.

#include <stddef.h>
#include <stdint.h>

#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// The code under test. Normally this would be in a separate library.
namespace {

// Parses select words into integers.
bool parse_num(const std::string &token, int *out) {
  if (token == "zero") {
    *out = 0;
  } else if (token == "one") {
    *out = 1;
  } else if (token == "two") {
    *out = 2;
  } else {
    return false;
  }
  return true;
}

// Calculates a result from a string like "add one to two".
bool calculate(const std::string &input, int *result) {
  std::istringstream iss(input);
  std::vector<std::string> tokens{std::istream_iterator<std::string>{iss},
                                  std::istream_iterator<std::string>{}};
  int op1, op2;
  if (tokens.size() != 4 || !parse_num(tokens[1], &op1) || !parse_num(tokens[3], &op2)) {
    return false;
  }
  if (tokens[0] == "add" && tokens[2] == "to") {
    *result = op1 + op2;
  } else if (tokens[0] == "subtract" && tokens[2] == "from") {
    *result = op2 - op1;
  } else if (tokens[0] == "multiply" && tokens[2] == "by") {
    *result = op1 * op2;
  } else if (tokens[0] == "divide" && tokens[2] == "by") {
    *result = op1 / op1;
  } else {
    return false;
  }
  return true;
}

}  // namespace

// The fuzz target function
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::string input(reinterpret_cast<const char *>(data), size);
  int result;
  calculate(input, &result);
  return 0;
}
