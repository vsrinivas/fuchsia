// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/variant.h"

namespace zxdb {

Variant::Variant(std::optional<uint64_t> discr_value, std::vector<LazySymbol> data_members)
    : Symbol(DwarfTag::kVariant),
      discr_value_(discr_value),
      data_members_(std::move(data_members)) {}

Variant::~Variant() = default;

}  // namespace zxdb
