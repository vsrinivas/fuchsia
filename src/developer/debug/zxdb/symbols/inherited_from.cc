// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/inherited_from.h"

namespace zxdb {

InheritedFrom::InheritedFrom(LazySymbol from, uint64_t offset)
    : kind_(kConstant), from_(std::move(from)), offset_(offset) {}

InheritedFrom::InheritedFrom(LazySymbol from, std::vector<uint8_t> expr)
    : kind_(kExpression), from_(std::move(from)), location_expression_(std::move(expr)) {}

InheritedFrom::~InheritedFrom() = default;

const InheritedFrom* InheritedFrom::AsInheritedFrom() const { return this; }

}  // namespace zxdb
