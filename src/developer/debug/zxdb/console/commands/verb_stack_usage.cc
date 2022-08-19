// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_stack_usage.h"

#include <optional>
#include <vector>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/analyze_memory.h"
#include "src/developer/debug/zxdb/console/async_output_buffer.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/commands/verb_mem_analyze.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kStackUsageShortHelp[] = "stack-usage: Summarize stack usage.";
const char kStackUsageHelp[] =
    R"(stack-usage

  Prints an overview of the stack usage for each thread of a process.

  To compute this table, zxdb locates for each thread the VMO mapping containing
  the stack pointer of the top stack frame (information from the "aspace"
  command) and computes statistics.

Columns

  Current
      The number of bytes between the base and the current top of the stack.

  Committed
      The number of bytes in committed pages in the mapped region of the stack.
      These are pages that have private data and represents the high-water mark
      of the stack, rounded up to the nearest page size. This is the amount of
      physical memory used by the stack (non-committed pages are 0-filled and
      take no space).

  Wasted
      The number of bytes in whole pages between the top of the stack and the
      committed size of the stack. This is unused stack data that nevertheless
      occupies physical memory.

  Mmap size
      Size in bytes of the area memory-mapped for the stack.

Examples

  stack-usage
      Shows an overview of stack usage for all threads.
)";

// The maps should be sorted but because it's a tree structure it's a little harder to deal with.
// None of these structures are very large so brute-force is simplest. Returns nullopt if there were
// no matches.
std::optional<debug_ipc::AddressRegion> RegionForAddress(
    const std::vector<debug_ipc::AddressRegion>& maps, uint64_t address) {
  const debug_ipc::AddressRegion* best_match = nullptr;
  for (const debug_ipc::AddressRegion& map : maps) {
    if (address < map.base || address >= map.base + map.size)
      continue;  // Not in range.
    if (!best_match) {
      // Found first match (normally this will always trigger on the first item in the vector
      // because that will be the root VMAR which will cover the whole address space).
      best_match = &map;
    } else if (map.size < best_match->size) {
      // Found a better match (defined by smaller size i.e. more specific).
      best_match = &map;
    }
  }
  if (best_match)
    return *best_match;
  return std::nullopt;
}

// Implements the command once all threads are stopped, frames are synced, and we have the address
// space information.
//
// Watch out: something could have been resumed out from under us so be tolerant of errors.
void RunStackUsageOnSyncedFrames(Process& process, std::vector<debug_ipc::AddressRegion> map) {
  ConsoleContext* console_context = &Console::get()->context();

  ThreadStackUsage totals;
  bool has_error = false;

  std::vector<std::vector<OutputBuffer>> rows;
  auto threads = process.GetThreads();
  for (Thread* thread : threads) {
    auto usage = GetThreadStackUsage(console_context, map, thread);

    auto& row = rows.emplace_back();
    if (usage.err.has_error()) {
      has_error = true;

      row.push_back(OutputBuffer(Syntax::kSpecial, std::to_string(usage.id)));
      row.push_back(OutputBuffer(Syntax::kComment, "?"));
      row.push_back(OutputBuffer(Syntax::kComment, "?"));
      row.push_back(OutputBuffer(Syntax::kComment, "?"));
      row.push_back(OutputBuffer(Syntax::kComment, "?"));

      // Output the error instead of the name.
      OutputBuffer err_buf;
      err_buf.Append(usage.err);
      row.push_back(std::move(err_buf));
    } else {
      row.push_back(OutputBuffer(Syntax::kSpecial, std::to_string(usage.id)));
      row.push_back(OutputBuffer(std::to_string(usage.used)));
      row.push_back(OutputBuffer(std::to_string(usage.committed)));
      row.push_back(OutputBuffer(std::to_string(usage.wasted)));
      row.push_back(OutputBuffer(std::to_string(usage.total)));
      row.push_back(OutputBuffer(usage.name));

      totals.used += usage.used;
      totals.committed += usage.committed;
      totals.wasted += usage.wasted;
      totals.total += usage.total;
    }
  }

  if (!has_error && threads.size() > 1) {
    // Show the totals, start with an empty row as separator.
    rows.emplace_back();

    // Totals.
    auto& row = rows.emplace_back();
    row.push_back(OutputBuffer(Syntax::kHeading, "Totals"));
    row.push_back(OutputBuffer(std::to_string(totals.used)));
    row.push_back(OutputBuffer(std::to_string(totals.committed)));
    row.push_back(OutputBuffer(std::to_string(totals.wasted)));
    row.push_back(OutputBuffer(std::to_string(totals.total)));
    row.emplace_back();  // Empty name.
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Thread #", 1), ColSpec(Align::kRight, 0, "Current", 1),
               ColSpec(Align::kRight, 0, "Committed", 1), ColSpec(Align::kRight, 0, "Wasted", 1),
               ColSpec(Align::kRight, 0, "Mmap size", 1), ColSpec(Align::kLeft, 0, "Name", 1)},
              rows, &out);

  Console::get()->Output(out);
}

Err RunVerbStackUsage(ConsoleContext* context, const Command& cmd) {
  Err err = AssertAllStoppedThreadsCommand(context, cmd, "stack-usage");
  if (err.has_error())
    return err;

  // Success, get the address space.
  Process* process = cmd.target()->GetProcess();
  process->GetAspace(0, [weak_process = process->GetWeakPtr()](
                            const Err& err, std::vector<debug_ipc::AddressRegion> map) {
    if (err.has_error()) {
      Console::get()->Output(err);
    } else if (!weak_process) {
      Console::get()->Output(Err("Process exited."));
    } else {
      // Success, ready to run.
      RunStackUsageOnSyncedFrames(*weak_process, std::move(map));
    }
  });

  return Err();
}

}  // namespace

ThreadStackUsage GetThreadStackUsage(ConsoleContext* console_context,
                                     const std::vector<debug_ipc::AddressRegion>& map,
                                     Thread* thread) {
  // The thread stack starts at the high address and grows to lower addresses.
  ThreadStackUsage result;
  result.id = console_context->IdForThread(thread);
  result.name = thread->GetName();

  const Stack& stack = thread->GetStack();
  if (stack.empty()) {
    result.err = Err("No frames, try \"pause\" and re-run \"stack-usage\".");
    return result;
  }

  const Frame* top_frame = stack[0];
  uint64_t stack_pointer = top_frame->GetStackPointer();

  // Get the region covering the stack, which we expect to be a VMO. Assume the top of that is the
  // stack base. It's actually the first address outside the region.
  auto region_or = RegionForAddress(map, stack_pointer);
  if (!region_or || region_or->vmo_koid == 0) {
    result.err = Err("Stack pointer not inside a VMO.");
    return result;
  }
  uint64_t stack_base = region_or->base + region_or->size;

  const uint64_t kPageSize = thread->session()->arch_info().page_size();
  if (kPageSize == 0) {
    result.err = Err("Invalid page size for target system.");
    return result;
  }

  result.total = region_or->size;
  result.used = stack_base - stack_pointer;
  result.committed = region_or->committed_pages * kPageSize;
  uint64_t used_pages = (result.used + kPageSize - 1) / kPageSize;
  result.wasted = (region_or->committed_pages - used_pages) * kPageSize;

  return result;
}

VerbRecord GetStackUsageVerbRecord() {
  VerbRecord stack(&RunVerbStackUsage, {"stack-usage"}, kStackUsageShortHelp, kStackUsageHelp,
                   CommandGroup::kQuery);
  return stack;
}

}  // namespace zxdb
