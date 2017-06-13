// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <magenta/syscalls.h>

class Socket : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    if (mx_socket_create(0, &in, &out) != MX_OK) {
      state.SkipWithError("Failed to create socket");
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

BENCHMARK_DEFINE_F(Socket, Write)(benchmark::State& state) {
  std::vector<char> buffer(state.range(0));
  mx_status_t status;
  uint64_t bytes_processed = 0;
  while (state.KeepRunning()) {
    size_t bytes_written;
    status = mx_socket_write(
        in, 0, buffer.data(), buffer.size(), &bytes_written);
    if (status != MX_OK) {
      state.SkipWithError("Failed to write to socket");
      return;
    }
    bytes_processed += bytes_written;

    // Make sure we drain the socket.
    state.PauseTiming();
    size_t bytes_read;
    status = mx_socket_read(out, 0, buffer.data(), buffer.size(), &bytes_read);
    if (status != MX_OK) {
      state.SkipWithError("Failed to read from socket");
      return;
    }
    if (bytes_read != buffer.size()) {
      state.SkipWithError("Did not read all data from socket");
      return;
    }
    state.ResumeTiming();
  }
  state.SetBytesProcessed(bytes_processed);
}
BENCHMARK_REGISTER_F(Socket, Write)
    ->Arg(64)
    ->Arg(1 * 1024)
    ->Arg(32 * 1024)
    ->Arg(64 * 1024);

BENCHMARK_DEFINE_F(Socket, Read)(benchmark::State& state) {
  std::vector<char> buffer(state.range(0));
  mx_status_t status;
  uint64_t bytes_processed = 0;
  while (state.KeepRunning()) {
    // Write the data to read into the socket.
    state.PauseTiming();
    size_t bytes_written;
    status = mx_socket_write(
        in, 0, buffer.data(), buffer.size(), &bytes_written);
    if (status != MX_OK) {
      state.SkipWithError("Failed to write to socket");
      return;
    }
    state.ResumeTiming();

    size_t bytes_read;
    status = mx_socket_read(out, 0, buffer.data(), buffer.size(), &bytes_read);
    if (status != MX_OK) {
      state.SkipWithError("Failed to read from socket");
      return;
    }
    if (bytes_read != buffer.size()) {
      state.SkipWithError("Did not read all data from socket");
      return;
    }
    bytes_processed += bytes_read;
  }
  state.SetBytesProcessed(bytes_processed);
}
BENCHMARK_REGISTER_F(Socket, Read)
    ->Arg(64)
    ->Arg(1 * 1024)
    ->Arg(32 * 1024)
    ->Arg(64 * 1024);
