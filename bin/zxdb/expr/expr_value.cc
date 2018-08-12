// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr_value.h"

#include "garnet/bin/zxdb/client/symbols/base_type.h"

namespace zxdb {

ExprValue::ExprValue() = default;

ExprValue::ExprValue(int8_t value)
    : ExprValue(BaseType::kBaseTypeSigned, "int8_t", &value, 1) {}
ExprValue::ExprValue(uint8_t value)
    : ExprValue(BaseType::kBaseTypeUnsigned, "uint8_t", &value, 1) {}
ExprValue::ExprValue(int16_t value)
    : ExprValue(BaseType::kBaseTypeSigned, "int16_t", &value, 2) {}
ExprValue::ExprValue(uint16_t value)
    : ExprValue(BaseType::kBaseTypeUnsigned, "uint16_t", &value, 2) {}
ExprValue::ExprValue(int32_t value)
    : ExprValue(BaseType::kBaseTypeSigned, "int32_t", &value, 4) {}
ExprValue::ExprValue(uint32_t value)
    : ExprValue(BaseType::kBaseTypeUnsigned, "uint32_t", &value, 4) {}
ExprValue::ExprValue(int64_t value)
    : ExprValue(BaseType::kBaseTypeSigned, "int64_t", &value, 8) {}
ExprValue::ExprValue(uint64_t value)
    : ExprValue(BaseType::kBaseTypeUnsigned, "uint64_t", &value, 8) {}

ExprValue::ExprValue(int base_type, const char* type_name, void* data,
                     uint32_t data_size)
    : type_(CreateSyntheticBaseType(base_type, type_name, data_size)) {
  data_.resize(data_size);
  memcpy(&data_[0], data, data_size);
}

ExprValue::ExprValue(fxl::RefPtr<Type> type, std::vector<uint8_t> data)
    : type_(type), data_(data) {}

ExprValue::~ExprValue() = default;

bool ExprValue::operator==(const ExprValue& other) const {
  // Currently this does a comparison of the raw bytes oif the value. This
  // will be fine for most primitive values but will be incorrect for some
  // composite structs.
  return data_ == other.data_;
}

int ExprValue::GetBaseType() const {
  if (!type_)
    return BaseType::kBaseTypeNone;

  // TODO(brettw) this should skip over "const" modifiers. Need to check
  // typedefs. And C++ references need to be handled.
  BaseType* base_type = type_->AsBaseType();
  if (!base_type)
    return BaseType::kBaseTypeNone;
  return base_type->base_type();
}

// static
fxl::RefPtr<BaseType> ExprValue::CreateSyntheticBaseType(int base_type,
                                                         const char* type_name,
                                                         uint32_t byte_size) {
  auto result = fxl::MakeRefCounted<BaseType>();
  result->set_assigned_name(type_name);
  result->set_base_type(base_type);
  result->set_byte_size(byte_size);
  return result;
}

template <>
int8_t ExprValue::GetAs<int8_t>() const {
  FXL_DCHECK(data_.size() == 1);
  int8_t result;
  memcpy(&result, &data_[0], 1);
  return result;
}

template <>
uint8_t ExprValue::GetAs<uint8_t>() const {
  FXL_DCHECK(data_.size() == 1);
  uint8_t result;
  memcpy(&result, &data_[0], 1);
  return result;
}

template <>
int16_t ExprValue::GetAs<int16_t>() const {
  FXL_DCHECK(data_.size() == 2);
  int16_t result;
  memcpy(&result, &data_[0], 2);
  return result;
}

template <>
uint16_t ExprValue::GetAs<uint16_t>() const {
  FXL_DCHECK(data_.size() == 2);
  uint16_t result;
  memcpy(&result, &data_[0], 2);
  return result;
}

template <>
int32_t ExprValue::GetAs<int32_t>() const {
  FXL_DCHECK(data_.size() == 4);
  int32_t result;
  memcpy(&result, &data_[0], 4);
  return result;
}

template <>
uint32_t ExprValue::GetAs<uint32_t>() const {
  FXL_DCHECK(data_.size() == 4);
  uint32_t result;
  memcpy(&result, &data_[0], 4);
  return result;
}

template <>
int64_t ExprValue::GetAs<int64_t>() const {
  FXL_DCHECK(data_.size() == 8);
  int64_t result;
  memcpy(&result, &data_[0], 8);
  return result;
}

template <>
uint64_t ExprValue::GetAs<uint64_t>() const {
  FXL_DCHECK(data_.size() == 8);
  uint64_t result;
  memcpy(&result, &data_[0], 8);
  return result;
}

template <>
float ExprValue::GetAs<float>() const {
  FXL_DCHECK(data_.size() == 4);
  float result;
  memcpy(&result, &data_[0], 4);
  return result;
}

template <>
double ExprValue::GetAs<double>() const {
  FXL_DCHECK(data_.size() == 8);
  double result;
  memcpy(&result, &data_[0], 8);
  return result;
}

}  // namespace zxdb
