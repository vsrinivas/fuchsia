// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/lib/debug_ipc/records.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Err;
class Frame;
class OutputBuffer;
class Process;
class Thread;

struct AnalyzeMemoryOptions {
  // Required.
  Process* process = nullptr;

  // Optional. If provided, the current thread registers and stack frames will
  // be queried and the dump will be annotated with matches if they're
  // available.
  Thread* thread = nullptr;

  // The address to begin dumping.
  uint64_t begin_address = 0;

  // Number of bytes following begin_address to analyze.
  uint32_t bytes_to_read = 0;
};

// Runs a stack analysis on the given thread. When the analysis is complete,
// the callback will be issued with the output and the address immediately
// following the last one analyzed (this is so the caller knows the aligned
// address to continue at if desired).
//
// On error, the Err will be set, the output buffer will be empty, and
// next_addr will be 0.
void AnalyzeMemory(const AnalyzeMemoryOptions& opts,
                   std::function<void(const Err& err, OutputBuffer analysis,
                                      uint64_t next_addr)>
                       cb);

namespace internal {

// Implementation of the memory analysis. Consumers should use AnalyzeMemory
// above, this is in the header so it can be unit tested more easily.
//
// This class is refcounted and manages its own lifetime across various
// asynchronous callbacks to issue the final complete callback.
class MemoryAnalysis : public fxl::RefCountedThreadSafe<MemoryAnalysis> {
 public:
  using Callback = std::function<void(const Err& err, OutputBuffer analysis,
                                      uint64_t next_addr)>;

  MemoryAnalysis(const AnalyzeMemoryOptions& opts, Callback cb);
  ~MemoryAnalysis() = default;

  // Opts is passed again so we don't have to save it in the constructor, which
  // is unsafe (the process and thread pointers aren't weak and may disappear).
  void Schedule(const AnalyzeMemoryOptions& opts);

  // Tests can call these functions to manually provide the data that would
  // normally be provided via IPC call. To use, call before "Schedule".
  void SetAspace(std::vector<debug_ipc::AddressRegion> aspace);
  void SetFrames(const std::vector<Frame*>& frames);
  void SetMemory(MemoryDump dump);
  void SetRegisters(const std::vector<debug_ipc::RegisterCategory>&);

 private:
  void DoAnalysis();

  // Request callbacks.
  void OnAspace(const Err& err, std::vector<debug_ipc::AddressRegion> aspace);
  void OnRegisters(const Err& err,
                   const std::vector<debug_ipc::RegisterCategory>&);
  void OnMemory(const Err& err, MemoryDump dump);
  void OnFrames(fxl::WeakPtr<Thread> thread);

  // Returns true when all asynchronous things are available.
  bool HasEverything() const;

  // Call when something goes wrong to issue the callback with the given
  // error printed to it.
  void IssueError(const Err& err);

  // Adds to the annotations map the given description for the given address.
  // If there is already an annotation at that address, adds to the end.
  void AddAnnotation(uint64_t address, const std::string& str);

  // Retrieves the data value at the given address. Returns true if there was
  // data, or false if the memory is invalid.
  bool GetData(uint64_t address, uint64_t* out_value) const;

  // Returns a formatted string representing all annotations in the range (end
  // non-inclusive).
  std::string GetAnnotationsBetween(uint64_t address_begin,
                                    uint64_t address_end) const;

  // Returns a formatted string represenging with the given data value
  // points to (if possible). Returns an empty string otherwise.
  std::string GetPointedToAnnotation(uint64_t data) const;

  // May become invalid across the async callbacks, check before using.
  fxl::WeakPtr<Process> process_;

  // This map collects the address of everything we want to annotate in the
  // stack. This will include registers and frame pointers.
  std::map<uint64_t, std::string> annotations_;

  uint64_t begin_address_;
  uint32_t bytes_to_read_;

  Callback callback_;

  MemoryDump memory_;

  std::vector<debug_ipc::AddressRegion> aspace_;

  // Set when an asynchronous operation has failed. The callback will already
  // have been issued, so everything should immediately exit when this flag is
  // set.
  bool aborted_ = false;

  // The things that need to be queried asynchronously before dumping.
  bool have_registers_ = false;
  bool have_memory_ = false;
  bool have_frames_ = false;
  bool have_aspace_ = false;
};

}  // namespace internal
}  // namespace zxdb
