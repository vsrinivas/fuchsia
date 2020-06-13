// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/substatement.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/arch_info.h"
#include "src/developer/debug/zxdb/client/disassembler.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/symbols/line_details.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"

namespace zxdb {

namespace {

// Appends all inline functions which start in the given location. The Location should identify both
// the address and the block the address is contained in. The result will be sorted by call address.
std::vector<SubstatementCall> GetInlineCallsForLocation(const ProcessSymbols* symbols,
                                                        const Location& loc) {
  std::vector<SubstatementCall> result;

  const Function* func = loc.symbol().Get()->AsFunction();
  if (!func)
    return result;

  // Move to the deepest code block for the address in question. Don't go into inlines since we're
  // currently going to search for the inline calls in the range.
  const CodeBlock* block = func->GetMostSpecificChild(loc.symbol_context(), loc.address(), false);
  if (!block)
    return result;

  TargetPointer relative_address = loc.symbol_context().AbsoluteToRelative(loc.address());

  // Check for inlines that are children of the current block for ones that start at the current
  // line.
  for (const auto& child : block->inner_blocks()) {
    const Function* call = child.Get()->AsFunction();
    if (!call || !call->is_inline())
      continue;

    if (call->code_ranges().empty())
      continue;  // No code for this call, not sure why.

    // To count as a call from the current line, the call must start after the current address
    // (the user could have stepped half through a bunch of inlines and we don't want to show the
    // ones already passed), and the call line must match.
    TargetPointer relative_call_addr = call->code_ranges()[0].begin();

    if (relative_call_addr >= relative_address && call->call_line() == loc.file_line()) {
      // This inline starts in the address range, count it.
      SubstatementCall& added = result.emplace_back();
      added.call_addr = loc.symbol_context().RelativeToAbsolute(relative_call_addr);
      added.call_dest = added.call_addr;
      added.inline_call = RefPtrTo(call);
    }
  }

  std::sort(result.begin(), result.end());
  return result;
}

// Checks all addresses in the given address range and adds the ranges that map to the given
// file_line to the output. This will also check for ranges that begin in the range but end outside
// of it.
//
// If expand_after_line_0 is set, if a query returns "line 0" from the line table (compiler
// generated code), it will consider the code immediately following it, even if that code is outside
// of the input range. This is used when the range being queried is open-ended and you just want
// all contiguous matching lines starting at a given address without being tripped up by a "line 0"
// entry at the beginning.
//
// This just does many individual queries. This could be done faster using the line table directly
// since then we can go through it linearly for the range we care about. But that approach makes the
// querying more complex and so far this has not shown to be too slow.
void AppendAddressRangesForLineInRange(Process* process, const FileLine& file_line,
                                       const AddressRange& range, bool expand_after_line_0,
                                       AddressRanges::RangeVector* out) {
  // Sanity threshold to avoid doing too many queries if the symbols are corrupt or very different
  // than we expect.
  constexpr uint64_t kMaxRangeSize = 1024;

  bool allow_next_query_out_of_range = false;  // Used to implement expand_after_line_0. See above.

  TargetPointer cur = range.begin();
  while (allow_next_query_out_of_range ||
         (cur < range.end() && cur - range.begin() < kMaxRangeSize)) {
    LineDetails line_details = process->GetSymbols()->LineDetailsForAddress(cur);
    if (!line_details.is_valid())
      return;

    AddressRange extent = line_details.GetExtent();
    if (extent.empty())
      return;

    if (line_details.file_line() == file_line)
      out->push_back(extent);

    allow_next_query_out_of_range = line_details.file_line().line() == 0 && expand_after_line_0;
    cur = extent.end();
  }
}

}  // namespace

AddressRange GetAddressRangeForLineSubstatements(Process* process, const Location& loc);

void GetSubstatementCallsForLine(
    Process* process, const Location& loc,
    fit::function<void(const Err&, std::vector<SubstatementCall>)> cb) {
  std::vector<SubstatementCall> inlines = GetInlineCallsForLocation(process->GetSymbols(), loc);

  // Each inline can have multiple non-contiguous ranges, possibly interleaved with other inline
  // calls. We need to consider the code not covered by any inline, so extract all inline ranges
  // into one structure. The inline ranges should not overlap since each of these inlines is at the
  // same lexicial scope.
  AddressRanges::RangeVector inline_range_vector;
  for (const auto& inline_call : inlines) {
    // These code ranges will be module-relative addresses.
    const auto& cur_ranges = inline_call.inline_call->code_ranges();
    inline_range_vector.insert(inline_range_vector.end(), cur_ranges.begin(), cur_ranges.end());
  }
  // This representation of all the inline ranges is sorted absolute addresses.
  AddressRanges inline_ranges = loc.symbol_context().RelativeToAbsolute(
      AddressRanges(AddressRanges::kNonCanonical, std::move(inline_range_vector)));

  // The code ranges we care about are all the bits between the inline functions we just identified
  // that map the current file/line.
  AddressRanges::RangeVector line_code_ranges;

  // Check the range in between each inline. Count starting from the current address.
  TargetPointer prev_end = loc.address();
  for (size_t i = 0; i < inline_ranges.size(); i++) {
    AddressRange cur_range(prev_end, inline_ranges[i].begin());
    if (!cur_range.empty()) {
      AppendAddressRangesForLineInRange(process, loc.file_line(), cur_range, false,
                                        &line_code_ranges);
    }
    prev_end = inline_ranges[i].end();
  }

  // The address immediately following the last inline call also counts as a place to query since
  // the last inline could be followed by a function call on the same line. If there are no inlines,
  // this location will just be the code range we're querying. Since the Append... funciton counts
  // any ranges beginning in the range we give it, we only need to supply a 1-byte range.
  TargetPointer end_inline_address =
      inline_ranges.empty() ? loc.address() : inline_ranges.back().end();
  AppendAddressRangesForLineInRange(process, loc.file_line(),
                                    AddressRange(end_inline_address, end_inline_address + 1), true,
                                    &line_code_ranges);

  // Make all of the matching ranges in a canonical form.
  AddressRanges line_code(AddressRanges::kNonCanonical, std::move(line_code_ranges));

  if (line_code.empty()) {
    // No code for this line to disassemble. All we have are the inlines (if any).
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb), inlines = std::move(inlines)]() mutable {
          cb(Err(), std::move(inlines));
        });
    return;
  }

  AddressRange extent = line_code.GetExtent();
  process->ReadMemory(
      extent.begin(), extent.size(),
      [arch_info = process->session()->arch_info(),
       weak_symbols = process->GetSymbols()->GetWeakPtr(), loc, line_code = std::move(line_code),
       inlines = std::move(inlines), cb = std::move(cb)](const Err& in_err, MemoryDump dump) {
        if (in_err.has_error())
          return cb(in_err, {});
        if (!weak_symbols)
          return cb(Err("Process destroyed."), {});

        std::vector<SubstatementCall> result =
            GetSubstatementCallsForMemory(arch_info, weak_symbols.get(), loc, line_code, dump);

        // Merge in inline calls.
        result.insert(result.end(), inlines.begin(), inlines.end());
        std::sort(result.begin(), result.end());

        cb(Err(), std::move(result));
      });
}

std::vector<SubstatementCall> GetSubstatementCallsForMemory(const ArchInfo* arch_info,
                                                            const ProcessSymbols* symbols,
                                                            const Location& loc,
                                                            const AddressRanges& ranges,
                                                            const MemoryDump& mem) {
  Disassembler disassembler;
  if (disassembler.Init(arch_info).has_error())
    return {};

  Disassembler::Options options;

  std::vector<Disassembler::Row> rows;
  disassembler.DisassembleDump(mem, mem.address(), options, 0, &rows);

  std::vector<SubstatementCall> result;
  for (const auto& row : rows) {
    if (row.call_dest && ranges.InRange(row.address)) {
      auto& call = result.emplace_back();
      call.call_addr = row.address;
      call.call_dest = *row.call_dest;
    }
  }

  return result;
}

}  // namespace zxdb
