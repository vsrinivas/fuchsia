// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmarkfidl/cpp/fidl.h>
#include <perftest/perftest.h>

#include "builder_benchmark_util.h"
#include "decode_benchmark_util.h"
#include "encode_benchmark_util.h"

namespace {

// Builds an EmptyStruct.
::benchmarkfidl::EmptyStruct BuildEmptyStruct() {
  ::benchmarkfidl::EmptyStruct v1;
  return v1;
}

// Benchmarks building an EmptyStruct.
bool BenchmarkBuilderEmptyStruct(perftest::RepeatState* state) {
  return hlcpp_benchmarks::BuilderBenchmark(state, BuildEmptyStruct);
}
// Benchmarks encoding an EmptyStruct, with breakdown of steps.
bool BenchmarkEncodeEmptyStruct(perftest::RepeatState* state) {
  return hlcpp_benchmarks::EncodeBenchmark(state, BuildEmptyStruct());
}
// Benchmarks decoding an EmptyStruct.
bool BenchmarkDecodeEmptyStruct(perftest::RepeatState* state) {
  return hlcpp_benchmarks::DecodeBenchmark(state, BuildEmptyStruct());
}

void RegisterTests() {
  perftest::RegisterTest("HLCPP/Builder/EmptyStruct/WallTime", BenchmarkBuilderEmptyStruct);
  perftest::RegisterTest("HLCPP/Encode/EmptyStruct/WallTime", BenchmarkEncodeEmptyStruct);
  perftest::RegisterTest("HLCPP/Decode/EmptyStruct/WallTime", BenchmarkDecodeEmptyStruct);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
