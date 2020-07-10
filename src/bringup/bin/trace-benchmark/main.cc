// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "benchmarks.h"

#include "runner.h"

namespace {

// Trace buffer sizes.
// "large" must be sized so it does not overflow during oneshot tests.
// The benchmark will assert-fail if the buffer fills: Otherwise the benchmark
// is invalid.
static constexpr size_t kLargeBufferSizeBytes = 16 * 1024 * 1024;
// "small" is sized so the buffer does fill, repeatedly, during the test.
// The number is chosen to make it easier to eyeball timing differences
// between large and small.
static constexpr size_t kSmallBufferSizeBytes = 16 * 1024;

}  // namespace

int main(int argc, char** argv) {
  RunTracingDisabledBenchmarks();
  RunNoTraceBenchmarks();

  static const BenchmarkSpec specs[] = {
      {
          // Note: The buffer is not allowed to fill in oneshot mode.
          "oneshot, 16MB buffer",
          TRACE_BUFFERING_MODE_ONESHOT,
          kLargeBufferSizeBytes,
          kDefaultRunIterations,
      },
      {
          "streaming, 16MB buffer",
          TRACE_BUFFERING_MODE_STREAMING,
          kLargeBufferSizeBytes,
          kDefaultRunIterations,
      },
      {
          "circular, 16MB buffer",
          TRACE_BUFFERING_MODE_CIRCULAR,
          kLargeBufferSizeBytes,
          kDefaultRunIterations,
      },
      {
          "streaming, 16K buffer",
          TRACE_BUFFERING_MODE_STREAMING,
          kSmallBufferSizeBytes,
          kDefaultRunIterations,
      },
      {
          "circular, 16K buffer",
          TRACE_BUFFERING_MODE_CIRCULAR,
          kSmallBufferSizeBytes,
          kDefaultRunIterations,
      },
  };

  for (const auto& spec : specs) {
    RunTracingEnabledBenchmarks(&spec);
  }

  printf("\nTracing benchmarks completed.\n");
  return 0;
}
