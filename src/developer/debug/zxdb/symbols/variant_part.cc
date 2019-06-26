// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/variant_part.h"

#include "src/developer/debug/zxdb/symbols/variant.h"

namespace zxdb {

VariantPart::VariantPart(const LazySymbol& discriminant, std::vector<LazySymbol> variants)
    : Symbol(DwarfTag::kVariantPart), discriminant_(discriminant), variants_(std::move(variants)) {}

VariantPart::~VariantPart() = default;

}  // namespace zxdb
