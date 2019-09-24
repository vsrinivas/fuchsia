// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/line_table.h"

#include <algorithm>

#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

std::optional<size_t> LineTable::GetFirstRowIndexForAddress(const SymbolContext& address_context,
                                                            uint64_t absolute_address) const {
  const auto& rows = GetRows();
  if (rows.empty())
    return std::nullopt;

  uint64_t rel_address = address_context.AbsoluteToRelative(absolute_address);

  auto found = std::lower_bound(rows.begin(), rows.end(), rel_address,
                                [](const Row& row, uint64_t addr) { return row.Address < addr; });
  if (found == rows.end())
    return rows.size() - 1;  // Last row covers addresses past the end.

  size_t found_index = found - rows.begin();
  if (found_index == 0 && rows[found_index].Address > rel_address)
    return std::nullopt;  // Before the beginning of the table.

  // If we get here, we found either the exact match or the row right past the one covering it.
  if (rows[found_index].Address > rel_address)
    found_index--;

  // Skip duplicates if there are any. We want the first entry for this address range.
  while (found_index > 0 && rows[found_index].Address == rows[found_index - 1].Address)
    found_index--;

  return found_index;
}

}  // namespace zxdb
