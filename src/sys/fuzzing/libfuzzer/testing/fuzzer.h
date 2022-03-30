// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_LIBFUZZER_TESTING_FUZZER_H_
#define SRC_SYS_FUZZING_LIBFUZZER_TESTING_FUZZER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/sync/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/processargs.h>

#include <atomic>
#include <memory>

#include <test/fuzzer/cpp/fidl.h>

#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/common/testing/module.h"
#include "src/sys/fuzzing/libfuzzer/testing/feedback.h"
#include "testing/fidl/async_loop_for_test.h"

namespace fuzzing {

class TestFuzzer {
 public:
  TestFuzzer();
  ~TestFuzzer() = default;

  // Implementation of |LLVMFuzzerTestOneInput|.
  int TestOneInput(const uint8_t* data, size_t size);

 private:
  // Triggers various error conditions.
  void Crash();
  void OOM();
  void Timeout();

  fidl::test::AsyncLoopForTest loop_;
  ExecutorPtr executor_;
  AsyncEventPair eventpair_;
  SharedMemory test_input_buffer_;
  SharedMemory feedback_buffer_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_LIBFUZZER_TESTING_FUZZER_H_
