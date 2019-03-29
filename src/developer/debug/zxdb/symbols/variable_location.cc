// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/variable_location.h"

#include <limits>

#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

bool VariableLocation::Entry::InRange(const SymbolContext& symbol_context,
                                      uint64_t ip) const {
  if (begin == 0 && end == 0)
    return true;
  return ip >= symbol_context.RelativeToAbsolute(begin) &&
         ip < symbol_context.RelativeToAbsolute(end);
}

VariableLocation::VariableLocation() = default;

VariableLocation::VariableLocation(const uint8_t* data, size_t size) {
  locations_.emplace_back();
  Entry& entry = locations_.back();

  entry.begin = 0;
  entry.end = 0;
  entry.expression.assign(&data[0], &data[size]);
}

VariableLocation::VariableLocation(std::vector<Entry> locations)
    : locations_(std::move(locations)) {}

VariableLocation::~VariableLocation() = default;

const VariableLocation::Entry* VariableLocation::EntryForIP(
    const SymbolContext& symbol_context, uint64_t ip) const {
  for (const auto& entry : locations_) {
    if (entry.InRange(symbol_context, ip))
      return &entry;
  }
  return nullptr;
}

}  // namespace zxdb
