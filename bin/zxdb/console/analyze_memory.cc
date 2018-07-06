// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/analyze_memory.h"

#include <inttypes.h>

#include <map>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/symbols/process_symbols.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/lib/debug_ipc/records.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Rounds the beginning and size to sizeof(uint64_t) which we assume all
// pointers are on the debugged platform. This may need to be configurable in
// the future.
constexpr uint64_t kAlign = sizeof(uint64_t);

// Aspace entries this size or larger will be ignored for annotation purposes.
// These large regions generally represent the process's available address
// space rather than actually used memory.
constexpr uint64_t kMaxAspaceRegion = 128000000000;  // 128GB

}  // namespace

namespace internal {

MemoryAnalysis::MemoryAnalysis(const AnalyzeMemoryOptions& opts, Callback cb)
    : callback_(std::move(cb)) {
  process_ = opts.process->GetWeakPtr();

  // This doesn't store the Thread because it may go out-of-scope during the
  // asynchronous requests. We'd need a weak pointer but its better avoided.
  begin_address_ = opts.begin_address / kAlign * kAlign;

  uint64_t end = opts.begin_address + opts.bytes_to_read;
  end += kAlign - 1;
  end = end / kAlign * kAlign;
  bytes_to_read_ = static_cast<uint32_t>(end - begin_address_);
}

void MemoryAnalysis::Schedule(const AnalyzeMemoryOptions& opts) {
  // Copies are passed to the callbacks to keep this object in scope until
  // all are complete.
  fxl::RefPtr<MemoryAnalysis> this_ref(this);

  if (opts.thread) {
    // Request registers.
    if (!have_registers_) {
      opts.thread->GetRegisters(
          [this_ref](const Err& err, std::vector<debug_ipc::Register> regs) {
            this_ref->OnRegisters(err, regs);
          });
    }

    // Request stack dump.
    if (!have_frames_) {
      if (opts.thread->HasAllFrames()) {
        OnFrames(opts.thread->GetWeakPtr());
      } else {
        opts.thread->SyncFrames([
          this_ref, weak_thread = opts.thread->GetWeakPtr()
        ]() { this_ref->OnFrames(weak_thread); });
      }
    }
  } else {
    // Mark these as complete so we can continue when everything else is done.
    have_registers_ = true;
    have_frames_ = true;
  }

  // Request memory dump.
  if (!have_memory_) {
    opts.process->ReadMemory(begin_address_, bytes_to_read_,
                             [this_ref](const Err& err, MemoryDump dump) {
                               this_ref->OnMemory(err, std::move(dump));
                             });
  }

  // Request address space dump.
  if (!have_aspace_) {
    opts.process->GetAspace(
        0, [this_ref](const Err& err,
                      std::vector<debug_ipc::AddressRegion> aspace) {
          this_ref->OnAspace(err, std::move(aspace));
        });
  }

  // Test code could have set everything, in which case trigger a run.
  if (HasEverything()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [this_ref]() { this_ref->DoAnalysis(); });
  }
}

void MemoryAnalysis::SetAspace(std::vector<debug_ipc::AddressRegion> aspace) {
  FXL_DCHECK(!have_aspace_);
  have_aspace_ = true;
  aspace_ = std::move(aspace);
}

void MemoryAnalysis::SetFrames(const std::vector<Frame*>& frames) {
  FXL_DCHECK(!have_frames_);
  have_frames_ = true;

  // Note that this skips frame 0. Frame 0 SP will always be the SP register
  // which will be annotated also.
  //
  // Note: if we add more stuff per frame (like return addresses and base
  // pointers), we'll wan to change this so frame 0's relevant stuff is
  // added but not its SP.
  for (int i = 1; i < static_cast<int>(frames.size()); i++) {
    AddAnnotation(frames[i]->GetStackPointer(),
                  fxl::StringPrintf("frame %d SP", i));
  }
}

void MemoryAnalysis::SetMemory(MemoryDump dump) {
  FXL_DCHECK(!have_memory_);
  have_memory_ = true;
  memory_ = std::move(dump);
}

void MemoryAnalysis::SetRegisters(
    const std::vector<debug_ipc::Register>& regs) {
  FXL_DCHECK(!have_registers_);
  have_registers_ = true;
  for (const auto& reg : regs)
    AddAnnotation(reg.value, reg.name);
}

