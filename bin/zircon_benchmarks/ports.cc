// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

class Port : public benchmark::Fixture {};

BENCHMARK_DEFINE_F(Port, Create)(benchmark::State& state) {
  mx_handle_t out;
  while (state.KeepRunning()) {
    if (mx_port_create(state.range(0), &out) != MX_OK) {
      state.SkipWithError("Failed to create port");
      return;
    }
    state.PauseTiming();
    mx_handle_close(out);
    state.ResumeTiming();
  }
}
BENCHMARK_REGISTER_F(Port, Create)
    ->Arg(0)
    ->Arg(0);
