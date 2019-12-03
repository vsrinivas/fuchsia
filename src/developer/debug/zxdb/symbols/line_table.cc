// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/line_table.h"

#include <algorithm>
#include <limits>

#include "src/developer/debug/zxdb/common/largest_less_or_equal.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

size_t LineTable::GetNumSequences() const {
  EnsureSequences();
  return sequences_.size();
}

containers::array_view<LineTable::Row> LineTable::GetSequenceAt(size_t index) const {
  // Callers will have to call GetNumSequences() above before querying for a specific one, so we
  // don't have to call EnsureSequence().
  FXL_DCHECK(index < sequences_.size());

  containers::array_view<Row> rows = GetRows();
  size_t row_begin = sequences_[index].row_begin;
  return rows.subview(row_begin, sequences_[index].row_end - row_begin);
}

containers::array_view<LineTable::Row> LineTable::GetRowSequenceForAddress(
    const SymbolContext& address_context, TargetPointer absolute_address) const {
  TargetPointer rel_address = address_context.AbsoluteToRelative(absolute_address);

  const Sequence* sequence = GetSequenceForRelativeAddress(rel_address);
  if (!sequence)
    return containers::array_view<Row>();

  containers::array_view<Row> rows = GetRows();
  // Include the last row (marked with EndSequence) in the result.
  return rows.subview(sequence->row_begin, sequence->row_end - sequence->row_begin + 1);
}

LineTable::FoundRow LineTable::GetRowForAddress(const SymbolContext& address_context,
                                                TargetPointer absolute_address) const {
  containers::array_view<Row> seq = GetRowSequenceForAddress(address_context, absolute_address);
  if (seq.empty())
    return FoundRow();

  TargetPointer rel_address = address_context.AbsoluteToRelative(absolute_address);
  auto found = LargestLessOrEqual(
      seq.begin(), seq.end(), rel_address,
      [](const Row& row, TargetPointer addr) { return row.Address < addr; },
      [](const Row& row, TargetPointer addr) { return row.Address == addr; });

  // The address should not be before the beginning of this sequence (the only end() case for
  // LargestLessOrEqual()). Otherwise GetRowSequenceForAddress() shouldn't have returned it.
  FXL_DCHECK(found != seq.end());

  // Skip duplicates if there are any. LargestLessOrEqual() may have returned the last one of a
  // series of duplicates preceeding the address in question. We want the first entry for this
  // address range.
  size_t found_index = found - seq.begin();
  while (found_index > 0 && seq[found_index].Address == seq[found_index - 1].Address)
    found_index--;

  return FoundRow(seq, found_index);
}

const LineTable::Sequence* LineTable::GetSequenceForRelativeAddress(
    TargetPointer relative_address) const {
  EnsureSequences();
  if (sequences_.empty())
    return nullptr;

  auto found = std::lower_bound(
      sequences_.begin(), sequences_.end(), relative_address,
      [](const Sequence& seq, TargetPointer addr) { return seq.addresses.end() < addr; });
  if (found == sequences_.end() || !found->addresses.InRange(relative_address))
    return nullptr;  // Not in any range.

  return &*found;
}

void LineTable::EnsureSequences() const {
  if (!sequences_.empty())
    return;
  const auto& rows = GetRows();
  if (rows.empty())
    return;

  constexpr size_t kNoSeq = std::numeric_limits<size_t>::max();

  // Beginning row index of current sequence, or kNoSeq if there is no current sequence.
  size_t cur_seq_begin_row = kNoSeq;

  for (size_t i = 0; i < rows.size(); i++) {
    if (cur_seq_begin_row == kNoSeq)
      cur_seq_begin_row = i;

    if (rows[i].EndSequence) {
      // When the linker strips dead code it will mark the sequence as starting at address 0. Strip
      // these from the table.
      auto seq_addr = rows[cur_seq_begin_row].Address;
      if (seq_addr)
        sequences_.emplace_back(AddressRange(seq_addr, rows[i].Address), cur_seq_begin_row, i);
      cur_seq_begin_row = kNoSeq;
    }
  }

  std::sort(sequences_.begin(), sequences_.end(), [](const Sequence& a, const Sequence& b) {
    return a.addresses.end() < b.addresses.end();
  });
}

}  // namespace zxdb
