// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_stack_usage.h"

#include <algorithm>
#include <map>
#include <optional>
#include <vector>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/join_callbacks.h"
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

const char kNoFramesError[] = "No frames, try \"pause\" and re-run \"stack-usage\".";

const char kStackUsageShortHelp[] = "stack-usage: Summarize stack usage.";
const char kStackUsageHelp[] =
    R"(stack-usage

  Prints an overview of the stack usage for each thread of a process.

  To compute this table, zxdb locates for each thread the VMO mapping containing
  the stack pointer of the top stack frame (information from the "aspace"
  command) and computes statistics.

Stack types

  A Fuchsia thread uses two stacks: the "safe" stack for return addresses and
  register saving, and the "unsafe" stack for data passed to other functions by
  pointer. This command shows the statistics for each.

  See https://fuchsia.dev/fuchsia-src/concepts/kernel/safestack for more.

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

// Returns the address of the unsafe stack pointer for the given thread.
ErrOr<TargetPointer> UnsafeStackPointerAddress(Thread* thread) {
  // See https://fuchsia.dev/fuchsia-src/concepts/kernel/safestack
  // This hardcodes the constants from <zircon/tls.h> which we do not expect to change. If this
  // is subject to variance in the future, we can include this offset with the architecture
  // information in the Hello message from the debug_agent.
  debug::RegisterID thread_reg = debug::RegisterID::kUnknown;
  int64_t safe_stack_offset = 0;
  switch (thread->session()->arch_info().arch()) {
    case debug::Arch::kUnknown:
      return Err("Unknown architecture.");
    case debug::Arch::kX64:
      thread_reg = debug::RegisterID::kX64_fsbase;
      safe_stack_offset = 0x18;
      break;
    case debug::Arch::kArm64:
      thread_reg = debug::RegisterID::kARMv8_tpidr;
      safe_stack_offset = -0x8;
      break;
  }

  Stack& stack = thread->GetStack();
  if (stack.empty())
    return Err(kNoFramesError);
  const Frame* frame = stack[0];

  // Expect the thread register to be in the general register set which is synchronously available
  // for stopped threads.
  auto* reg_vect = frame->GetRegisterCategorySync(debug::RegisterCategory::kGeneral);
  if (!reg_vect)
    return Err("Registers not available.");

  // Locate the thread register in the register list.
  auto found_reg =
      std::find_if(reg_vect->begin(), reg_vect->end(),
                   [&](const debug::RegisterValue& reg) { return reg.id == thread_reg; });
  if (found_reg == reg_vect->end())
    return Err("Thread register not available.");

  return static_cast<TargetPointer>(found_reg->GetValue()) + safe_stack_offset;
}

// Reads the unsafe stack pointer from the given thread's thread data and passes it to the |cb|.
void GetUnsafeStackPointer(Thread* thread, fit::function<void(ErrOr<TargetPointer>)> cb) {
  auto addr_or = UnsafeStackPointerAddress(thread);
  if (addr_or.has_error())
    return cb(addr_or.err());

  thread->GetProcess()->ReadMemory(addr_or.value(), sizeof(TargetPointer),
                                   [cb = std::move(cb)](const Err& err, MemoryDump dump) {
                                     if (err.has_error())
                                       return cb(err);

                                     // Expect the memory dump to have one valid block of the
                                     // correct size.
                                     if (!dump.AllValid() || dump.blocks().size() != 1 ||
                                         dump.blocks()[0].data.size() != sizeof(TargetPointer))
                                       return cb(Err("Unable to read unsafe stack pointer."));

                                     TargetPointer unsafe_stack_pointer = 0;
                                     memcpy(&unsafe_stack_pointer, dump.blocks()[0].data.data(),
                                            sizeof(TargetPointer));
                                     cb(unsafe_stack_pointer);
                                   });
}

ErrOr<OneStackUsage> GetUsageForStackPointer(const std::vector<debug_ipc::AddressRegion>& maps,
                                             uint64_t stack_pointer, uint64_t page_size) {
  // Get the region covering the stack, which we expect to be a VMO. Assume the top of that is the
  // stack base. It's actually the first address outside the region.
  auto region_or = RegionForAddress(maps, stack_pointer);
  if (!region_or || region_or->vmo_koid == 0)
    return Err("Stack pointer not inside a VMO.");
  uint64_t stack_base = region_or->base + region_or->size;

  if (page_size == 0)
    return Err("Invalid page size for target system.");

  OneStackUsage result;
  result.total = region_or->size;
  result.used = stack_base - stack_pointer;
  result.committed = region_or->committed_pages * page_size;
  uint64_t used_pages = (result.used + page_size - 1) / page_size;
  result.wasted = (region_or->committed_pages - used_pages) * page_size;

  return result;
}

