// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

class Port : public benchmark::Fixture {};

BENCHMARK_DEFINE_F(Port, Create)(benchmark::State& state) {
  zx_handle_t out;
  while (state.KeepRunning()) {
    if (zx_port_create(state.range(0), &out) != ZX_OK) {
      state.SkipWithError("Failed to create port");
      return;
    }
    state.PauseTiming();
    zx_handle_close(out);
    state.ResumeTiming();
  }
}
BENCHMARK_REGISTER_F(Port, Create)
    ->Arg(0)
    ->Arg(0);
