// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_SANITIZER_COV_PROXY_H_
#define SRC_LIB_FUZZING_FIDL_SANITIZER_COV_PROXY_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/synchronization/thread_annotations.h>
#include <lib/sync/completion.h>
#include <stdint.h>
#include <zircon/status.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "shared-memory.h"
#include "traced-instruction.h"

namespace fuzzing {
namespace {

using ::fuchsia::fuzzer::CoveragePtr;
using ::fuchsia::mem::Buffer;

}  // namespace

// This class can be used with sanitizer-cov.inc to provide a __sanitizer_cov_*-like interface that
// proxies all calls to a process running a fuchsia.fuzzer.Coverage FIDL service.
class SanitizerCovProxy {
 public:
  // Singleton. Tests can avoid the proxy autoconnecting by calling this with |autoconnect| set to
  // false before any other calls.
  static SanitizerCovProxy *GetInstance(bool autoconnect = true);
  virtual ~SanitizerCovProxy();

  // Sets the Coverage service this proxy is connected to. Used for testing (autoconnect=false).
  zx_status_t SetCoverage(CoveragePtr coverage) FXL_LOCKS_EXCLUDED(lock_);

  // Analogous to __sanitizer_cov_8bit_counters_init. Note that this method blocks until the proxy
  // receives a response from the Coverage service to ensure coverage data is first recorded in the
  // same iteration that the proxy connects.
  static void Init8BitCounters(uint8_t *start, uint8_t *stop) {
    SanitizerCovProxy::GetInstance()->Init8BitCountersImpl(start, stop);
  }

  // Analogous to __sanitizer_cov_pcs_init. Note that this method blocks until the proxy receives a
  // response from the Coverage service to ensure coverage data is first recorded in the same
  // iteration that the proxy connects.
  static void InitPcs(const uintptr_t *pcs_beg, const uintptr_t *pcs_end) {
    SanitizerCovProxy::GetInstance()->InitPcsImpl(pcs_beg, pcs_end);
  }

  // Analogous to __sanitizer_cov_trace_*, except for __sanitizer_cov_trace_switch. Note that traces
  // are ignored by the Coverage service is between fuzzing iterations.
  static void Trace(Instruction::Type type, uintptr_t pc, uint64_t arg0, uint64_t arg1) {
    SanitizerCovProxy::GetInstance()->TraceImpl(type, pc, arg0, arg1);
  }

  // Analogous to __sanitizer_cov_trace_switch. Note that traces are ignored by the Coverage service
  // is between fuzzing iterations.
  static void TraceSwitch(uintptr_t pc, uint64_t val, uint64_t *cases) {
    SanitizerCovProxy::GetInstance()->TraceSwitchImpl(pc, val, cases);
  }

  // Sets this object to its initial state.
  void Reset() FXL_LOCKS_EXCLUDED(lock_);

 private:
  // If autoconnect is true, this constructor starts a dispatcher loop, retrieves the service
  // directory from the namespace, and connects to a discoverable Coverage service. If it is false,
  // as it is when testing, callers must provide a bound CoveragePtr via |SetCoverage|.
  explicit SanitizerCovProxy(bool autoconnect);

  // Non-static implementations of the methods above.
  void Init8BitCountersImpl(uint8_t *start, uint8_t *stop) FXL_LOCKS_EXCLUDED(lock_);
  void InitPcsImpl(const uintptr_t *pcs_beg, const uintptr_t *pcs_end) FXL_LOCKS_EXCLUDED(lock_);
  void TraceImpl(Instruction::Type type, uintptr_t pc, uint64_t arg0, uint64_t arg1);
  void TraceSwitchImpl(uintptr_t pc, uint64_t val, uint64_t *cases);

  // Creates a mapped VMO, records the original and mapped pointers in a region, and returns the VMO
  // as a sharable fuchsia.mem.Buffer.
  //
  // TODO(aarongreen): Currently, using this approach for the inline 8-bit counters and PC tables
  // requires the proxy to copy data between the memory region specified by sanitizer_common and the
  // mapped VMO. Ideally, this would instead use the writable VMO already created for the BSS
  // section, see //src/lib/process_builder/src/elf_load.rs. Unfortunately, the process doesn't
  // currently get a handle to that VMO.
  zx_status_t CreateSharedBufferLocked(const void *start, const void *end, Buffer *out_buffer)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Accessors to the underlying trace array. These should only be used for testing.
  const zx::vmo *vmo() { return vmo_; }
  Instruction *traces() { return traces_; }
  friend class FakeCoverage;

  // The number of threads in the code under test is undetermined. Concurrent access to infrequently
  // updated variables is managed by |lock_|. Concurrency to the instruction trace array happens
  // frequently and is managed locklessly by |state_|.
  std::mutex lock_;
  std::atomic<uint64_t> state_;

  // FIDL dispatcher loop. Null when testing.
  std::unique_ptr<async::Loop> loop_;

  // Interface to the Coverage service.
  CoveragePtr coverage_ FXL_GUARDED_BY(lock_);

  // Updatable memory regions shared with the Coverage service. Concurrency is managed by |lock_|.
  std::map<zx_vaddr_t, SharedMemory> regions_ FXL_GUARDED_BY(lock_);
  SharedMemory shmem_ FXL_GUARDED_BY(lock_);

  // Additional pointers to the shared memory for instruction traces. For performance, these
  // interfaces to the memory have concurrency locklessly managed by |state_|.
  const zx::vmo *vmo_;
  Instruction *traces_;

  // Dedicated thread to transfer coverage maps and insert sentinels in instruction traces.
  std::thread collector_;

  // Per-instruction-buffer associated synchronization objects.
  struct BufferSync {
    sync_completion_t write;
    sync_completion_t reset;
  } syncs_[kNumInstructionBuffers];

  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(SanitizerCovProxy);
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_SANITIZER_COV_PROXY_H_
