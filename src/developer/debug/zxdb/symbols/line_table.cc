// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/line_table.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <limits>

#include "src/developer/debug/shared/largest_less_or_equal.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace zxdb {

static constexpr uint64_t kMaxAddress = std::numeric_limits<uint64_t>::max();

size_t LineTable::GetNumSequences() const {
  EnsureSequences();
  return sequences_.size();
}

containers::array_view<LineTable::Row> LineTable::GetSequenceAt(size_t index) const {
  // Callers will have to call GetNumSequences() above before querying for a specific one, so we
  // don't have to call EnsureSequence().
  FX_DCHECK(index < sequences_.size());

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
                                                TargetPointer absolute_address,
                                                SkipMode skip_mode) const {
  containers::array_view<Row> seq = GetRowSequenceForAddress(address_context, absolute_address);
  if (seq.empty())
    return FoundRow();

  TargetPointer rel_address = address_context.AbsoluteToRelative(absolute_address);
  // LargestLessOrEqual() will return the first item when it compares equal to an item and that's
  // a sequence of exact matches. That's the behavior we want here.
  auto found = debug_ipc::LargestLessOrEqual(
      seq.begin(), seq.end(), rel_address,
      [](const Row& row, TargetPointer addr) { return row.Address.Address < addr; },
      [](const Row& row, TargetPointer addr) { return row.Address.Address == addr; });

  // The address should not be before the beginning of this sequence (the only end() case for
  // LargestLessOrEqual()). Otherwise GetRowSequenceForAddress() shouldn't have returned it.
  FX_DCHECK(found != seq.end());

  size_t found_index = found - seq.begin();
  if (skip_mode == kSkipCompilerGenerated) {
    // Skip compiler-generated rows. Don't advance to an "end sequence" line because that doesn't
    // represent actual code, just the end of the extent of the sequence.
    while (found_index + 1 < seq.size() && seq[found_index].Line == 0 &&
           !seq[found_index + 1].EndSequence)
      found_index++;
  }

  return FoundRow(seq, found_index);
}

const LineTable::Sequence* LineTable::GetSequenceForRelativeAddress(
    TargetPointer relative_address) const {
  EnsureSequences();
  if (sequences_.empty())
    return nullptr;

  auto found = debug_ipc::LargestLessOrEqual(
      sequences_.begin(), sequences_.end(), relative_address,
      [](const Sequence& seq, TargetPointer addr) { return seq.addresses.begin() < addr; },
      [](const Sequence& seq, TargetPointer addr) { return seq.addresses.begin() == addr; });
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
      // these from the table. As of revision
      // e618ccbf431f6730edb6d1467a127c3a52fd57f7 in Clang, -1 is used to
      // indicate that a function was removed. Versions of Clang earlier than
      // this do not support this behavior.
      auto seq_addr = rows[cur_seq_begin_row].Address.Address;
      if (seq_addr && seq_addr != kMaxAddress)
        sequences_.emplace_back(AddressRange(seq_addr, rows[i].Address.Address), cur_seq_begin_row,
                                i);
      cur_seq_begin_row = kNoSeq;
    }
  }

  std::sort(sequences_.begin(), sequences_.end(), [](const Sequence& a, const Sequence& b) {
    return a.addresses.end() < b.addresses.end();
  });
}

}  // namespace zxdb
