// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_
#define SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_

#include <lib/fidl/llcpp/aligned.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/visitor.h>
#include <lib/fidl/walker.h>

#include <perftest/perftest.h>

namespace walker_benchmarks {
namespace internal {

// Linearizes by encoding and then decoding into a linearized form.
// fidl::Linearize is being removed in favor of fidl::LinearizeAndEncode,
// so it is no longer possible to directly linearize arbitrary values.
// TODO(fxbug.dev/53743) Change the walker to walk encoded bytes.
template <typename FidlType>
struct LinearizedResult {
  fidl::OwnedOutgoingMessage<FidlType> encoded;
  zx_status_t status;

  explicit LinearizedResult(FidlType* value) : encoded(value) {
    if (!encoded.ok() || encoded.error() != nullptr) {
      status = encoded.status();
      return;
    }
    auto decoded = fidl::IncomingMessage<FidlType>::FromOutgoingWithRawHandleCopy(&encoded);
    if (!decoded.ok() || decoded.error() != nullptr) {
      status = decoded.status();
      return;
    }
    status = ZX_OK;
  }
};

void Walk(const fidl_type_t* fidl_type, uint8_t* data);

}  // namespace internal

template <typename FidlType, typename BuilderFunc>
bool WalkerBenchmark(perftest::RepeatState* state, BuilderFunc builder) {
  builder([state](FidlType value) {
    fidl::aligned<FidlType> aligned_value = std::move(value);
    auto linearize_result = internal::LinearizedResult<FidlType>(&aligned_value.value);
    ZX_ASSERT(linearize_result.status == ZX_OK);

    while (state->KeepRunning()) {
      internal::Walk(FidlType::Type, linearize_result.encoded.GetOutgoingMessage().bytes());
    }
  });

  return true;
}

}  // namespace walker_benchmarks

#endif  // SRC_TESTS_BENCHMARKS_FIDL_WALKER_WALKER_BENCHMARK_UTIL_H_
