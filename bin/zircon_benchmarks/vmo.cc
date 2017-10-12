// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <zircon/syscalls.h>

class Vmo : public benchmark::Fixture {};

BENCHMARK_F(Vmo, Create)(benchmark::State& state) {
  zx_handle_t vmo;
  while(state.KeepRunning()) {
    if (zx_vmo_create(64 * 1024, 0, &vmo) != ZX_OK) {
      state.SkipWithError("Failed to create vmo");
      return;
    }
    state.PauseTiming();
    zx_handle_close(vmo);
    state.ResumeTiming();
  }
}
