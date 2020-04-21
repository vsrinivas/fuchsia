// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>

#include <benchmarkfidl/cpp/fidl.h>
#include <perftest/perftest.h>

#include "data.h"

namespace {

#define BENCHMARK_FOR(Num)                                              \
  bool BenchmarkS ## Num(perftest::RepeatState* state) {                \
    while (state->KeepRunning()) {                                      \
      const char* bytes = lib_fidl_benchmarks::S_ ## Num;               \
      uint32_t num_bytes = sizeof(lib_fidl_benchmarks::S_ ## Num) - 1;  \
      ZX_ASSERT(ZX_OK == fidl_validate_string(bytes, num_bytes));       \
    }                                                                   \
    return true;                                                        \
  }

BENCHMARK_FOR(258)
BENCHMARK_FOR(1025)
BENCHMARK_FOR(4098)
BENCHMARK_FOR(16385)
BENCHMARK_FOR(65536)

void RegisterTests() {
  perftest::RegisterTest("LibFIDL/fidl_validate_string/258/WallTime", BenchmarkS258);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/1025/WallTime", BenchmarkS1025);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/4098/WallTime", BenchmarkS4098);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/16385/WallTime", BenchmarkS16385);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/65536/WallTime", BenchmarkS65536);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
