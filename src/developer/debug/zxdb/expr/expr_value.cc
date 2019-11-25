// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_value.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

ExprValue::ExprValue() = default;

ExprValue::ExprValue(bool value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeBoolean, "bool", &value, sizeof(bool), source) {
}
ExprValue::ExprValue(int8_t value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeSigned, "int8_t", &value, sizeof(int8_t),
                source) {}
ExprValue::ExprValue(uint8_t value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeUnsigned, "uint8_t", &value, sizeof(uint8_t),
                source) {}
ExprValue::ExprValue(int16_t value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeSigned, "int16_t", &value, sizeof(int16_t),
                source) {}
ExprValue::ExprValue(uint16_t value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeUnsigned, "uint16_t", &value, sizeof(uint16_t),
                source) {}
ExprValue::ExprValue(int32_t value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeSigned, "int32_t", &value, sizeof(int32_t),
                source) {}
ExprValue::ExprValue(uint32_t value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeUnsigned, "uint32_t", &value, sizeof(uint32_t),
                source) {}
ExprValue::ExprValue(int64_t value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeSigned, "int64_t", &value, sizeof(int64_t),
                source) {}
ExprValue::ExprValue(uint64_t value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeUnsigned, "uint64_t", &value, sizeof(uint64_t),
                source) {}
ExprValue::ExprValue(float value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeFloat, "float", &value, sizeof(float), source) {
}
ExprValue::ExprValue(double value, fxl::RefPtr<Type> type, const ExprValueSource& source)
    : ExprValue(std::move(type), BaseType::kBaseTypeFloat, "double", &value, sizeof(double),
                source) {}

ExprValue::ExprValue(fxl::RefPtr<Type> optional_type, int base_type, const char* type_name,
                     void* data, uint32_t data_size, const ExprValueSource& source)
    : type_(optional_type ? std::move(optional_type)
                          : fxl::RefPtr<Type>(
                                fxl::MakeRefCounted<BaseType>(base_type, data_size, type_name))),
      source_(source) {
  // The type that we made or were given should match the size of the input data. But also allow
  // 0-sized types since the input type may not be concrete.
  FXL_DCHECK(type_->byte_size() == data_size || type_->byte_size() == 0);

  data_.resize(data_size);
  memcpy(&data_[0], data, data_size);
}

ExprValue::ExprValue(fxl::RefPtr<Type> type, std::vector<uint8_t> data,
                     const ExprValueSource& source)
    : type_(std::move(type)), source_(source), data_(data) {}

ExprValue::~ExprValue() = default;

bool ExprValue::operator==(const ExprValue& other) const {
  // Currently this does a comparison of the raw bytes of the value. This will be fine for most
  // primitive values but will be incorrect for some composite structs.
  return data_ == other.data_;
}

int ExprValue::GetBaseType() const {
  if (!type_)
    return BaseType::kBaseTypeNone;

  // Remove "const", etc. and see if it's a base type.
  const BaseType* base_type = type_->StripCVT()->AsBaseType();
  if (!base_type)
    return BaseType::kBaseTypeNone;
  return base_type->base_type();
}

fxl::RefPtr<Type> ExprValue::GetConcreteType(EvalContext* context) const {
  return context->GetConcreteType(type_.get());
}

Err ExprValue::EnsureSizeIs(size_t size) const {
  if (data_.size() != size) {
    return Err(
        fxl::StringPrintf("The value of type '%s' is the incorrect size "
                          "(expecting %zu, got %zu). Please file a bug.",
                          type_ ? type_->GetFullName().c_str() : "<unknown>", size, data_.size()));
  }
  return Err();
}

template <>
int8_t ExprValue::GetAs<int8_t>() const {
  FXL_DCHECK(data_.size() == sizeof(int8_t)) << "Got size of " << data_.size();
  int8_t result;
  memcpy(&result, &data_[0], sizeof(int8_t));
  return result;
}

template <>
uint8_t ExprValue::GetAs<uint8_t>() const {
  FXL_DCHECK(data_.size() == sizeof(uint8_t)) << "Got size of " << data_.size();
  uint8_t result;
  memcpy(&result, &data_[0], sizeof(uint8_t));
  return result;
}

