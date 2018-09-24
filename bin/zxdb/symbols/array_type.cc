// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/array_type.h"

#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

ArrayType::ArrayType(LazySymbol value_type, size_t num_elts)
    : Type(Symbol::kTagArrayType),
      value_type_(value_type),
      num_elts_(num_elts) {
  set_byte_size(sizeof(uint64_t));  // Contents is a pointer.
}

ArrayType::~ArrayType() = default;

const ArrayType* ArrayType::AsArrayType() const { return this; }

std::string ArrayType::ComputeFullName() const {
  return value_type_.Get()->GetFullName() +
         fxl::StringPrintf("[%zu]", num_elts_);
}

}  // namespace zxdb
