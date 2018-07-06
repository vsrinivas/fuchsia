// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "benchmarks.h"

#include "runner.h"

namespace {

// "large" must be sized so it does not overflow during oneshot tests.
// The benchmark will assert-fail if the buffer fills: Otherwise the benchmark
// is invalid.
static constexpr size_t kLargeBufferSizeBytes = 16 * 1024 * 1024;

} // namespace

int main(int argc, char** argv) {
    RunTracingDisabledBenchmarks();
    RunNoTraceBenchmarks();

    static const BenchmarkSpec specs[] = {
        {
            // The buffer is not allowed to fill in oneshot mode, so there's
            // no use in reporting the buffer size in the name here.
            "oneshot",
            kLargeBufferSizeBytes,
            kDefaultRunIterations,
        },
    };

    for (const auto& spec : specs) {
        RunTracingEnabledBenchmarks(&spec);
    }

    printf("\nTracing benchmarks completed.\n");
    return 0;
}