void AppendOneStackUsageColumns(const OneStackUsage& usage, std::vector<OutputBuffer>& row) {
  row.push_back(OutputBuffer(std::to_string(usage.used)));
  row.push_back(OutputBuffer(std::to_string(usage.committed)));
  row.push_back(OutputBuffer(std::to_string(usage.wasted)));
  row.push_back(OutputBuffer(std::to_string(usage.total)));
}

void AppendOneStackUsageError(std::vector<OutputBuffer>& row) {
  row.push_back(OutputBuffer(Syntax::kComment, "?"));
  row.push_back(OutputBuffer(Syntax::kComment, "?"));
  row.push_back(OutputBuffer(Syntax::kComment, "?"));
  row.push_back(OutputBuffer(Syntax::kComment, "?"));
}

using ThreadKoidToStackPointer = std::map<uint64_t, TargetPointer>;

// Implememnts actually computing the stack statistics. The safe stack pointers are passed in as
// a map indexed by thread koid.
void RunStackUsage(Process& process, std::vector<debug_ipc::AddressRegion> map,
                   const ThreadKoidToStackPointer& unsafe_stack_pointers,
                   fxl::RefPtr<CommandContext> cmd_context) {
  ConsoleContext* console_context = cmd_context->GetConsoleContext();
  if (!console_context)
    return;  // Console gone, nothing to do.

  OneStackUsage totals;

  // Our header is two lines. The top line is the "real" ColSpec header. This is the 2nd line.
  std::vector<std::vector<OutputBuffer>> rows;
  auto& header2 = rows.emplace_back();
  header2.push_back(OutputBuffer(Syntax::kHeading, "#"));
  header2.push_back(OutputBuffer("│"));
  header2.push_back(OutputBuffer(Syntax::kHeading, "Current"));
  header2.push_back(OutputBuffer(Syntax::kHeading, "Commit"));
  header2.push_back(OutputBuffer(Syntax::kHeading, "Wasted"));
  header2.push_back(OutputBuffer(Syntax::kHeading, "Mapped"));
  header2.push_back(OutputBuffer("│"));
  header2.push_back(OutputBuffer(Syntax::kHeading, "Current"));
  header2.push_back(OutputBuffer(Syntax::kHeading, "Commit"));
  header2.push_back(OutputBuffer(Syntax::kHeading, "Wasted"));
  header2.push_back(OutputBuffer(Syntax::kHeading, "Mapped"));
  header2.push_back(OutputBuffer("│"));
  header2.push_back(OutputBuffer(Syntax::kHeading, "Name"));

  auto threads = process.GetThreads();
  for (Thread* thread : threads) {
    // The unsafe stack pointer is stored in the map.
    TargetPointer unsafe_stack_pointer = 0;
    if (auto found = unsafe_stack_pointers.find(thread->GetKoid());
        found != unsafe_stack_pointers.end()) {
      unsafe_stack_pointer = found->second;
    }

    auto usage = GetThreadStackUsage(console_context, map, thread, unsafe_stack_pointer);

    auto& row = rows.emplace_back();
    row.push_back(OutputBuffer(Syntax::kSpecial, std::to_string(usage.id)));
    row.push_back(OutputBuffer("│"));

    // Safe stack.
    if (usage.safe_stack.has_error()) {
      AppendOneStackUsageError(row);
    } else {
      const OneStackUsage& safe = usage.safe_stack.value();
      AppendOneStackUsageColumns(safe, row);
      totals += safe;
    }

    // Unsafe stack.
    row.push_back(OutputBuffer("│"));
    if (usage.unsafe_stack.has_error()) {
      AppendOneStackUsageError(row);
    } else {
      const OneStackUsage& unsafe = usage.unsafe_stack.value();
      AppendOneStackUsageColumns(unsafe, row);
      totals += unsafe;
    }

    // Output the error or thread name.
    row.push_back(OutputBuffer("│"));
    if (usage.safe_stack.has_error() || usage.unsafe_stack.has_error()) {
      OutputBuffer err_buf;
      err_buf.Append(Syntax::kError, "Error: ");
      if (usage.safe_stack.has_error()) {
        err_buf.Append(usage.safe_stack.err());
      } else {
        err_buf.Append(usage.unsafe_stack.err());
      }
      row.push_back(std::move(err_buf));
    } else {
      row.push_back(OutputBuffer(usage.name));
    }
  }

  OutputBuffer out;
  out.Append(Syntax::kHeading, "Per-thread stack usage");
  out.Append(Syntax::kComment, " (see \"help stack-usage\" for meanings)\n");

  // The headers here are just for the top line showing the categories. The 2nd line of headers was
  // added as the first row at the top of this function.
  // clang-format off
  FormatTable({ColSpec(Align::kRight, 0, "", 1),
               ColSpec(Align::kRight, 0, "│"),
               ColSpec(Align::kRight, 0, "Safe   "),
               ColSpec(Align::kRight, 0, "", 0),
               ColSpec(Align::kRight, 0, "", 0),
               ColSpec(Align::kRight, 0, "", 0),
               ColSpec(Align::kRight, 0, "│"),
               ColSpec(Align::kRight, 0, "Unsafe "),
               ColSpec(Align::kRight, 0, "", 0),
               ColSpec(Align::kRight, 0, "", 0),
               ColSpec(Align::kRight, 0, "", 0),
               ColSpec(Align::kRight, 0, "│"),
               ColSpec(Align::kLeft, 0, "")},
              rows, &out);
  // clang-format on

  // Show the totals.
  out.Append(Syntax::kHeading, "\nTotals for all threads' safe and unsafe stacks");
  out.Append(Syntax::kHeading, "\n    Current: ");
  out.Append(std::to_string(totals.used));
  out.Append(Syntax::kHeading, "\n     Commit: ");
  out.Append(std::to_string(totals.committed));
  out.Append(Syntax::kHeading, "\n     Wasted: ");
  out.Append(std::to_string(totals.wasted));
  out.Append(Syntax::kHeading, "\n     Mapped: ");
  out.Append(std::to_string(totals.total));
  out.Append("\n");

  cmd_context->Output(out);
}

