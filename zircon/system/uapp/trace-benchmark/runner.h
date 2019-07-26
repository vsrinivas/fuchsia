// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>
#include <fbl/function.h>

#include <zircon/syscalls.h>
#include <lib/zx/time.h>

#include <utility>

// Lines of text for each result are prefixed with this.
constexpr const char* kTestOutputPrefix = "  - ";

constexpr unsigned kWarmUpIterations = 10;
// N.B. This value can't be so large that the buffer fills in oneshot mode.
// The benchmark will assert-fail if the buffer fills: Otherwise the benchmark
// is invalid.
constexpr unsigned kDefaultRunIterations = 100000;

// The number of test runs to do.
// We do this many runs and report min,max,average.
// We do this because there's some variability in the runs, and this helps
// identify when it's happening and cope.
constexpr unsigned kNumTestRuns = 10;

// Measures how long it takes to run some number of iterations of a closure.
// Returns a value in microseconds.
template <typename T>
float Measure(unsigned iterations, const T& closure) {
  zx_ticks_t start = zx_ticks_get();
  for (unsigned i = 0; i < iterations; ++i) {
    closure();
  }
  zx_ticks_t stop = zx_ticks_get();
  return (static_cast<float>(stop - start) * 1000000.f / static_cast<float>(zx_ticks_per_second()));
}

using thunk = fbl::Function<void()>;

// Runs a closure repeatedly and prints its timing.
template <typename T>
void RunAndMeasure(const char* test_name, const char* spec_name, unsigned iterations,
                   const T& closure, thunk setup, thunk teardown) {
  printf("\n* %s: %s ...\n", spec_name, test_name);

  setup();
  float warm_up_time = Measure(kWarmUpIterations, closure);
  teardown();
  printf("%swarm-up: %u iterations in %.3f us, %.3f us per iteration\n", kTestOutputPrefix,
         kWarmUpIterations, warm_up_time, warm_up_time / kWarmUpIterations);

  float run_times[kNumTestRuns];
  for (unsigned i = 0; i < kNumTestRuns; ++i) {
    setup();
    run_times[i] = Measure(iterations, closure);
    teardown();
    zx::nanosleep(zx::deadline_after(zx::msec(10)));
  }

  float min = 0, max = 0;
  float cumulative = 0;
  for (const auto rt : run_times) {
    if (min == 0 || min > rt)
      min = rt;
    if (max == 0 || max < rt)
      max = rt;
    cumulative += rt;
  }
  float average = cumulative / kNumTestRuns;

  printf("%srun: %u test runs, %u iterations per run\n", kTestOutputPrefix, kNumTestRuns,
         iterations);
  printf("%stotal (usec): min: %.3f, max: %.3f, ave: %.3f\n", kTestOutputPrefix, min, max, average);
  printf("%sper-iteration (usec): min: %.3f\n",
         // The static cast is to avoid a "may change value" warning.
         kTestOutputPrefix, min / static_cast<float>(iterations));
}

template <typename T>
void RunAndMeasure(const char* test_name, const char* spec_name, const T& closure, thunk setup,
                   thunk teardown) {
  RunAndMeasure(test_name, spec_name, kDefaultRunIterations, closure, std::move(setup),
                std::move(teardown));
}
