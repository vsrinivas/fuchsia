// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_LIBFUZZER_TESTING_FUZZER_H_
#define SRC_SYS_FUZZING_LIBFUZZER_TESTING_FUZZER_H_

#include <stddef.h>
#include <stdint.h>

#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/shared-memory.h"

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

  std::unique_ptr<ComponentContext> context_;
  std::unique_ptr<AsyncEventPair> eventpair_;
  SharedMemory test_input_buffer_;
  SharedMemory feedback_buffer_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_LIBFUZZER_TESTING_FUZZER_H_
