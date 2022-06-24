// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_stack_entry.h"

#include <float.h>
#include <lib/syslog/cpp/macros.h>
#include <math.h>

#include <algorithm>
#include <iostream>

namespace zxdb {

DwarfStackEntry::DwarfStackEntry(uint128_t generic_value) : unsigned_value_(generic_value) {}

DwarfStackEntry::DwarfStackEntry(fxl::RefPtr<BaseType> type, int128_t value)
    : type_(std::move(type)), signed_value_(value) {
  FX_DCHECK(TreatAsSigned());
}

DwarfStackEntry::DwarfStackEntry(fxl::RefPtr<BaseType> type, uint128_t value)
    : type_(std::move(type)), unsigned_value_(value) {
  FX_DCHECK(TreatAsUnsigned());
}

DwarfStackEntry::DwarfStackEntry(fxl::RefPtr<BaseType> type, float value)
    : type_(std::move(type)), float_value_(value) {
  FX_DCHECK(TreatAsFloat());
}

DwarfStackEntry::DwarfStackEntry(fxl::RefPtr<BaseType> type, double value)
    : type_(std::move(type)), double_value_(value) {
  FX_DCHECK(TreatAsDouble());
}

bool DwarfStackEntry::operator==(const DwarfStackEntry& other) const {
  if (is_generic() != other.is_generic())
    return false;

  if (!is_generic()) {
    // Validate base type and byte size.
    if (type_->base_type() != other.type_->base_type() ||
        type_->byte_size() != other.type_->byte_size())
      return false;
  }

  if (TreatAsUnsigned())
    return unsigned_value_ == other.unsigned_value_;
  if (TreatAsSigned())
    return signed_value_ == other.signed_value_;

  // This is used for tests that compare the results of expressions. The floating-point error can
  // accumulate much larger than DBL_EPSILON so we have our own more permissive value. If necessary,
  // this can get much fancier, gtest does some more rigorous comparisons in its ASSERT_DOUBLE_EQ.
  constexpr double kEpsilon = 0.000000001;

  if (TreatAsFloat()) {
    if (isnan(float_value_) || isnan(other.float_value_))
      return false;
    return fabsf(float_value_ - other.float_value_) < kEpsilon;
  }
  if (TreatAsDouble()) {
    if (isnan(double_value_) || isnan(other.double_value_))
      return false;
    return fabs(double_value_ - other.double_value_) < kEpsilon;
  }

  FX_NOTREACHED();
  return false;
}

size_t DwarfStackEntry::GetByteSize() const {
  if (type_) {
    // In case the type info specifies something like a 256 bit integer, clamp the size to the
    // maximum size of our data.
    return std::min<size_t>(sizeof(UnsignedType), type_->byte_size());
  }
  return sizeof(UnsignedType);
}

// static
bool DwarfStackEntry::TreatAsSigned(const BaseType* type) {
  if (!type)
    return false;  // Generic types are unsigned.
  return type->base_type() == BaseType::kBaseTypeSigned ||
         type->base_type() == BaseType::kBaseTypeSignedChar;
}

// static
bool DwarfStackEntry::TreatAsUnsigned(const BaseType* type) {
  if (!type)
    return true;  // Generic types are unsigned.
  return type->base_type() == BaseType::kBaseTypeAddress ||
         type->base_type() == BaseType::kBaseTypeBoolean ||
         type->base_type() == BaseType::kBaseTypeUnsigned ||
         type->base_type() == BaseType::kBaseTypeUnsignedChar ||
         type->base_type() == BaseType::kBaseTypeUTF;
}

// static
bool DwarfStackEntry::TreatAsFloat(const BaseType* type) {
  if (!type)
    return false;  // Generic types are unsigned.
  return type->base_type() == BaseType::kBaseTypeFloat && type->byte_size() == 4;
}

// static
bool DwarfStackEntry::TreatAsDouble(const BaseType* type) {
  if (!type)
    return false;  // Generic types are unsigned.
  return type->base_type() == BaseType::kBaseTypeFloat && type->byte_size() == 8;
}

bool DwarfStackEntry::IsZero() const {
  if (TreatAsSigned())
    return signed_value_ == 0;
  if (TreatAsUnsigned())
    return unsigned_value_ == 0;
  if (TreatAsFloat())
    return !isnan(float_value_) && float_value_ > -FLT_EPSILON && float_value_ < FLT_EPSILON;
  if (TreatAsDouble())
    return !isnan(double_value_) && double_value_ > -DBL_EPSILON && double_value_ < DBL_EPSILON;

  FX_NOTREACHED();
  return false;
}

bool DwarfStackEntry::SameTypeAs(const DwarfStackEntry& other) const {
  if (is_generic() && other.is_generic())
    return true;

  if (is_generic() || other.is_generic())
    return false;  // One is generic and the other isn't they can't match.

  // Both are declared types, the types and sizes must match.
  return (type_->base_type() == other.type_->base_type()) &&
         (type_->byte_size() == other.type_->byte_size());
}

std::string DwarfStackEntry::GetTypeDescription() const {
  if (is_generic())
    return "generic";
  return BaseType::BaseTypeToString(type_->base_type()) +
         "(size=" + std::to_string(type_->byte_size()) + ")";
}

std::ostream& operator<<(std::ostream& out, const DwarfStackEntry& entry) {
  out << "DwarfStackEntry(type=" << entry.GetTypeDescription() << ", value=";
  if (entry.TreatAsUnsigned()) {
    out << to_string(entry.unsigned_value());
  } else if (entry.TreatAsSigned()) {
    out << to_string(entry.signed_value());
  } else if (entry.TreatAsFloat()) {
    out << std::to_string(entry.float_value());
  } else if (entry.TreatAsDouble()) {
    out << std::to_string(entry.double_value());
  }
  return out << ")";
}

}  // namespace zxdb
