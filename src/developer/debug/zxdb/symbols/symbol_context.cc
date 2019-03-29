// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

AddressRange SymbolContext::RelativeToAbsolute(
    const AddressRange& relative) const {
  return AddressRange(RelativeToAbsolute(relative.begin()),
                      RelativeToAbsolute(relative.end()));
}

AddressRange SymbolContext::AbsoluteToRelative(
    const AddressRange& absolute) const {
  return AddressRange(AbsoluteToRelative(absolute.begin()),
                      AbsoluteToRelative(absolute.end()));
}

AddressRanges SymbolContext::RelativeToAbsolute(
    const AddressRanges& relative) const {
  AddressRanges::RangeVector result;
  result.reserve(relative.size());

  for (const auto& range : relative)
    result.push_back(RelativeToAbsolute(range));

  return AddressRanges(AddressRanges::kCanonical, std::move(result));
}

AddressRanges SymbolContext::AbsoluteToRelative(
    const AddressRanges& absolute) const {
  AddressRanges::RangeVector result;
  result.reserve(absolute.size());

  for (const auto& range : absolute)
    result.push_back(AbsoluteToRelative(range));

  return AddressRanges(AddressRanges::kCanonical, std::move(result));
}

}  // namespace zxdb
