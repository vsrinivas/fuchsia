// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/substatement.h"

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
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

// Appends all inline functions which start in the given address range to the vector. It's assumed
// that the location address identifies the block we want and the address range doesn't cover other
// blocks (since this is used to look up statement calls, this is a good assumption).
void GetInlineCallsForMemory(const ProcessSymbols* symbols, const Location& loc,
                             const AddressRange& abs_range, std::vector<SubstatementCall>* out) {
  const Function* func = loc.symbol().Get()->AsFunction();
  if (!func)
    return;

  // Move to the deepest code block for the address in question. Don't go into inlines since we're
  // currently going to search for the inline calls in the range.
  const CodeBlock* block = func->GetMostSpecificChild(loc.symbol_context(), loc.address(), false);
  if (!block)
    return;

  AddressRange relative_range = loc.symbol_context().AbsoluteToRelative(abs_range);

  // Limit the code size to prevent symbol errors from DoS-ing us.
  constexpr uint64_t kMaxCodeSize = 16384;
  if (relative_range.size() > kMaxCodeSize)
    relative_range = AddressRange(relative_range.begin(), relative_range.begin() + kMaxCodeSize);

  for (const auto& child : block->inner_blocks()) {
    const Function* call = child.Get()->AsFunction();
    if (!call || !call->is_inline())
      continue;

    if (call->code_ranges().empty())
      continue;  // No code for this call, not sure why.

    TargetPointer relative_call_addr = call->code_ranges()[0].begin();
    if (relative_range.InRange(relative_call_addr)) {
      // This inline starts in the address range, count it.
      SubstatementCall& added = out->emplace_back();
      added.call_addr = loc.symbol_context().RelativeToAbsolute(relative_call_addr);
      added.call_dest = added.call_addr;
      added.inline_call = RefPtrTo(call);
    }
  }
}

}  // namespace

void GetSubstatementCallsForLine(
    Process* process, const Location& loc,
    fit::function<void(const Err&, std::vector<SubstatementCall>)> cb) {
  // LineDetails line_details = process->GetSymbols()->LineDetailsForAddress(address, true);
  LineDetails line_details = process->GetSymbols()->LineDetailsForAddress(loc.address());
  if (!line_details.is_valid()) {
    // No line information, not an error but no information. Don't reenter the caller.
    debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE,
                                                [cb = std::move(cb)]() { cb(Err(), {}); });
    return;
  }

  AddressRange extent = line_details.GetExtent();
  process->ReadMemory(
      extent.begin(), extent.size(),
      [arch_info = process->session()->arch_info(),
       weak_symbols = process->GetSymbols()->GetWeakPtr(), loc, extent,
       cb = std::move(cb)](const Err& in_err, MemoryDump dump) {
        if (in_err.has_error())
          return cb(in_err, {});
        if (!weak_symbols)
          return cb(Err("Process destroyed."), {});
        cb(Err(), GetSubstatementCallsForMemory(arch_info, weak_symbols.get(), loc, extent, dump));
      });
}

std::vector<SubstatementCall> GetSubstatementCallsForMemory(const ArchInfo* arch_info,
                                                            const ProcessSymbols* symbols,
                                                            const Location& loc,
                                                            const AddressRange& range,
                                                            const MemoryDump& mem) {
  Disassembler disassembler;
  if (disassembler.Init(arch_info).has_error())
    return {};

  Disassembler::Options options;

  std::vector<Disassembler::Row> rows;
  disassembler.DisassembleDump(mem, mem.address(), options, 0, &rows);

  std::vector<SubstatementCall> result;
  for (const auto& row : rows) {
    if (row.call_dest) {
      auto& call = result.emplace_back();
      call.call_addr = row.address;
      call.call_dest = *row.call_dest;
    }
  }

  // Merge in the inlines, the result should be sorted by call address.
  GetInlineCallsForMemory(symbols, loc, range, &result);

  std::sort(result.begin(), result.end(),
            [](const SubstatementCall& left, const SubstatementCall& right) {
              return left.call_addr < right.call_addr;
            });
  return result;
}

}  // namespace zxdb
