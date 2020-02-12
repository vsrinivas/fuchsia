// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_MICROBENCHMARKS_TEST_RUNNER_H_
#define SRC_TESTS_MICROBENCHMARKS_TEST_RUNNER_H_

#include <perftest/perftest.h>

namespace fbenchmark {

// Register a benchmark that is specified by a class.
//
// Any class may be used as long as it provides a Run() method that runs an
// iteration of the test.
template <class TestClass, typename... Args>
void RegisterTest(const char* test_name, Args... args) {
  perftest::RegisterTest(test_name, [=](perftest::RepeatState* state) {
    TestClass test(args...);
    while (state->KeepRunning()) {
      test.Run();
    }
    return true;
  });
}

}  // namespace fbenchmark

#endif  // SRC_TESTS_MICROBENCHMARKS_TEST_RUNNER_H_
