// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/analyze_memory.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>

#include <map>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_register.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/symbol_utils.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Rounds the beginning and size to sizeof(uint64_t) which we assume all pointers are on the
// debugged platform. This may need to be configurable in the future.
constexpr uint64_t kAlign = sizeof(uint64_t);

// Aspace entries this size or larger will be ignored for annotation purposes. These large regions
// generally represent the process's available address space rather than actually used memory.
constexpr uint64_t kMaxAspaceRegion = 128000000000;  // 128GB

}  // namespace

namespace internal {

MemoryAnalysis::MemoryAnalysis(const AnalyzeMemoryOptions& opts, Callback cb)
    : callback_(std::move(cb)) {
  process_ = opts.process->GetWeakPtr();

  // This doesn't store the Thread because it may go out-of-scope during the asynchronous requests.
  // We'd need a weak pointer but its better avoided.
  begin_address_ = opts.begin_address / kAlign * kAlign;

  uint64_t end = opts.begin_address + opts.bytes_to_read;
  end += kAlign - 1;
  end = end / kAlign * kAlign;
  bytes_to_read_ = static_cast<uint32_t>(end - begin_address_);
}

void MemoryAnalysis::Schedule(const AnalyzeMemoryOptions& opts) {
  // Copies are passed to the callbacks to keep this object in scope until all are complete.
  fxl::RefPtr<MemoryAnalysis> this_ref(this);

  if (opts.thread) {
    // Request stack dump.
    if (!have_frames_) {
      if (opts.thread->GetStack().has_all_frames()) {
        OnFrames(opts.thread->GetWeakPtr());
      } else {
        opts.thread->GetStack().SyncFrames(
            [this_ref, weak_thread = opts.thread->GetWeakPtr()](const Err&) {
              // Can ignore the error, the frames will be re-queried from the thread and we'll check
              // the weak pointer in case its destroyed.
              this_ref->OnFrames(weak_thread);
            });
      }
    }
  } else {
    // Mark these as complete so we can continue when everything else is done.
    have_frames_ = true;
  }

  // Request memory dump.
  if (!have_memory_) {
    opts.process->ReadMemory(
        begin_address_, bytes_to_read_,
        [this_ref](const Err& err, MemoryDump dump) { this_ref->OnMemory(err, std::move(dump)); });
  }

  // Request address space dump.
  if (!have_aspace_) {
    opts.process->GetAspace(
        0, [this_ref](const Err& err, std::vector<debug_ipc::AddressRegion> aspace) {
          this_ref->OnAspace(err, std::move(aspace));
        });
  }

  // Test code could have set everything, in which case trigger a run.
  if (HasEverything()) {
    debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE,
                                                [this_ref]() { this_ref->DoAnalysis(); });
  }
}

void MemoryAnalysis::SetAspace(std::vector<debug_ipc::AddressRegion> aspace) {
  FX_DCHECK(!have_aspace_);
  have_aspace_ = true;
  aspace_ = std::move(aspace);
}

void MemoryAnalysis::SetStack(const Stack& stack) {
  FX_DCHECK(!have_frames_);
  have_frames_ = true;

  for (size_t i = 0; i < stack.size(); i++) {
    // Only add the registers once per inline function call sequence. It makes the most sense for
    // the frames to reference the topmost frame of an inline call sequence so this skips everything
    // with an inline frame immediately above it.
    if (i > 0 && stack[i - 1]->IsInline())
      continue;

    const std::vector<debug_ipc::Register>* regs =
        stack[i]->GetRegisterCategorySync(debug_ipc::RegisterCategory::kGeneral);
    FX_DCHECK(regs);  // Always expect general registers to be available.
    AddRegisters(i, *regs);

    // TODO(brettw) make this work when the frame base is asynchronous.
    if (auto bp = stack[i]->GetBasePointer())
      AddAnnotation(*bp, fxl::StringPrintf("frame %zu base", i));
  }
}

