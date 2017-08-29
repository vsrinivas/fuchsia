// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#include <magenta/syscalls.h>

static constexpr unsigned kWarmUpIterations = 100;
static constexpr unsigned kRunIterations = 1000000;

// Measures how long it takes to run some number of iterations of a closure.
// Returns a value in microseconds.
template <typename T>
float Measure(unsigned iterations, const T& closure) {
    uint64_t start = mx_ticks_get();
    for (unsigned i = 0; i < iterations; i++) {
        closure();
    }
    uint64_t stop = mx_ticks_get();
    return static_cast<float>(stop - start) * 1000000.f /
           static_cast<float>(mx_ticks_per_second());
}

// Runs a closure repeatedly and prints its timing.
template <typename T>
void Run(const char* test_name, const T& closure) {
    printf("* %s...\n", test_name);

    float warm_up_time = Measure(kWarmUpIterations, closure);
    printf("  - warm-up: %u iterations in %.1f us, %.3f us per iteration\n",
           kWarmUpIterations, warm_up_time, warm_up_time / kWarmUpIterations);

    float run_time = Measure(kRunIterations, closure);
    printf("  - run: %u iterations in %.1f us, %.3f us per iteration\n\n",
           kRunIterations, run_time, run_time / kRunIterations);
}

// Runs benchmarks which need tracing disabled.
void RunTracingDisabledBenchmarks();

// Runs benchmarks which need tracing enabled.
void RunTracingEnabledBenchmarks();

// Runs benchmarks with NTRACE macro defined.
void RunNoTraceBenchmarks();
