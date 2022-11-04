// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_value.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/string_util.h"
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
  FX_DCHECK(type_->byte_size() == data_size || type_->byte_size() == 0);

  TaggedData::DataBuffer bytes;
  bytes.resize(data_size);
  memcpy(bytes.data(), data, data_size);
  data_ = TaggedData(std::move(bytes));
}

ExprValue::ExprValue(fxl::RefPtr<Type> type, std::vector<uint8_t> data,
                     const ExprValueSource& source)
    : type_(std::move(type)), source_(source), data_(std::move(data)) {}

ExprValue::ExprValue(fxl::RefPtr<Type> type, TaggedData buffer, const ExprValueSource& source)
    : type_(std::move(type)), source_(source), data_(std::move(buffer)) {}

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
  const BaseType* base_type = type_->StripCVT()->As<BaseType>();
  if (!base_type)
    return BaseType::kBaseTypeNone;
  return base_type->base_type();
}

Err ExprValue::EnsureAllValid() const {
  if (!data_.all_valid())
    return Err::OptimizedOut();
  return Err();
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
  FX_DCHECK(data_.size() == sizeof(int8_t)) << "Got size of " << data_.size();
  int8_t result;
  memcpy(&result, data_.bytes().data(), sizeof(int8_t));
  return result;
}

template <>
uint8_t ExprValue::GetAs<uint8_t>() const {
  FX_DCHECK(data_.size() == sizeof(uint8_t)) << "Got size of " << data_.size();
  uint8_t result;
  memcpy(&result, data_.bytes().data(), sizeof(uint8_t));
  return result;
}

template <>
int16_t ExprValue::GetAs<int16_t>() const {
  FX_DCHECK(data_.size() == sizeof(int16_t)) << "Got size of " << data_.size();
  int16_t result;
  memcpy(&result, data_.bytes().data(), sizeof(int16_t));
  return result;
}

template <>
uint16_t ExprValue::GetAs<uint16_t>() const {
  FX_DCHECK(data_.size() == sizeof(uint16_t)) << "Got size of " << data_.size();
  uint16_t result;
  memcpy(&result, data_.bytes().data(), sizeof(uint16_t));
  return result;
}

template <>
int32_t ExprValue::GetAs<int32_t>() const {
  FX_DCHECK(data_.size() == sizeof(int32_t)) << "Got size of " << data_.size();
  int32_t result;
  memcpy(&result, data_.bytes().data(), sizeof(int32_t));
  return result;
}

template <>
uint32_t ExprValue::GetAs<uint32_t>() const {
  FX_DCHECK(data_.size() == sizeof(uint32_t)) << "Got size of " << data_.size();
  uint32_t result;
  memcpy(&result, data_.bytes().data(), sizeof(uint32_t));
  return result;
}

template <>
int64_t ExprValue::GetAs<int64_t>() const {
  FX_DCHECK(data_.size() == sizeof(int64_t)) << "Got size of " << data_.size();
  int64_t result;
  memcpy(&result, data_.bytes().data(), sizeof(int64_t));
  return result;
}

template <>
uint64_t ExprValue::GetAs<uint64_t>() const {
  FX_DCHECK(data_.size() == sizeof(uint64_t)) << "Got size of " << data_.size();
  uint64_t result;
  memcpy(&result, data_.bytes().data(), sizeof(uint64_t));
  return result;
}

template <>
int128_t ExprValue::GetAs<int128_t>() const {
  FX_DCHECK(data_.size() == sizeof(int128_t)) << "Got size of " << data_.size();
  int128_t result;
  memcpy(&result, data_.bytes().data(), sizeof(int128_t));
  return result;
}

template <>
uint128_t ExprValue::GetAs<uint128_t>() const {
  FX_DCHECK(data_.size() == sizeof(uint128_t)) << "Got size of " << data_.size();
  uint128_t result;
  memcpy(&result, data_.bytes().data(), sizeof(uint128_t));
  return result;
}

template <>
float ExprValue::GetAs<float>() const {
  FX_DCHECK(data_.size() == sizeof(float)) << "Got size of " << data_.size();
  float result;
  memcpy(&result, data_.bytes().data(), sizeof(float));
  return result;
}

template <>
double ExprValue::GetAs<double>() const {
  FX_DCHECK(data_.size() == sizeof(double)) << "Got size of " << data_.size();
  double result;
  memcpy(&result, data_.bytes().data(), sizeof(double));
  return result;
}

Err ExprValue::PromoteTo64(int64_t* output) const {
  if (data_.empty())
    return Err("Value has no data.");
  if (!data_.all_valid())
    return Err::OptimizedOut();

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
  if (!data_.all_valid())
    return Err::OptimizedOut();

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

Err ExprValue::PromoteTo128(int128_t* output) const {
  if (!data_.all_valid())
    return Err::OptimizedOut();

  if (data_.size() == sizeof(int128_t)) {
    *output = GetAs<int128_t>();
    return Err();
  }

  // Use PromoteTo64 to handle all other cases.
  int64_t out64 = 0;
  if (Err err = PromoteTo64(&out64); err.has_error())
    return err;

  *output = out64;
  return Err();
}

Err ExprValue::PromoteTo128(uint128_t* output) const {
  if (!data_.all_valid())
    return Err::OptimizedOut();

  if (data_.size() == sizeof(uint128_t)) {
    *output = GetAs<uint128_t>();
    return Err();
  }

  // Use PromoteTo64 to handle all other cases.
  uint64_t out64 = 0;
  if (Err err = PromoteTo64(&out64); err.has_error())
    return err;

  *output = out64;
  return Err();
}

Err ExprValue::PromoteToDouble(double* output) const {
  if (data_.empty())
    return Err("Value has no data.");
  if (!data_.all_valid())
    return Err::OptimizedOut();

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

std::ostream& operator<<(std::ostream& out, const ExprValue& value) {
  if (!value.type())
    return out << "{null ExprValue}";

  out << value.type()->GetFullName() << "(";

  std::string value_str;

  const Type* type = value.type()->StripCVT();
  if (const BaseType* base = type->As<BaseType>()) {
    switch (base->base_type()) {
      case BaseType::kBaseTypeBoolean: {
        uint64_t as_unsigned = 0;
        if (value.PromoteTo64(&as_unsigned).ok())
          value_str = as_unsigned ? "true" : "false";
        break;
      }
      // Ignore kBaseTypeAddress which we don't use (pointers are "ModifiedTypes").
      case BaseType::kBaseTypeFloat: {
        double as_double = 0.0;
        if (value.PromoteToDouble(&as_double).ok())
          value_str = std::to_string(as_double);
        break;
      }
      case BaseType::kBaseTypeSigned:
      case BaseType::kBaseTypeSignedChar: {
        int64_t as_signed = 0;
        if (value.PromoteTo64(&as_signed).ok())
          value_str = std::to_string(as_signed);
        break;
      }
      case BaseType::kBaseTypeUnsigned:
      case BaseType::kBaseTypeUnsignedChar: {
        uint64_t as_unsigned = 0;
        if (value.PromoteTo64(&as_unsigned).ok())
          value_str = std::to_string(as_unsigned);
        break;
      }
    }
  }

  if (value_str.empty()) {
    // This catches anything that's not a base type and anything that errored out above. Output as a
    // hex dump.
    const auto& bytes = value.data().bytes();
    for (size_t i = 0; i < bytes.size(); i++) {
      if (i > 0)
        value_str.push_back(' ');
      value_str.append(to_hex_string(bytes[i], 2, true));
    }
  }

  out << value_str << ")";
  return out;
}

}  // namespace zxdb