void MemoryAnalysis::DoAnalysis() {
  std::vector<std::vector<OutputBuffer>> rows;
  rows.reserve(bytes_to_read_ / kAlign);
  for (uint64_t offset = 0; offset < bytes_to_read_; offset += kAlign) {
    rows.emplace_back();
    auto& row = rows.back();

    uint64_t address = begin_address_ + offset;

    // Address.
    row.push_back(OutputBuffer::WithContents(
        Syntax::kComment, fxl::StringPrintf("0x%" PRIx64, address)));

    // Data
    uint64_t data_value = 0;
    bool has_data = GetData(address, &data_value);
    if (has_data) {
      row.push_back(OutputBuffer::WithContents(
          fxl::StringPrintf("0x%016" PRIx64, data_value)));
    } else {
      row.push_back(OutputBuffer::WithContents("<invalid memory>"));
    }

    std::string annotation = GetAnnotationsBetween(address, address + kAlign);
    std::string pointed_to;
    if (has_data)
      pointed_to = GetPointedToAnnotation(data_value);

    OutputBuffer comments;
    if (!annotation.empty()) {
      // Mark things pointing into the stack as special since they're important
      // and can get drowned out by the "pointed to" annotations.
      comments.Append(Syntax::kSpecial, std::move(annotation));
      if (!pointed_to.empty())
        comments.Append(". ");  // Separator between sections.
    }
    if (!pointed_to.empty())
      comments.Append(std::move(pointed_to));
    row.push_back(comments);
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Address"),
               ColSpec(Align::kRight, 0, "Data"), ColSpec()},
              rows, &out);
  callback_(Err(), std::move(out), begin_address_ + bytes_to_read_);
}

void MemoryAnalysis::OnAspace(const Err& err,
                              std::vector<debug_ipc::AddressRegion> aspace) {
  if (aborted_)
    return;

  // This function can continue without address space annotations so ignore
  // errors.
  SetAspace(std::move(aspace));

  if (HasEverything())
    DoAnalysis();
}

void MemoryAnalysis::OnRegisters(const Err& err,
                                 const std::vector<debug_ipc::Register>& regs) {
  if (aborted_)
    return;

  // This function can continue without registers (say, if the thread has been
  // resumed by the time the request got executed). So just ignore failures.
  SetRegisters(regs);

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

  // This function can continue even if the thread is gone, it just won't get
  // the frame annotations.
  std::vector<Frame*> frames;
  if (thread)
    frames = thread->GetFrames();

  SetFrames(frames);

  if (HasEverything())
    DoAnalysis();
}

bool MemoryAnalysis::HasEverything() const {
  return have_registers_ && have_memory_ && have_frames_ && have_aspace_;
}

void MemoryAnalysis::IssueError(const Err& err) {
  aborted_ = true;
  callback_(err, OutputBuffer(), 0);

  // Reset so we notice if there's an accidental double-call.
  callback_ = Callback();
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
  // Need to handle invalid memory. The easiest thing is to read a byte at a
  // time. This doesn't handle invalid regions spanning a pointer; that
  // shouldn't happen because valid memory regions should always be aligned
  // more coarsly than the size of a pointer.
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

std::string MemoryAnalysis::GetAnnotationsBetween(uint64_t address_begin,
                                                  uint64_t address_end) const {
  auto lower = annotations_.lower_bound(address_begin);
  auto upper = annotations_.upper_bound(address_end - 1);
  if (lower == upper)
    return std::string();  // No annotations in this range.

  std::string result = ("◁ ");
  for (auto cur = lower; cur != upper; ++cur) {
    if (cur != lower) {
      // Not the first annotation, needs a separator.
      result += "; ";
    }
    if (cur->first != address_begin) {
      // Not at the address but inside of the range. Annotate that carefully.
      result += fxl::StringPrintf("@ 0x%" PRIx64 ": ", cur->first);
    }
    result += cur->second;
  }
  return result;
}

std::string MemoryAnalysis::GetPointedToAnnotation(uint64_t data) const {
  if (!process_)
    return std::string();
  Location loc = process_->GetSymbols()->LocationForAddress(data);
  if (!loc.has_symbols()) {
    // Check if this points into any relevant aspace entries. Want the deepest
    // one smaller than the max size threshold.
    int max_depth = -1;
    size_t found_entry = aspace_.size();  // Indicates not found.
    for (size_t i = 0; i < aspace_.size(); i++) {
      const auto& region = aspace_[i];
      if (region.size < kMaxAspaceRegion && data >= region.base &&
          data < region.base + region.size &&
          max_depth < static_cast<int>(region.depth)) {
        max_depth = static_cast<int>(region.depth);
        found_entry = i;
      }
    }

    if (found_entry == aspace_.size())
      return std::string();  // Not found.
    return fxl::StringPrintf("▷ inside map \"%s\"",
                             aspace_[found_entry].name.c_str());
  }
  // TODO(brettw) this should indicate the byte offset from the beginning of
  // the function, or maybe the file/line number.
  return "▷ inside " + loc.function() + "()";
}

}  // namespace internal

void AnalyzeMemory(const AnalyzeMemoryOptions& opts,
                   std::function<void(const Err& err, OutputBuffer analysis,
                                      uint64_t next_addr)>
                       cb) {
  auto analysis =
      fxl::MakeRefCounted<zxdb::internal::MemoryAnalysis>(opts, std::move(cb));
  analysis->Schedule(opts);
}

}  // namespace zxdb
