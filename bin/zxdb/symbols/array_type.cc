// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/array_type.h"

#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

ArrayType::ArrayType(fxl::RefPtr<Type> value_type, size_t num_elts)
    : Type(Symbol::kTagArrayType),
      value_type_(std::move(value_type)),
      num_elts_(num_elts) {
  set_byte_size(num_elts * value_type_->byte_size());
}

ArrayType::~ArrayType() = default;

const ArrayType* ArrayType::AsArrayType() const { return this; }

std::string ArrayType::ComputeFullName() const {
  return value_type_->GetFullName() + fxl::StringPrintf("[%zu]", num_elts_);
}

}  // namespace zxdb
