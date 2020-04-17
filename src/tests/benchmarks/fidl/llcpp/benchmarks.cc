// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmarkfidl/llcpp/fidl.h>
#include <perftest/perftest.h>

#include "builder_benchmark_util.h"
#include "decode_benchmark_util.h"
#include "encode_benchmark_util.h"

namespace {

// Builds an EmptyStruct using std::make_unique for out-of-line objects.
::llcpp::benchmarkfidl::EmptyStruct BuildEmptyStructHeap(perftest::RepeatState* state) {
  auto obj = ::llcpp::benchmarkfidl::EmptyStruct();
  if (state != nullptr)
    state->NextStep();  // Next: Destructors
  return obj;
}

// Builds an EmptyStruct using fidl::Allocator for out-of-line objects.
::llcpp::benchmarkfidl::EmptyStruct BuildEmptyStructAllocator(perftest::RepeatState* state,
                                                              fidl::Allocator* allocator) {
  auto obj = ::llcpp::benchmarkfidl::EmptyStruct();
  if (state != nullptr)
    state->NextStep();  // Next: Destructors
  return obj;
}

// Builds an EmptyStruct without using an allocator.
::llcpp::benchmarkfidl::EmptyStruct BuildEmptyStructUnowned(perftest::RepeatState* state) {
  auto obj = ::llcpp::benchmarkfidl::EmptyStruct();
  if (state != nullptr)
    state->NextStep();  // Next: Destructors
  return obj;
}

// Benchmarks building an EmptyStruct using std::make_unique for out-of-line objects.
bool BenchmarkBuilderEmptyStructHeap(perftest::RepeatState* state) {
  return llcpp_benchmarks::BuilderBenchmark(state, BuildEmptyStructHeap);
}

// Benchmarks building an EmptyStruct using fidl::Allocator for out-of-line objects.
bool BenchmarkBuilderEmptyStructBufferAllocator(perftest::RepeatState* state) {
  // TODO(fxb/49640) This allocation might impact the builder time if it is too large.
  return llcpp_benchmarks::BuilderBenchmark<fidl::BufferAllocator<4096>>(state,
                                                                         BuildEmptyStructAllocator);
}

// Benchmarks building an EmptyStruct without using an allocator.
bool BenchmarkBuilderEmptyStructUnowned(perftest::RepeatState* state) {
  return llcpp_benchmarks::BuilderBenchmark(state, BuildEmptyStructUnowned);
}

// Benchmarks encoding an EmptyStruct, with breakdown of steps.
bool BenchmarkEncodeEmptyStruct(perftest::RepeatState* state) {
  fidl::aligned<::llcpp::benchmarkfidl::EmptyStruct> aligned_obj = BuildEmptyStructHeap(nullptr);
  return llcpp_benchmarks::EncodeBenchmark(state, &aligned_obj);
}

// Benchmarks decoding an EmptyStruct.
bool BenchmarkDecodeEmptyStruct(perftest::RepeatState* state) {
  std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  return llcpp_benchmarks::DecodeBenchmark<::llcpp::benchmarkfidl::EmptyStruct>(state,
                                                                                std::move(data));
}

void RegisterTests() {
  perftest::RegisterTest("LLCPP/Builder/EmptyStruct/Heap/Steps", BenchmarkBuilderEmptyStructHeap);
  perftest::RegisterTest("LLCPP/Builder/EmptyStruct/BufferAllocator/Steps",
                         BenchmarkBuilderEmptyStructBufferAllocator);
  perftest::RegisterTest("LLCPP/Builder/EmptyStruct/Unowned/Steps",
                         BenchmarkBuilderEmptyStructUnowned);

  perftest::RegisterTest("LLCPP/Encode/EmptyStruct/Steps", BenchmarkEncodeEmptyStruct);

  perftest::RegisterTest("LLCPP/Decode/EmptyStruct/Steps", BenchmarkDecodeEmptyStruct);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
