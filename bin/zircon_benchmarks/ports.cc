// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

class Port : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    if (zx_port_create(0, &port_) != ZX_OK) {
      state.SkipWithError("Failed to create channel");
    }
  }

  void TearDown(benchmark::State& state) override {
    zx_handle_close(port_);
  }

 protected:
  zx_handle_t port_ = ZX_HANDLE_INVALID;
  static constexpr size_t packet_count = 1u;
};

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
    ->Arg(0);

BENCHMARK_F(Port, Queue)(benchmark::State& state) {
  zx_port_packet out_packet;
  zx_port_packet in_packet;
  out_packet.type = ZX_PKT_TYPE_USER;

  while (state.KeepRunning()) {
    if (zx_port_queue(port_, &out_packet, packet_count) != ZX_OK) {
      state.SkipWithError("Failed to queue a packet to a port");
      return;
    }

    // Make sure to drain the queue.
    state.PauseTiming();
    if(zx_port_wait(port_, ZX_TIME_INFINITE, &in_packet, packet_count) != ZX_OK) {
      state.SkipWithError("Failed to wait for a packet at a port");
      return;
    }
    state.ResumeTiming();
  }
}

BENCHMARK_F(Port, Wait)(benchmark::State& state) {
  zx_port_packet out_packet;
  zx_port_packet in_packet;
  out_packet.type = ZX_PKT_TYPE_USER;

  while (state.KeepRunning()) {
    state.PauseTiming();
    if (zx_port_queue(port_, &out_packet, packet_count) != ZX_OK) {
      state.SkipWithError("Failed to queue a packet to a port");
      return;
    }
    state.ResumeTiming();

    if(zx_port_wait(port_, ZX_TIME_INFINITE, &in_packet, packet_count) != ZX_OK) {
      state.SkipWithError("Failed to wait for a packet at a port");
      return;
    }
  }
}
