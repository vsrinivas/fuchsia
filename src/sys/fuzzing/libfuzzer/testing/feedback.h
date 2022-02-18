// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_LIBFUZZER_TESTING_FEEDBACK_H_
#define SRC_SYS_FUZZING_LIBFUZZER_TESTING_FEEDBACK_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <stddef.h>
#include <stdint.h>

#include "src/sys/fuzzing/common/result.h"

namespace fuzzing {

constexpr size_t kMaxNumFeedbackCounters = 256;

// Represents a single inline, 8-bit counter. See |RelayedFeedback| below.
struct Counter {
  uint16_t offset;
  uint8_t value;
};

// Represents the unit test's instructions to the test fuzzer as to what behaviors to emulate.
// See also |LibFuzzerRunnerTest::setFeedback| and |TestFuzzer::TestOneInput|.
struct RelayedFeedback {
  FuzzResult result;
  bool leak_suspected;
  size_t num_counters;
  Counter counters[kMaxNumFeedbackCounters];
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_LIBFUZZER_TESTING_FEEDBACK_H_