// Implements the command once all threads are stopped, frames are synced, and we have the address
// space information.
//
// Watch out: something could have been resumed out from under us so be tolerant of errors.
void RunStackUsageOnSyncedFrames(Process& process, std::vector<debug_ipc::AddressRegion> map,
                                 fxl::RefPtr<CommandContext> cmd_context) {
  // The thread callbacks collect their stack pointers in this map.
  auto stack_pointer_map = std::make_shared<ThreadKoidToStackPointer>();

  // Collects the callbacks and dispatches the final result to the RunStackUsage() function.
  auto join = fxl::MakeRefCounted<JoinCallbacks<void>>([weak_process = process.GetWeakPtr(),
                                                        map = std::move(map), stack_pointer_map,
                                                        cmd_context]() {
    if (!weak_process) {
      cmd_context->ReportError(Err("Process exited."));
    } else {
      RunStackUsage(*weak_process, map, *stack_pointer_map, cmd_context);
    }
  });

  // Schedule requesting the unsafe stack pointers.
  for (Thread* thread : process.GetThreads()) {
    fit::callback<void()> join_cb = join->AddCallback();
    GetUnsafeStackPointer(thread, [koid = thread->GetKoid(), stack_pointer_map,
                                   cb = std::move(join_cb)](ErrOr<TargetPointer> sp_or) mutable {
      if (sp_or.ok())
        (*stack_pointer_map)[koid] = sp_or.value();
      cb();  // Tell the JoinCallbacks that this one is done.
    });
  }

  join->Ready();
}

void RunVerbStackUsage(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  Err err = AssertAllStoppedThreadsCommand(cmd_context->GetConsoleContext(), cmd, "stack-usage");
  if (err.has_error())
    return cmd_context->ReportError(err);

  // Success, get the address space.
  Process* process = cmd.target()->GetProcess();
  process->GetAspace(0, [weak_process = process->GetWeakPtr(), cmd_context](
                            const Err& err, std::vector<debug_ipc::AddressRegion> map) {
    if (err.has_error()) {
      cmd_context->ReportError(err);
    } else if (!weak_process) {
      cmd_context->ReportError(Err("Process exited."));
    } else {
      // Success.
      RunStackUsageOnSyncedFrames(*weak_process, std::move(map), cmd_context);
    }
  });
}

}  // namespace

// The unsafe stack pointer can be 0 to indicate there is no unsafe stack.
ThreadStackUsage GetThreadStackUsage(ConsoleContext* console_context,
                                     const std::vector<debug_ipc::AddressRegion>& map,
                                     Thread* thread, TargetPointer unsafe_stack_pointer) {
  // The thread stack starts at the high address and grows to lower addresses.
  ThreadStackUsage result;
  result.id = console_context->IdForThread(thread);
  result.name = thread->GetName();

  const uint64_t page_size = thread->session()->arch_info().page_size();

  // Safe stack comes from the thread pointer.
  const Stack& stack = thread->GetStack();
  if (stack.empty()) {
    result.safe_stack = Err(kNoFramesError);
  } else {
    const Frame* top_frame = stack[0];
    uint64_t stack_pointer = top_frame->GetStackPointer();
    result.safe_stack = GetUsageForStackPointer(map, stack_pointer, page_size);
  }

  if (unsafe_stack_pointer)
    result.unsafe_stack = GetUsageForStackPointer(map, unsafe_stack_pointer, page_size);

  return result;
}

VerbRecord GetStackUsageVerbRecord() {
  VerbRecord stack(&RunVerbStackUsage, {"stack-usage"}, kStackUsageShortHelp, kStackUsageHelp,
                   CommandGroup::kQuery);
  return stack;
}

}  // namespace zxdb