void MemoryAnalysis::SetMemory(MemoryDump dump) {
  FX_DCHECK(!have_memory_);
  have_memory_ = true;
  memory_ = std::move(dump);
}

void MemoryAnalysis::DoAnalysis() {
  std::vector<std::vector<OutputBuffer>> rows;
  rows.reserve(bytes_to_read_ / kAlign);
  for (uint64_t offset = 0; offset < bytes_to_read_; offset += kAlign) {
    rows.emplace_back();
    auto& row = rows.back();

    uint64_t address = begin_address_ + offset;

    // Address.
    row.emplace_back(Syntax::kComment, to_hex_string(address));

    // Data
    uint64_t data_value = 0;
    bool has_data = GetData(address, &data_value);
    if (has_data) {
      row.emplace_back(to_hex_string(data_value, 16));
    } else {
      row.emplace_back("<invalid memory>");
    }

    OutputBuffer annotation = GetAnnotationsBetween(address, address + kAlign);
    OutputBuffer pointed_to;
    if (has_data)
      pointed_to = GetPointedToAnnotation(data_value);

    if (!pointed_to.empty()) {
      if (!annotation.empty())
        annotation.Append(". ");  // Separator between sections.
      annotation.Append(std::move(pointed_to));
    }
    row.push_back(annotation);
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Address"), ColSpec(Align::kRight, 0, "Data"), ColSpec()},
              rows, &out);
  callback_(Err(), std::move(out), begin_address_ + bytes_to_read_);
}

void MemoryAnalysis::OnAspace(const Err& err, std::vector<debug_ipc::AddressRegion> aspace) {
  if (aborted_)
    return;

  // This function can continue without address space annotations so ignore errors.
  SetAspace(std::move(aspace));

  if (HasEverything())
    DoAnalysis();
}

void MemoryAnalysis::OnMemory(const Err& err, MemoryDump dump) {
  if (aborted_)
    return;
  if (err.has_error()) {
    IssueError(err);
    return;
  }

  SetMemory(std::move(dump));

  if (HasEverything())
    DoAnalysis();
}

void MemoryAnalysis::OnFrames(fxl::WeakPtr<Thread> thread) {
  if (aborted_)
    return;

  // This function can continue even if the thread is gone, it just won't get the frame annotations.
  if (thread)
    SetStack(thread->GetStack());
  else
    have_frames_ = true;  // Mark fetching is complete.

  if (HasEverything())
    DoAnalysis();
}

bool MemoryAnalysis::HasEverything() const { return have_memory_ && have_frames_ && have_aspace_; }

void MemoryAnalysis::IssueError(const Err& err) {
  aborted_ = true;
  callback_(err, OutputBuffer(), 0);

  // Reset so we notice if there's an accidental double-call.
  callback_ = Callback();
}

void MemoryAnalysis::AddRegisters(int frame_no, const std::vector<debug_ipc::Register>& regs) {
  // Frames can have saved registers. Sometimes these will be the same as frame 0 (the current CPU
  // state). We want to make them say, e.g. "rax" if the value matches the top frame, but if the
  // current frame's register value is different, we want e.g. "frame 5's rax".
  for (const auto& r : regs) {
    if (r.data.size() > sizeof(uint64_t))
      continue;  // Weird register, don't bother.

    uint64_t value = r.GetValue();
    std::string reg_desc;

    if (frame_no == 0) {
      // Frame 0 always gets added with no frame annotation.
      reg_desc = RegisterIDToString(r.id);
      frame_0_regs_[r.id] = value;
    } else {
      // Later frames get an annotation and only get added if they're different than frame 0.
      // Duplicates for inline frames should have been filtered out by the caller.
      auto found_frame_0 = frame_0_regs_.find(r.id);
      if (found_frame_0 != frame_0_regs_.end() && found_frame_0->second == value)
        continue;  // Matches frame 0, don't add a record.

      reg_desc = fxl::StringPrintf("frame %d %s", frame_no, RegisterIDToString(r.id));
    }

    AddAnnotation(value, reg_desc);
  }
}

