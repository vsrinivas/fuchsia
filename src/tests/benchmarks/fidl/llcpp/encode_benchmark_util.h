// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ENCODE_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ENCODE_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/coding.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>

#include <perftest/perftest.h>

namespace {

constexpr size_t MaxLinearizeStackSize = 512;

// The stack-allocated buffer should hold the inline size of the object and 512 bytes (used when
// LLCPP stack-allocates out of line elements).
template <typename FidlType, size_t StackSize = MaxLinearizeStackSize>
constexpr size_t BufferSize = std::max(FIDL_ALIGN(sizeof(FidlType)), StackSize);

template <typename FidlType>
constexpr size_t MessageSize =
    ::fidl::internal::ClampedMessageSize<FidlType, ::fidl::MessageDirection::kSending>();

template <typename FidlType>
struct BenchmarkLinearizeResult {
  fidl::LinearizeResult<FidlType> result;
  std::unique_ptr<uint8_t[]> owned_buffer;
};

template <typename FidlType>
BenchmarkLinearizeResult<FidlType> InlineLinearize(perftest::RepeatState* state, FidlType* value,
                                                   uint8_t buffer[BufferSize<FidlType>]) {
  if (state != nullptr)
    state->NextStep();  // Finished allocating buffer for linearization.
  return BenchmarkLinearizeResult<FidlType>{
      .result =
          fidl::LinearizeResult(ZX_OK, nullptr,
                                fidl::DecodedMessage<FidlType>(fidl::BytePart(
                                    reinterpret_cast<uint8_t*>(value), FidlAlign(sizeof(FidlType)),
                                    FidlAlign(sizeof(FidlType))))),
      .owned_buffer = nullptr,
  };
}

template <typename FidlType>
BenchmarkLinearizeResult<FidlType> StackAllocateLinearize(perftest::RepeatState* state,
                                                          FidlType* value,
                                                          uint8_t buffer[BufferSize<FidlType>]) {
  if (state != nullptr)
    state->NextStep();  // Finished allocating buffer for linearization.
  auto linearize_result = fidl::Linearize(value, fidl::BytePart(buffer, BufferSize<FidlType>));
  return BenchmarkLinearizeResult<FidlType>{
      .result = std::move(linearize_result),
      .owned_buffer = nullptr,
  };
}

template <typename FidlType>
BenchmarkLinearizeResult<FidlType> HeapAllocateLinearize(perftest::RepeatState* state,
                                                         FidlType* value) {
  constexpr size_t size = MessageSize<FidlType>;
  auto buffer = std::make_unique<uint8_t[]>(size);
  if (state != nullptr)
    state->NextStep();  // Finished allocating buffer for linearization.
  auto linearize_result = fidl::Linearize(value, fidl::BytePart(buffer.get(), size));
  return BenchmarkLinearizeResult<FidlType>{
      .result = std::move(linearize_result),
      .owned_buffer = std::move(buffer),
  };
}

template <typename FidlType>
BenchmarkLinearizeResult<FidlType> Linearize(perftest::RepeatState* state, FidlType* value,
                                             uint8_t buffer[BufferSize<FidlType>]) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  if constexpr (FidlType::HasPointer) {
    if constexpr (MessageSize<FidlType> <= MaxLinearizeStackSize) {
      return StackAllocateLinearize(state, value, buffer);
    }
    return HeapAllocateLinearize(state, value);
  }
  return InlineLinearize(state, value, buffer);
}

}  // namespace

namespace llcpp_benchmarks {

template <typename FidlType>
bool EncodeBenchmark(perftest::RepeatState* state, fidl::aligned<FidlType>* aligned_value) {
  static_assert(fidl::IsFidlType<FidlType>::value, "FIDL type required");

  state->DeclareStep("AllocateForLinearization/WallTime");
  state->DeclareStep("LinearizeStep/WallTime");
  state->DeclareStep("Encode/WallTime");
  state->DeclareStep("Destructors/WallTime");

  uint8_t linearize_buffer[BufferSize<FidlType>];

  while (state->KeepRunning()) {
    auto benchmark_linearize_result = Linearize(state, &aligned_value->value, linearize_buffer);
    auto& linearize_result = benchmark_linearize_result.result;
    ZX_ASSERT(linearize_result.status == ZX_OK && linearize_result.error == nullptr);

    state->NextStep();  // Finished Linearization. Next: Encode.

    auto encode_result = fidl::Encode(std::move(linearize_result.message));
    ZX_ASSERT(encode_result.status == ZX_OK && encode_result.error == nullptr);

    state->NextStep();  // Next: Destructors called.
  }

  return true;
}

}  // namespace llcpp_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_LLCPP_ENCODE_BENCHMARK_UTIL_H_
