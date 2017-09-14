// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <zircon/syscalls.h>

class Fifo : public benchmark::Fixture {};

BENCHMARK_F(Fifo, Create)(benchmark::State& state) {
  zx_handle_t out0;
  zx_handle_t out1;
  zx_status_t status;
  while (state.KeepRunning()) {
    status = zx_fifo_create(2, 2048, 0, &out0, &out1);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to create fifo");
      return;
    }
    state.PauseTiming();
    zx_handle_close(out0);
    zx_handle_close(out1);
    state.ResumeTiming();
  }
}
