// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_COVERAGE_H_
#define SRC_LIB_FUZZING_FIDL_COVERAGE_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/fxl/synchronization/thread_annotations.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <stddef.h>
#include <zircon/types.h>

#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "shared-memory.h"
#include "traced-instruction.h"

namespace fuzzing {
namespace {

using ::fuchsia::fuzzer::Coverage;
using ::fuchsia::mem::Buffer;

}  // namespace

// Forward declaration
class AggregatedCoverage;

// The Coverage service aggregates coverage information from multiple processes and passes it to the
// __sanitizer_cov_* interface. See also |SanitizerCovProxy|, the per-process client of the service.
class CoverageImpl : public Coverage {
 public:
  explicit CoverageImpl(AggregatedCoverage *aggregate);
  virtual ~CoverageImpl();

  // FIDL Methods
  void AddInline8BitCounters(Buffer ctrs, AddInline8BitCountersCallback callback) override;
  void AddPcTable(Buffer pcs, AddPcTableCallback callback) override;
  void AddTraces(zx::vmo traces, AddTracesCallback callback) override;

 private:
  // If |status| indicates an error, asks the AggregatedCoverage to close its binding and returns
  // false, otherwise returns true.
  bool Check(zx_status_t status);

  // Memory from other processes shared with this service.
  std::vector<SharedMemory> mapped_;
  SharedMemory traces_;

  // Interface to the __sanitizer_cov_trace_* calls.
  AggregatedCoverage *aggregate_;
};

// The AggregatedCoverage class manages a collection of single-client Coverage connections. It also
// coordinates and provides thread-safety for invoking the __sanitizer_cov_trace_* interface.
class AggregatedCoverage final {
 public:
  AggregatedCoverage();
  ~AggregatedCoverage();

  fidl::InterfaceRequestHandler<Coverage> GetHandler();

  // Signals all connected proxies that the current iteration is complete, i.e. they should ensure
  // their coverage data is updated.
  zx_status_t CompleteIteration();

  // Returns this instance to its original state.
  void Reset();

 protected:
  friend class CoverageImpl;

  // Adds a wait item for the shared memory from a call to |Coverage::AddTraces|.
  zx_status_t Add(const SharedMemory &traces) FXL_LOCKS_EXCLUDED(lock_);

  // Close and removes the binding for an associated coverage instance.
  void Close(CoverageImpl *coverage, zx_status_t epitaph);

 private:
  // Starts processing coverage.
  void Start();

  // Manage the shared VMOs' signals and process the data from the proxies accordingly.
  void ProcessAll();
  void ProcessTraces(Instruction *traces, uint64_t distinguisher);

  // Stops processing coverage.
  void Stop();

  // Binding set that owns the Coverage objects.
  fidl::BindingSet<Coverage, std::unique_ptr<Coverage>> bindings_;

  // Thread used to run |ProcessAll|.
  std::thread processor_;

  // An array of wait items used to monitor connected Coverage/Proxy pairs.
  zx_wait_item_t items_[ZX_WAIT_MANY_MAX_ITEMS];
  std::atomic<size_t> num_items_;

  // The instruction trace data associated with each wait item. This object MUST keep the two arrays
  // in sync.
  Instruction *traces_[ZX_WAIT_MANY_MAX_ITEMS];
  uint64_t distinguishers_[ZX_WAIT_MANY_MAX_ITEMS];
  uint64_t num_distinguishers_;

  // The first wait item always corresponds to an event used to control iteration state.
  zx::event controller_;

  // This lock guards against concurrent calls to |Add|.
  std::mutex lock_;

  // Synchronization objects used to coordinate iterations.
  std::atomic<size_t> pending_;
  sync_completion_t sync_;
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_COVERAGE_H_
