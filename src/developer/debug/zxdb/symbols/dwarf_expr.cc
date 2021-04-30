// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_expr.h"

#include "src/developer/debug/zxdb/symbols/compile_unit.h"

namespace zxdb {

std::optional<uint64_t> DwarfExpr::GetAddrBase() const {
  if (!source_)
    return std::nullopt;

  fxl::RefPtr<Symbol> symbol = source_.Get();
  if (!symbol)
    return std::nullopt;

  auto compile_unit = symbol->GetCompileUnit();
  if (!compile_unit)
    return std::nullopt;

  return compile_unit->addr_base();
}

}  // namespace zxdb
