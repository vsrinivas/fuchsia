// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <zircon/syscalls.h>

class Event : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    if (zx_event_create(0, &event) != ZX_OK) {
      state.SkipWithError("Failed to create channel");
    }
  }

  void TearDown(benchmark::State& state) override {
    zx_handle_close(event);
  }

 protected:
  zx_handle_t event;
};

BENCHMARK_F(Event, Create)(benchmark::State& state) {
  while (state.KeepRunning()) {
    state.PauseTiming();
    zx_handle_close(event);
    state.ResumeTiming();
    if (zx_event_create(0, &event) != ZX_OK) {
      state.SkipWithError("Failed to create event");
      return;
    }
  }
}

BENCHMARK_F(Event, Close)(benchmark::State& state) {
  while (state.KeepRunning()) {
    if (zx_handle_close(event) != ZX_OK) {
      state.SkipWithError("Failed to close event");
      return;
    }
    state.PauseTiming();
    if (zx_event_create(0, &event) != ZX_OK) {
      state.SkipWithError("Failed to create event");
    }
    state.ResumeTiming();
  }
}

BENCHMARK_F(Event, Signal)(benchmark::State& state) {
   while (state.KeepRunning()) {
    if (zx_object_signal(event, 0, 0) != 0) {
      state.SkipWithError("Failed to signal event");
      return;
    }
  }
}

class EventPair : public benchmark::Fixture {};

BENCHMARK_F(EventPair, Create)(benchmark::State& state) {
  zx_handle_t out0;
  zx_handle_t out1;
  while (state.KeepRunning()) {
    if (zx_eventpair_create(0, &out0, &out1) != ZX_OK) {
      state.SkipWithError("Failed to create event pair");
      return;
    }
    state.PauseTiming();
    zx_handle_close(out0);
    zx_handle_close(out1);
    state.ResumeTiming();
  }
}
