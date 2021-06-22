// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/variable_location.h"

#include <limits>

#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

bool VariableLocation::Entry::InRange(const SymbolContext& symbol_context, uint64_t ip) const {
  return symbol_context.RelativeToAbsolute(range).InRange(ip);
}

VariableLocation::VariableLocation() = default;

VariableLocation::VariableLocation(DwarfExpr expr) : default_expr_(std::move(expr)) {}

VariableLocation::VariableLocation(std::vector<Entry> locations,
                                   std::optional<DwarfExpr> default_expr)
    : locations_(std::move(locations)), default_expr_(std::move(default_expr)) {}

VariableLocation::~VariableLocation() = default;

const DwarfExpr* VariableLocation::ExprForIP(const SymbolContext& symbol_context,
                                             uint64_t ip) const {
  for (const auto& entry : locations_) {
    if (entry.InRange(symbol_context, ip))
      return &entry.expression;
  }

  // Fall back to the default expression if nothing else matches.
  if (default_expr_)
    return &*default_expr_;

  return nullptr;
}

}  // namespace zxdb
