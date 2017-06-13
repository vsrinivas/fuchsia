// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <magenta/syscalls.h>

class Fifo : public benchmark::Fixture {};

BENCHMARK_F(Fifo, Create)(benchmark::State& state) {
  mx_handle_t out0;
  mx_handle_t out1;
  mx_status_t status;
  while (state.KeepRunning()) {
    status = mx_fifo_create(2, 2048, 0, &out0, &out1);
    if (status != MX_OK) {
      state.SkipWithError("Failed to create fifo");
      return;
    }
    state.PauseTiming();
    mx_handle_close(out0);
    mx_handle_close(out1);
    state.ResumeTiming();
  }
}
