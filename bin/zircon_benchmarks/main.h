// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include <benchmark/benchmark.h>

namespace fbenchmark {

void RegisterTestRunner(const char* name, std::function<void()> func);
int BenchmarksMain(int argc, char** argv, bool run_gbenchmark);

// Register a benchmark that is specified by a class.
template <class TestClass, typename... Args>
void RegisterTest(const char* test_name, Args... args) {
  benchmark::RegisterBenchmark(
      test_name,
      [=](benchmark::State& state) {
        TestClass test(args...);
        while (state.KeepRunning())
          test.Run();
      });
  RegisterTestRunner(
      test_name,
      [=]() {
        TestClass test(args...);
        // Run the test a small number of times to ensure that doing
        // multiple runs works OK.
        for (int i = 0; i < 5; ++i)
          test.Run();
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
    void Run() {
      test_func();
    }
  };
  RegisterTest<TestClass>(test_name);
}

}  // namespace fbenchmark
