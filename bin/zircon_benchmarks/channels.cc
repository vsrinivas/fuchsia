// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <benchmark/benchmark.h>
#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>

#include "channels.h"

class Channel : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    if (mx_channel_create(0, &in, &out) != NO_ERROR) {
      state.SkipWithError("Failed to create channel");
    }
  }

  void TearDown(benchmark::State& state) override {
    mx_handle_close(in);
    mx_handle_close(out);
  }

 protected:
  mx_handle_t in;
  mx_handle_t out;
};

BENCHMARK_F(Channel, Create)(benchmark::State& state) {
  while (state.KeepRunning()) {
    state.PauseTiming();
    mx_handle_close(in);
    mx_handle_close(out);
    state.ResumeTiming();
    if (mx_channel_create(0, &in, &out) != NO_ERROR) {
      state.SkipWithError("Failed to create channel");
      return;
    }
  }
}

BENCHMARK_DEFINE_F(Channel, Write)(benchmark::State& state) {
  std::vector<char> buffer(state.range(0));
  mx_status_t status;
  while (state.KeepRunning()) {
    status = mx_channel_write(in, 0, buffer.data(), buffer.size(), nullptr, 0);
    if (status != NO_ERROR) {
      state.SkipWithError("Failed to write to channel");
      return;
    }

    // Make sure we drain the channel.
    state.PauseTiming();
    status = mx_channel_read(out, 0, buffer.data(), nullptr,
                             buffer.size(), 0, nullptr, nullptr);
    if (status != NO_ERROR) {
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
  mx_status_t status;
  uint64_t bytes_processed = 0;
  while (state.KeepRunning()) {
    // Write the data to read into the channel.
    state.PauseTiming();
    status = mx_channel_write(in, 0, buffer.data(), buffer.size(), nullptr, 0);
    if (status != NO_ERROR) {
      state.SkipWithError("Failed to write to channel");
      return;
    }
    state.ResumeTiming();

    uint32_t bytes_read;
    status = mx_channel_read(out, 0, buffer.data(), nullptr,
                             buffer.size(), 0, &bytes_read, nullptr);
    if (status != NO_ERROR) {
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

static bool Launch(const char* arg, int range, mx_handle_t* channel,
                   mx_handle_t* process) {
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
  *channel = MX_HANDLE_INVALID;
  return status == NO_ERROR;
}

int channel_read(uint32_t num_bytes) {
  mx_handle_t channel = mx_get_startup_handle(HELPER_HANDLE_ID);
  if (channel == MX_HANDLE_INVALID) {
    return -1;
  }

  std::vector<char> buffer(num_bytes);
  mx_signals_t signals;
  mx_status_t status;
  uint32_t bytes_read;
  do {
    status = mx_object_wait_one(channel,
                                MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                MX_TIME_INFINITE, &signals);
    if (status != NO_ERROR) {
      return -1;
    } else if (signals & MX_CHANNEL_PEER_CLOSED) {
      return 0;
    }
    status = mx_channel_read(channel, 0, buffer.data(), nullptr,
                             buffer.size(), 0, &bytes_read, nullptr);
  } while(status == NO_ERROR && bytes_read == num_bytes);
  return -1;
}

int channel_write(uint32_t num_bytes) {
  mx_handle_t channel = mx_get_startup_handle(HELPER_HANDLE_ID);
  if (channel == MX_HANDLE_INVALID) {
    return -1;
  }

  std::vector<char> buffer(num_bytes);
  mx_status_t status;
  do {
    status = mx_channel_write(channel, 0, buffer.data(), buffer.size(), nullptr,
                              0);
  } while(status == NO_ERROR);
  return -1;
}

class ChannelMultiProcess : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    if (mx_channel_create(0, &channel, &channel_for_process) != NO_ERROR) {
      state.SkipWithError("Failed to create channel");
    }
  }

  void TearDown(benchmark::State& state) override {
    mx_handle_close(channel);
    if (channel_for_process != MX_HANDLE_INVALID) {
      mx_handle_close(channel_for_process);
    }
    if (process < 0) {
      return;
    }
    mx_status_t status = mx_object_wait_one(process, MX_PROCESS_SIGNALED,
                                            MX_TIME_INFINITE, nullptr);
    if (status != NO_ERROR) {
      state.SkipWithError("Failed to wait for process termination");
    }
  }

 protected:
  mx_handle_t channel;
  mx_handle_t channel_for_process;
  mx_handle_t process = MX_HANDLE_INVALID;
};

BENCHMARK_DEFINE_F(ChannelMultiProcess, Write)(benchmark::State& state) {
  if (!Launch("--channel_read", state.range(0), &channel_for_process,
              &process)) {
    state.SkipWithError("Failed to launch process");
    return;
  }
  std::vector<char> buffer(state.range(0));
  mx_status_t status;
  while (state.KeepRunning()) {
    status = mx_channel_write(channel, 0, buffer.data(), buffer.size(), nullptr,
                              0);
    if (status != NO_ERROR) {
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
  mx_status_t status;
  uint64_t bytes_processed = 0;
  while (state.KeepRunning()) {
    state.PauseTiming();
    status = mx_object_wait_one(channel, MX_CHANNEL_READABLE, MX_TIME_INFINITE,
                                nullptr);
    if (status != NO_ERROR) {
      state.SkipWithError("Failed to wait for channel to be readable");
      return;
    }
    state.ResumeTiming();

    uint32_t bytes_read;
    status = mx_channel_read(channel, 0, buffer.data(), nullptr,
                             buffer.size(), 0, &bytes_read, nullptr);
    if (status != NO_ERROR) {
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
