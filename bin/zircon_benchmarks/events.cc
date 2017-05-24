// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <magenta/syscalls.h>

class Event : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    if (mx_event_create(0, &event) != NO_ERROR) {
      state.SkipWithError("Failed to create channel");
    }
  }

  void TearDown(benchmark::State& state) override {
    mx_handle_close(event);
  }

 protected:
  mx_handle_t event;
};

BENCHMARK_F(Event, Create)(benchmark::State& state) {
  while (state.KeepRunning()) {
    state.PauseTiming();
    mx_handle_close(event);
    state.ResumeTiming();
    if (mx_event_create(0, &event) != NO_ERROR) {
      state.SkipWithError("Failed to create event");
      return;
    }
  }
}

BENCHMARK_F(Event, Signal)(benchmark::State& state) {
   while (state.KeepRunning()) {
    if (mx_object_signal(event, 0, 0) != 0) {
      state.SkipWithError("Failed to signal event");
      return;
    }
  }
}