void MemoryAnalysis::AddAnnotation(uint64_t address, const std::string& str) {
  auto found = annotations_.find(address);
  if (found == annotations_.end()) {
    annotations_[address] = str;
  } else {
    found->second.append(", ");
    found->second.append(str);
  }
}

bool MemoryAnalysis::GetData(uint64_t address, uint64_t* out_value) const {
  // Need to handle invalid memory. The easiest thing is to read a byte at a time. This doesn't
  // handle invalid regions spanning a pointer; that shouldn't happen because valid memory regions
  // should always be aligned more coarsely than the size of a pointer.
  uint64_t data = 0;
  for (uint64_t i = 0; i < kAlign; i++) {
    uint8_t byte;
    if (!memory_.GetByte(address + i, &byte))
      return false;
    data |= static_cast<uint64_t>(byte) << (i * 8);
  }

  *out_value = data;
  return true;
}

OutputBuffer MemoryAnalysis::GetAnnotationsBetween(uint64_t address_begin,
                                                   uint64_t address_end) const {
  auto lower = annotations_.lower_bound(address_begin);
  auto upper = annotations_.upper_bound(address_end - 1);
  if (lower == upper)
    return OutputBuffer();  // No annotations in this range.

  // Mark "pointing to here" annotations as special since they can get drowned out by all of the
  // other pointer stuff.
  OutputBuffer result(Syntax::kSpecial, "◁ ");
  for (auto cur = lower; cur != upper; ++cur) {
    if (cur != lower) {
      // Not the first annotation, needs a separator.
      result.Append("; ");
    }
    if (cur->first != address_begin) {
      // Not at the address but inside of the range. Annotate that carefully.
      result.Append(Syntax::kSpecial, fxl::StringPrintf("@ 0x%" PRIx64 ": ", cur->first));
    }
    result.Append(Syntax::kSpecial, cur->second);
  }
  return result;
}

OutputBuffer MemoryAnalysis::GetPointedToAnnotation(uint64_t data) const {
  if (!process_)
    return OutputBuffer();
  auto locations = process_->GetSymbols()->ResolveInputLocation(InputLocation(data));
  FX_DCHECK(locations.size() == 1);

  if (!locations[0].symbol()) {
    // Check if this points into any relevant aspace entries. Want the deepest one smaller than the
    // max size threshold.
    int max_depth = -1;
    size_t found_entry = aspace_.size();  // Indicates not found.
    for (size_t i = 0; i < aspace_.size(); i++) {
      const auto& region = aspace_[i];
      if (region.size < kMaxAspaceRegion && data >= region.base &&
          data < region.base + region.size && max_depth < static_cast<int>(region.depth)) {
        max_depth = static_cast<int>(region.depth);
        found_entry = i;
      }
    }

    if (found_entry == aspace_.size())
      return OutputBuffer();  // Not found.
    return fxl::StringPrintf("▷ inside map \"%s\"", aspace_[found_entry].name.c_str());
  }

  FormatLocationOptions opts;
  opts.func.name.show_global_qual = false;
  opts.func.name.elide_templates = true;
  opts.func.name.bold_last = true;
  opts.func.params = FormatFunctionNameOptions::kNoParams;
  opts.always_show_addresses = false;
  opts.show_params = false;
  opts.show_file_line = false;
  opts.show_file_path = false;

  OutputBuffer out("▷ ");
  out.Append(FormatLocation(locations[0], opts));
  return out;
}

}  // namespace internal

void AnalyzeMemory(
    const AnalyzeMemoryOptions& opts,
    fit::callback<void(const Err& err, OutputBuffer analysis, uint64_t next_addr)> cb) {
  auto analysis = fxl::MakeRefCounted<zxdb::internal::MemoryAnalysis>(opts, std::move(cb));
  analysis->Schedule(opts);
}

}  // namespace zxdb
