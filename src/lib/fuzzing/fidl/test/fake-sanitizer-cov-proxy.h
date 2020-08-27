// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_TEST_FAKE_SANITIZER_COV_PROXY_H_
#define SRC_LIB_FUZZING_FIDL_TEST_FAKE_SANITIZER_COV_PROXY_H_

#include <stdint.h>

#include <mutex>
#include <vector>

#include "fake-libfuzzer.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "traced-instruction.h"

namespace fuzzing {

// This class provides a fake implmentation similar to |SanitizerCovProxy|. In particular, it
//  includes the same static methods referenced by sanitizer-cov.inc and can be used to generate
// __sanitizer_cov_* symbols for that interface. Unlike the real |SanitizerCovProxy|, this class
// simply logs the calls to the __sanitizer_cov_* interface.
class FakeSanitizerCovProxy {
 public:
  // Implements the sanitizer-cov interface as simply a log of calls made.
  static void Init8BitCounters(uint8_t *start, uint8_t *stop);
  static void InitPcs(const uintptr_t *pcs_beg, const uintptr_t *pcs_end);
  static void Trace(Instruction::Type type, uintptr_t pc, uint64_t arg0, uint64_t arg1);
  static void TraceSwitch(uintptr_t pc, uint64_t val, uint64_t *cases);

  // Returns whether a memory region of the given length was added to the singleton.
  static bool HasInit(size_t length);

  // Fetches parts of a log entry and return true; or return false if log is empty.
  static size_t Count(uint64_t type, uint64_t pc, uint64_t arg0, uint64_t arg1);

  // Clears the log.
  static void Reset();

 private:
  static FakeSanitizerCovProxy *GetInstance();

  void InitImpl(size_t length);
  bool HasInitImpl(size_t length);

  void TraceImpl(uint64_t type, uintptr_t pc, uint64_t arg0, uint64_t arg1);
  size_t CountImpl(uint64_t type, uint64_t pc, uint64_t arg0, uint64_t arg1);

  void ResetImpl();

  std::mutex lock_;
  std::vector<size_t> inits_;
  std::vector<uint64_t> traces_;
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_TEST_FAKE_SANITIZER_COV_PROXY_H_
