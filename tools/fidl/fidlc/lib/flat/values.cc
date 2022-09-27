// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/values.h"

#include <zircon/assert.h>

#include <safemath/checked_math.h>

#include "tools/fidl/fidlc/include/fidl/raw_ast.h"
#include "tools/fidl/fidlc/include/fidl/utils.h"

namespace fidl::flat {

// Explicit instantations.
template struct NumericConstantValue<int8_t>;
template struct NumericConstantValue<int16_t>;
template struct NumericConstantValue<int32_t>;
template struct NumericConstantValue<int64_t>;
template struct NumericConstantValue<uint8_t>;
template struct NumericConstantValue<uint16_t>;
template struct NumericConstantValue<uint32_t>;
template struct NumericConstantValue<uint64_t>;
template struct NumericConstantValue<float>;
template struct NumericConstantValue<double>;

template <typename ValueType>
bool NumericConstantValue<ValueType>::Convert(Kind kind,
                                              std::unique_ptr<ConstantValue>* out_value) const {
  ZX_ASSERT(out_value != nullptr);

  auto checked_value = safemath::CheckedNumeric<ValueType>(value);

  switch (kind) {
    case Kind::kInt8: {
      int8_t casted_value;
      if (!checked_value.template Cast<int8_t>().AssignIfValid(&casted_value)) {
        return false;
      }
      *out_value = std::make_unique<NumericConstantValue<int8_t>>(casted_value);
      return true;
    }
    case Kind::kInt16: {
      int16_t casted_value;
      if (!checked_value.template Cast<int16_t>().AssignIfValid(&casted_value)) {
        return false;
      }
      *out_value = std::make_unique<NumericConstantValue<int16_t>>(casted_value);
      return true;
    }
    case Kind::kInt32: {
      int32_t casted_value;
      if (!checked_value.template Cast<int32_t>().AssignIfValid(&casted_value)) {
        return false;
      }
      *out_value = std::make_unique<NumericConstantValue<int32_t>>(casted_value);
      return true;
    }
    case Kind::kInt64: {
      int64_t casted_value;
      if (!checked_value.template Cast<int64_t>().AssignIfValid(&casted_value)) {
        return false;
      }
      *out_value = std::make_unique<NumericConstantValue<int64_t>>(casted_value);
      return true;
    }
    case Kind::kUint8:
    case Kind::kZxUchar: {
      uint8_t casted_value;
      if (!checked_value.template Cast<uint8_t>().AssignIfValid(&casted_value)) {
        return false;
      }
      *out_value = std::make_unique<NumericConstantValue<uint8_t>>(casted_value);
      return true;
    }
    case Kind::kUint16: {
      uint16_t casted_value;
      if (!checked_value.template Cast<uint16_t>().AssignIfValid(&casted_value)) {
        return false;
      }
      *out_value = std::make_unique<NumericConstantValue<uint16_t>>(casted_value);
      return true;
    }
    case Kind::kUint32: {
      uint32_t casted_value;
      if (!checked_value.template Cast<uint32_t>().AssignIfValid(&casted_value)) {
        return false;
      }
      *out_value = std::make_unique<NumericConstantValue<uint32_t>>(casted_value);
      return true;
    }
    case Kind::kUint64:
    case Kind::kZxUsize:
    case Kind::kZxUintptr: {
      uint64_t casted_value;
      if (!checked_value.template Cast<uint64_t>().AssignIfValid(&casted_value)) {
        return false;
      }
      *out_value = std::make_unique<NumericConstantValue<uint64_t>>(casted_value);
      return true;
    }
    case Kind::kFloat32: {
      float casted_value;
      if (!checked_value.template Cast<float>().AssignIfValid(&casted_value)) {
        return false;
      }
      *out_value = std::make_unique<NumericConstantValue<float>>(casted_value);
      return true;
    }
    case Kind::kFloat64: {
      double casted_value;
      if (!checked_value.template Cast<double>().AssignIfValid(&casted_value)) {
        return false;
      }
      *out_value = std::make_unique<NumericConstantValue<double>>(casted_value);
      return true;
    }
    case Kind::kDocComment:
    case Kind::kString:
    case Kind::kBool:
      return false;
  }
}

bool BoolConstantValue::Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const {
  ZX_ASSERT(out_value != nullptr);
  switch (kind) {
    case Kind::kBool:
      *out_value = std::make_unique<BoolConstantValue>(value);
      return true;
    default:
      return false;
  }
}

bool DocCommentConstantValue::Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const {
  ZX_ASSERT(out_value != nullptr);
  switch (kind) {
    case Kind::kDocComment:
      *out_value = std::make_unique<DocCommentConstantValue>(std::string_view(value));
      return true;
    default:
      return false;
  }
}

std::string DocCommentConstantValue::MakeContents() const {
  if (value.empty()) {
    return "";
  }
  return fidl::utils::strip_doc_comment_slashes(value);
}

bool StringConstantValue::Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const {
  ZX_ASSERT(out_value != nullptr);
  switch (kind) {
    case Kind::kString:
      *out_value = std::make_unique<StringConstantValue>(std::string_view(value));
      return true;
    default:
      return false;
  }
}

std::string StringConstantValue::MakeContents() const {
  if (value.empty()) {
    return "";
  }
  return fidl::utils::strip_string_literal_quotes(value);
}

void Constant::ResolveTo(std::unique_ptr<ConstantValue> value, const Type* type) {
  ZX_ASSERT(value != nullptr);
  ZX_ASSERT_MSG(!IsResolved(), "constants should only be resolved once");
  value_ = std::move(value);
  this->type = type;
}

const ConstantValue& Constant::Value() const {
  if (!IsResolved()) {
    std::cout << span.data() << std::endl;
  }
  ZX_ASSERT_MSG(IsResolved(), "accessing the value of an unresolved Constant");
  return *value_;
}

std::unique_ptr<Constant> Constant::Clone() const {
  auto cloned = CloneImpl();
  cloned->compiled = compiled;
  cloned->type = type;
  cloned->value_ = value_ ? value_->Clone() : nullptr;
  return cloned;
}

LiteralConstant::LiteralConstant(const raw::Literal* literal)
    : Constant(Kind::kLiteral, literal->span()), literal(literal) {}

}  // namespace fidl::flat
