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
    case ValueType::kInt8:
      Release();
      int8_value_ = value.int8_value_;
      break;
    case ValueType::kUint8:
      Release();
      uint8_value_ = value.uint8_value_;
      break;
    case ValueType::kInt16:
      Release();
      int16_value_ = value.int16_value_;
      break;
    case ValueType::kUint16:
      Release();
      uint16_value_ = value.uint16_value_;
      break;
    case ValueType::kInt32:
      Release();
      int32_value_ = value.int32_value_;
      break;
    case ValueType::kUint32:
      Release();
      uint32_value_ = value.uint32_value_;
      break;
    case ValueType::kInt64:
      Release();
      int64_value_ = value.int64_value_;
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
    case ValueType::kInt8:
    case ValueType::kUint8:
    case ValueType::kInt16:
    case ValueType::kUint16:
    case ValueType::kInt32:
    case ValueType::kUint32:
    case ValueType::kInt64:
    case ValueType::kUint64:
      break;
    case ValueType::kString:
      string_->Release();
      break;
  }
}

}  // namespace interpreter
}  // namespace shell
