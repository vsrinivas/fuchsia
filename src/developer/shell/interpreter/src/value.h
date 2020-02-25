// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_INTERPRETER_SRC_VALUE_H_
#define SRC_DEVELOPER_SHELL_INTERPRETER_SRC_VALUE_H_

namespace shell {
namespace interpreter {

enum class ValueType {
  // Value is not defined. This is, for example, the case when we try to load a global which doesn't
  // exist.
  kUndef,
  // The value is a 64 bit unsigned integer.
  kUint64
};

// Stores any value manageable by the interpreter.
class Value {
 public:
  Value() = default;

  ValueType type() const { return type_; }
  uint64_t value() const { return value_; }

  // Sets the value.
  void Set(ValueType type, uint64_t value = 0) {
    type_ = type;
    value_ = value;
  }

 private:
  // Current type for the value.
  ValueType type_ = ValueType::kUndef;
  // Current 64 bit value for the value.
  uint64_t value_ = 0;
};

}  // namespace interpreter
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_INTERPRETER_SRC_VALUE_H_
