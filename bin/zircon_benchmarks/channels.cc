// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <benchmark/benchmark.h>
#include <launchpad/launchpad.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "channels.h"

class Channel : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    if (zx_channel_create(0, &in, &out) != ZX_OK) {
      state.SkipWithError("Failed to create channel");
    }
  }

  void TearDown(benchmark::State& state) override {
    zx_handle_close(in);
    zx_handle_close(out);
  }

 protected:
  zx_handle_t in;
  zx_handle_t out;
};

BENCHMARK_DEFINE_F(Channel, Write)(benchmark::State& state) {
  std::vector<char> buffer(state.range(0));
  zx_status_t status;
  while (state.KeepRunning()) {
    status = zx_channel_write(in, 0, buffer.data(), buffer.size(), nullptr, 0);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to write to channel");
      return;
    }

    // Make sure we drain the channel.
    state.PauseTiming();
    status = zx_channel_read(out, 0, buffer.data(), nullptr,
                             buffer.size(), 0, nullptr, nullptr);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to read from channel");
      return;
    }
    state.ResumeTiming();
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(Channel, Write)
    ->Arg(64)
    ->Arg(1 * 1024)
    ->Arg(32 * 1024)
    ->Arg(64 * 1024);

BENCHMARK_DEFINE_F(Channel, Read)(benchmark::State& state) {
  std::vector<char> buffer(state.range(0));
  zx_status_t status;
  uint64_t bytes_processed = 0;
  while (state.KeepRunning()) {
    // Write the data to read into the channel.
    state.PauseTiming();
    status = zx_channel_write(in, 0, buffer.data(), buffer.size(), nullptr, 0);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to write to channel");
      return;
    }
    state.ResumeTiming();

    uint32_t bytes_read;
    status = zx_channel_read(out, 0, buffer.data(), nullptr,
                             buffer.size(), 0, &bytes_read, nullptr);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to read from channel");
      return;
    }
    bytes_processed += bytes_read;
  }
  state.SetBytesProcessed(bytes_processed);
}
BENCHMARK_REGISTER_F(Channel, Read)
    ->Arg(64)
    ->Arg(1 * 1024)
    ->Arg(32 * 1024)
    ->Arg(64 * 1024);
