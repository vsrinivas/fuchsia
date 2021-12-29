// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>

#include <perftest/perftest.h>
#include <test/benchmarkfidl/cpp/fidl.h>

#include "data.h"

namespace {

#define BENCHMARK_FOR(Name, Num)                                                 \
  bool Benchmark##Name##S##Num(perftest::RepeatState* state) {                   \
    while (state->KeepRunning()) {                                               \
      const char* bytes = lib_fidl_microbenchmarks::Name##_S_##Num;              \
      uint32_t num_bytes = sizeof(lib_fidl_microbenchmarks::Name##_S_##Num) - 1; \
      ZX_ASSERT(ZX_OK == fidl_validate_string(bytes, num_bytes));                \
    }                                                                            \
    return true;                                                                 \
  }

BENCHMARK_FOR(Utf8, 258)
BENCHMARK_FOR(Utf8, 1025)
BENCHMARK_FOR(Utf8, 4098)
BENCHMARK_FOR(Utf8, 16385)
BENCHMARK_FOR(Utf8, 65536)

BENCHMARK_FOR(Ascii, 258)
BENCHMARK_FOR(Ascii, 1025)
BENCHMARK_FOR(Ascii, 4098)
BENCHMARK_FOR(Ascii, 16385)
BENCHMARK_FOR(Ascii, 65536)

void RegisterTests() {
  perftest::RegisterTest("LibFIDL/fidl_validate_string/258/WallTime", BenchmarkUtf8S258);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/1025/WallTime", BenchmarkUtf8S1025);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/4098/WallTime", BenchmarkUtf8S4098);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/16385/WallTime", BenchmarkUtf8S16385);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/65536/WallTime", BenchmarkUtf8S65536);

  perftest::RegisterTest("LibFIDL/fidl_validate_string/ASCII/258/WallTime", BenchmarkAsciiS258);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/ASCII/1025/WallTime", BenchmarkAsciiS1025);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/ASCII/4098/WallTime", BenchmarkAsciiS4098);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/ASCII/16385/WallTime", BenchmarkAsciiS16385);
  perftest::RegisterTest("LibFIDL/fidl_validate_string/ASCII/65536/WallTime", BenchmarkAsciiS65536);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
