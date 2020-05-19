// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_UAPP_TRACE_BENCHMARK_BENCHMARKS_H_
#define ZIRCON_SYSTEM_UAPP_TRACE_BENCHMARK_BENCHMARKS_H_

#include <lib/trace-engine/types.h>
#include <stddef.h>

struct BenchmarkSpec {
  const char* name;
  trace_buffering_mode_t mode;
  size_t buffer_size;
  // The number of iterations is a parameter to make it easier to
  // experiment and debug.
  unsigned num_iterations;
};

// Runs benchmarks which need tracing disabled.
void RunTracingDisabledBenchmarks();

// Runs benchmarks which need tracing enabled.
void RunTracingEnabledBenchmarks(const BenchmarkSpec* spec);

// Runs benchmarks with NTRACE macro defined.
void RunNoTraceBenchmarks();

#endif  // ZIRCON_SYSTEM_UAPP_TRACE_BENCHMARK_BENCHMARKS_H_
