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

constexpr int MULTI_PROCESS_WRITE_BATCH_SIZE = 10000;

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

BENCHMARK_F(Channel, Create)(benchmark::State& state) {
  while (state.KeepRunning()) {
    state.PauseTiming();
    zx_handle_close(in);
    zx_handle_close(out);
    state.ResumeTiming();
    if (zx_channel_create(0, &in, &out) != ZX_OK) {
      state.SkipWithError("Failed to create channel");
      return;
    }
  }
}

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

static bool Launch(const char* arg, int range, zx_handle_t* channel,
                   zx_handle_t* process) {
  std::string optarg = std::to_string(range);
  std::vector<const char*> argv = {HELPER_PATH, arg, optarg.c_str()};
  uint32_t handle_id = HELPER_HANDLE_ID;

  launchpad_t* lp;
  launchpad_create(0, argv[0], &lp);
  launchpad_load_from_file(lp, argv[0]);
  launchpad_set_args(lp, argv.size(), argv.data());
  launchpad_add_handle(lp, *channel, handle_id);
  const char* errmsg;
  auto status = launchpad_go(lp, process, &errmsg);
  *channel = ZX_HANDLE_INVALID;
  return status == ZX_OK;
}

int channel_read(uint32_t num_bytes) {
  zx_handle_t channel = zx_get_startup_handle(HELPER_HANDLE_ID);
  if (channel == ZX_HANDLE_INVALID) {
    return -1;
  }

  std::vector<char> buffer(num_bytes);
  zx_signals_t signals;
  zx_status_t status;
  uint32_t bytes_read;
  do {
    status = zx_object_wait_one(channel,
                                ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                ZX_TIME_INFINITE, &signals);
    if (status != ZX_OK) {
      return -1;
    } else if (signals & ZX_CHANNEL_PEER_CLOSED) {
      return 0;
    }
    status = zx_channel_read(channel, 0, buffer.data(), nullptr,
                             buffer.size(), 0, &bytes_read, nullptr);
  } while(status == ZX_OK && bytes_read == num_bytes);
  return -1;
}

int channel_write(uint32_t num_bytes) {
  zx_handle_t channel = zx_get_startup_handle(HELPER_HANDLE_ID);
  if (channel == ZX_HANDLE_INVALID) {
    return -1;
  }

  std::vector<char> buffer(num_bytes);
  zx_signals_t signals;
  zx_status_t status;
  do {
    for (int i = 0; i < MULTI_PROCESS_WRITE_BATCH_SIZE; i++) {
      status = zx_channel_write(channel, 0, buffer.data(), buffer.size(),
                                nullptr, 0);
      if (status != ZX_OK) {
        return -1;
      }
    }
    status = zx_object_wait_one(channel,
                                ZX_USER_SIGNAL_0 | ZX_CHANNEL_PEER_CLOSED,
                                ZX_TIME_INFINITE, &signals);
    if (status != ZX_OK) {
      return -1;
    } else if (signals & ZX_CHANNEL_PEER_CLOSED) {
      return 0;
    }
    status = zx_object_signal(channel, ZX_USER_SIGNAL_0, 0);
  } while(status == ZX_OK);
  return -1;
}

class ChannelMultiProcess : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    if (zx_channel_create(0, &channel, &channel_for_process) != ZX_OK) {
      state.SkipWithError("Failed to create channel");
    }
  }

  void TearDown(benchmark::State& state) override {
    zx_handle_close(channel);
    if (channel_for_process != ZX_HANDLE_INVALID) {
      zx_handle_close(channel_for_process);
    }
    if (process < 0) {
      return;
    }
    zx_status_t status = zx_object_wait_one(process, ZX_PROCESS_TERMINATED,
                                            ZX_TIME_INFINITE, nullptr);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to wait for process termination");
    }
  }

 protected:
  zx_handle_t channel;
  zx_handle_t channel_for_process;
  zx_handle_t process = ZX_HANDLE_INVALID;
};

BENCHMARK_DEFINE_F(ChannelMultiProcess, Write)(benchmark::State& state) {
  if (!Launch("--channel_read", state.range(0), &channel_for_process,
              &process)) {
    state.SkipWithError("Failed to launch process");
    return;
  }
  std::vector<char> buffer(state.range(0));
  zx_status_t status;
  while (state.KeepRunning()) {
    status = zx_channel_write(channel, 0, buffer.data(), buffer.size(), nullptr,
                              0);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to write to channel");
      return;
    }
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(ChannelMultiProcess, Write)
    ->Arg(64)
    ->Arg(1 * 1024)
    ->Arg(32 * 1024)
    ->Arg(64 * 1024);

BENCHMARK_DEFINE_F(ChannelMultiProcess, Read)(benchmark::State& state) {
  if (!Launch("--channel_write", state.range(0), &channel_for_process,
              &process)) {
    state.SkipWithError("Failed to launch process");
    return;
  }
  std::vector<char> buffer(state.range(0));
  zx_status_t status;
  uint64_t bytes_processed = 0;
  while (state.KeepRunning()) {
    state.PauseTiming();
    int iteration = state.iterations() - 1;  // state.iterations starts at 1.
    if (iteration > 0 && (iteration % MULTI_PROCESS_WRITE_BATCH_SIZE == 0)) {
      // Signal the process to continue writing.
      zx_object_signal_peer(channel, 0, ZX_USER_SIGNAL_0);
    }
    status = zx_object_wait_one(channel, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE,
                                nullptr);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to wait for channel to be readable");
      return;
    }
    state.ResumeTiming();

    uint32_t bytes_read;
    status = zx_channel_read(channel, 0, buffer.data(), nullptr,
                             buffer.size(), 0, &bytes_read, nullptr);
    if (status != ZX_OK) {
      state.SkipWithError("Failed to read from channel");
      return;
    }
    bytes_processed += bytes_read;
  }
  state.SetBytesProcessed(bytes_processed);
}
BENCHMARK_REGISTER_F(ChannelMultiProcess, Read)
    ->Arg(64)
    ->Arg(1 * 1024)
    ->Arg(32 * 1024)
    ->Arg(64 * 1024);
