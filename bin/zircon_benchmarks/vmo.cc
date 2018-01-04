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

BENCHMARK_DEFINE_F(Vmo, Write)(benchmark::State& state) {
  zx_handle_t vmo;
  size_t bytes_written;
  uint64_t bytes_processed = 0;
  std::vector<char> buffer(state.range(0));

  if (zx_vmo_create(state.range(0), 0, &vmo) != ZX_OK) {
    state.SkipWithError("Failed to create vmo");
    return;
  }
  while(state.KeepRunning()) {
    if(zx_vmo_write(vmo, buffer.data(), 0, buffer.size(), &bytes_written) != ZX_OK) {
      state.SkipWithError("Failed to write to vmo");
    }
    state.PauseTiming();
    bytes_processed += bytes_written;
    state.ResumeTiming();
  }
  zx_handle_close(vmo);
  state.SetBytesProcessed(bytes_processed);
}

BENCHMARK_REGISTER_F(Vmo, Write)
    ->Arg(128 * 1024)
    ->Arg(1000 * 1024);

BENCHMARK_DEFINE_F(Vmo, Read)(benchmark::State& state) {
  zx_handle_t vmo;
  std::vector<char> buffer(state.range(0));
  uint64_t bytes_processed = 0;
  size_t bytes_read = 0;

  if (zx_vmo_create(state.range(0), 0, &vmo) != ZX_OK) {
    state.SkipWithError("Failed to create vmo");
    return;
  }
  while(state.KeepRunning()) {
    if(zx_vmo_read(vmo, buffer.data(), 0, buffer.size(), &bytes_read) != ZX_OK) {
      state.SkipWithError("Failed to read to vmo");
    }
    state.PauseTiming();
    bytes_processed += bytes_read;
    state.ResumeTiming();
  }
  zx_handle_close(vmo);
  state.SetBytesProcessed(bytes_processed);
}

BENCHMARK_REGISTER_F(Vmo, Read)
    ->Arg(128 * 1024)
    ->Arg(1000 * 1024);