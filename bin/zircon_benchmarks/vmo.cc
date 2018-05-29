// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <zircon/syscalls.h>

class Vmo : public benchmark::Fixture {};

BENCHMARK_DEFINE_F(Vmo, Write)(benchmark::State& state) {
  zx_handle_t vmo;
  uint64_t bytes_processed = 0;
  std::vector<char> buffer(state.range(0));

  if (zx_vmo_create(state.range(0), 0, &vmo) != ZX_OK) {
    state.SkipWithError("Failed to create vmo");
    return;
  }
  while(state.KeepRunning()) {
    if(zx_vmo_write(vmo, buffer.data(), 0, buffer.size()) != ZX_OK) {
      state.SkipWithError("Failed to write to vmo");
    }
    bytes_processed += buffer.size();
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

  if (zx_vmo_create(state.range(0), 0, &vmo) != ZX_OK) {
    state.SkipWithError("Failed to create vmo");
    return;
  }
  while(state.KeepRunning()) {
    if(zx_vmo_read(vmo, buffer.data(), 0, buffer.size()) != ZX_OK) {
      state.SkipWithError("Failed to read to vmo");
    }
    bytes_processed += buffer.size();
  }
  zx_handle_close(vmo);
  state.SetBytesProcessed(bytes_processed);
}

BENCHMARK_REGISTER_F(Vmo, Read)
    ->Arg(128 * 1024)
    ->Arg(1000 * 1024);
