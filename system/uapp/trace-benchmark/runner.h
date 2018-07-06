// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>

#include <zircon/syscalls.h>

static constexpr unsigned kWarmUpIterations = 10;
// N.B. This value can't be so large that the buffer fills in oneshot mode.
// The benchmark will assert-fail if the buffer fills: Otherwise the benchmark
// is invalid.
static constexpr unsigned kDefaultRunIterations = 100000;

// Measures how long it takes to run some number of iterations of a closure.
// Returns a value in microseconds.
template <typename T>
float Measure(unsigned iterations, const T& closure) {
    zx_ticks_t start = zx_ticks_get();
    for (unsigned i = 0; i < iterations; ++i) {
        closure();
    }
    zx_ticks_t stop = zx_ticks_get();
    return (static_cast<float>(stop - start) * 1000000.f /
            static_cast<float>(zx_ticks_per_second()));
}

// Runs a closure repeatedly and prints its timing.
template <typename T>
void RunAndMeasure(const char* test_name, unsigned iterations,
                   const T& closure) {
    printf("* %s...\n", test_name);

    float warm_up_time = Measure(kWarmUpIterations, closure);
    printf("  - warm-up: %u iterations in %.1f us, %.3f us per iteration\n",
           kWarmUpIterations, warm_up_time, warm_up_time / kWarmUpIterations);

    float run_time = Measure(iterations, closure);
    printf("  - run: %u iterations in %.1f us, %.3f us per iteration\n",
           // The static cast is to avoid a "may change value" warning.
           iterations, run_time, run_time / static_cast<float>(iterations));
}

template <typename T>
void RunAndMeasure(const char* test_name, const T& closure) {
    RunAndMeasure(test_name, kDefaultRunIterations, closure);
}