template <>
int16_t ExprValue::GetAs<int16_t>() const {
  FXL_DCHECK(data_.size() == sizeof(int16_t)) << "Got size of " << data_.size();
  int16_t result;
  memcpy(&result, &data_[0], sizeof(int16_t));
  return result;
}

template <>
uint16_t ExprValue::GetAs<uint16_t>() const {
  FXL_DCHECK(data_.size() == sizeof(uint16_t)) << "Got size of " << data_.size();
  uint16_t result;
  memcpy(&result, &data_[0], sizeof(uint16_t));
  return result;
}

template <>
int32_t ExprValue::GetAs<int32_t>() const {
  FXL_DCHECK(data_.size() == sizeof(int32_t)) << "Got size of " << data_.size();
  int32_t result;
  memcpy(&result, &data_[0], sizeof(int32_t));
  return result;
}

template <>
uint32_t ExprValue::GetAs<uint32_t>() const {
  FXL_DCHECK(data_.size() == sizeof(uint32_t)) << "Got size of " << data_.size();
  uint32_t result;
  memcpy(&result, &data_[0], sizeof(uint32_t));
  return result;
}

template <>
int64_t ExprValue::GetAs<int64_t>() const {
  FXL_DCHECK(data_.size() == sizeof(int64_t)) << "Got size of " << data_.size();
  int64_t result;
  memcpy(&result, &data_[0], sizeof(int64_t));
  return result;
}

template <>
uint64_t ExprValue::GetAs<uint64_t>() const {
  FXL_DCHECK(data_.size() == sizeof(uint64_t)) << "Got size of " << data_.size();
  uint64_t result;
  memcpy(&result, &data_[0], sizeof(uint64_t));
  return result;
}

template <>
float ExprValue::GetAs<float>() const {
  FXL_DCHECK(data_.size() == sizeof(float)) << "Got size of " << data_.size();
  float result;
  memcpy(&result, &data_[0], sizeof(float));
  return result;
}

template <>
double ExprValue::GetAs<double>() const {
  FXL_DCHECK(data_.size() == sizeof(double)) << "Got size of " << data_.size();
  double result;
  memcpy(&result, &data_[0], sizeof(double));
  return result;
}

Err ExprValue::PromoteTo64(int64_t* output) const {
  if (data_.empty())
    return Err("Value has no data.");
  switch (data_.size()) {
    case sizeof(int8_t):
      *output = GetAs<int8_t>();
      break;
    case sizeof(int16_t):
      *output = GetAs<int16_t>();
      break;
    case sizeof(int32_t):
      *output = GetAs<int32_t>();
      break;
    case sizeof(int64_t):
      *output = GetAs<int64_t>();
      break;
    default:
      return Err(
          fxl::StringPrintf("Unexpected value size (%zu), please file a bug.", data_.size()));
  }
  return Err();
}

Err ExprValue::PromoteTo64(uint64_t* output) const {
  if (data_.empty())
    return Err("Value has no data.");
  switch (data_.size()) {
    case sizeof(uint8_t):
      *output = GetAs<uint8_t>();
      break;
    case sizeof(uint16_t):
      *output = GetAs<uint16_t>();
      break;
    case sizeof(uint32_t):
      *output = GetAs<uint32_t>();
      break;
    case sizeof(uint64_t):
      *output = GetAs<uint64_t>();
      break;
    default:
      return Err(
          fxl::StringPrintf("Unexpected value size (%zu), please file a bug.", data_.size()));
  }
  return Err();
}

Err ExprValue::PromoteToDouble(double* output) const {
  if (data_.empty())
    return Err("Value has no data.");
  switch (data_.size()) {
    case sizeof(float):
      *output = GetAs<float>();
      break;
    case sizeof(double):
      *output = GetAs<double>();
      break;
    default:
      return Err(
          fxl::StringPrintf("Unexpected value size (%zu), please file a bug.", data_.size()));
  }
  return Err();
}

}  // namespace zxdb
