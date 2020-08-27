// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_TEST_FAKE_COVERAGE_H_
#define SRC_LIB_FUZZING_FIDL_TEST_FAKE_COVERAGE_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/vmo.h>
#include <stddef.h>

#include <deque>

#include "shared-memory.h"
#include "traced-instruction.h"

namespace fuzzing {
namespace {

using ::fuchsia::fuzzer::Coverage;
using ::fuchsia::mem::Buffer;

}  // namespace

// This class provides a faked implementation of the fuchsia::fuzzer::Coverage FIDL interface. It
// differs from the real implementation in that it does NOT call the __sanitizer_cov_* interface.
// Instead, it simply tracks what memory was shared with it and what traced instructions were
// provided.
class FakeCoverage final : public Coverage {
 public:
  FakeCoverage();
  ~FakeCoverage();

  Instruction *traces() { return traces_; }

  fidl::InterfaceRequestHandler<Coverage> GetHandler();
  void Configure();

  // FIDL methods
  void AddInline8BitCounters(Buffer inline_8bit_counters,
                             AddInline8BitCountersCallback callback) override;
  void AddPcTable(Buffer pcs, AddPcTableCallback callback) override;
  void AddTraces(zx::vmo traces, AddTracesCallback callback) override;

  // If shared memory was added via one of the |Add...| methods, moves that buffer to |out| and
  // returns true, otherwise returns false.
  bool MapPending(SharedMemory *out);

  void Resolve();

  // Returns the number of traced instructions of a given |type| that this object has received via
  // |Updatetraces|
  size_t Count(Instruction::Type type);

  // Notifies clients that a fuzzing iteration is complete.
  void SendIterationComplete();

  // Returns true if and only if all notified clients have called |CompleteUpdate|.
  bool HasCompleted();

 private:
  fidl::Binding<Coverage> binding_;
  std::deque<Buffer> pending_;
  const zx::vmo *vmo_;
  Instruction *traces_;
  size_t counts_[Instruction::kMaxValue + 1];
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_TEST_FAKE_COVERAGE_H_
