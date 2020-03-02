// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/value.h"

#include <cstdint>
#include <string>

namespace shell {
namespace interpreter {

void Value::Set(const Value& value) {
  // For types which need to take a reference, we take the reference before we release the old
  // value. This way, we don't end with a reference to a destroyed value if the user assigns a
  // value to itself.
  switch (value.type_) {
    case ValueType::kUndef:
      Release();
      break;
    case ValueType::kUint64:
      Release();
      uint64_value_ = value.uint64_value_;
      break;
    case ValueType::kString:
      String* string = value.string_->Use();
      Release();
      string_ = string;
      break;
  }
  type_ = value.type_;
}

void Value::Release() {
  switch (type_) {
    case ValueType::kUndef:
      break;
    case ValueType::kUint64:
      break;
    case ValueType::kString:
      string_->Release();
      break;
  }
}

}  // namespace interpreter
}  // namespace shell
