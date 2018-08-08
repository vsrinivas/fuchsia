// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <benchmark/benchmark.h>
#include <perftest/perftest.h>

namespace fbenchmark {

int BenchmarksMain(int argc, char** argv, bool run_gbenchmark);

// Register a benchmark that is specified by a class.
//
// Any class may be used as long as it provides a Run() method that runs an
// iteration of the test.
template <class TestClass, typename... Args>
void RegisterTest(const char* test_name, Args... args) {
  benchmark::RegisterBenchmark(test_name, [=](benchmark::State& state) {
    TestClass test(args...);
    while (state.KeepRunning())
      test.Run();
  });

  perftest::RegisterTest(test_name, [=](perftest::RepeatState* state) {
    TestClass test(args...);
    while (state->KeepRunning()) {
      test.Run();
    }
    return true;
  });
}

typedef void TestFunc();

// Convenience routine for registering a benchmark that is specified by a
// function.  This is for benchmarks that don't set up any fixtures that
// are shared across invocations of the function.
//
// This takes the function as a template parameter rather than as a value
// parameter in order to avoid the potential cost of an indirect function
// call and hence be consistent with RegisterTest() (which also avoids an
// indirect function call).
template <TestFunc test_func>
void RegisterTestFunc(const char* test_name) {
  class TestClass {
   public:
    void Run() { test_func(); }
  };
  RegisterTest<TestClass>(test_name);
}

}  // namespace fbenchmark
