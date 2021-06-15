// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/array_type.h"

#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

ArrayType::ArrayType(fxl::RefPtr<Type> value_type, std::optional<size_t> num_elts)
    : Type(DwarfTag::kArrayType), value_type_(std::move(value_type)), num_elts_(num_elts) {
  if (num_elts_)
    set_byte_size(*num_elts_ * value_type_->byte_size());
}

ArrayType::~ArrayType() = default;

const ArrayType* ArrayType::AsArrayType() const { return this; }

std::string ArrayType::ComputeFullName() const {
  // Same as the nested case but with no "outer" string.
  return ComputeFullNameOfNestedArray(std::string());
}

std::string ArrayType::ComputeFullNameOfNestedArray(const std::string& outer_dims) const {
  std::string elt_count = num_elts_ ? fxl::StringPrintf("[%zu]", *num_elts_) : "[]";
  if (const ArrayType* inner_array = value_type_->As<ArrayType>()) {
    // Special-case nested arrays.
    return inner_array->ComputeFullNameOfNestedArray(outer_dims + elt_count);
  }
  return value_type_->GetFullName() + outer_dims + elt_count;
}

}  // namespace zxdb
