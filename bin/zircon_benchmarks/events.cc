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

BENCHMARK_F(Event, Signal)(benchmark::State& state) {
  while (state.KeepRunning()) {
    if (zx_object_signal(event, 0, 0) != 0) {
      state.SkipWithError("Failed to signal event");
      return;
    }
  }
}

BENCHMARK_F(Event, Duplicate)(benchmark::State& state) {
  zx_handle_t dup_event;
  while (state.KeepRunning()) {
    if (zx_handle_duplicate(event, ZX_RIGHT_SAME_RIGHTS, &dup_event) != 0) {
      state.SkipWithError("Failed to duplicate event");
      return;
    }
    state.PauseTiming();
    zx_handle_close(dup_event);
    state.ResumeTiming();
  }
}

BENCHMARK_F(Event, Replace)(benchmark::State& state) {
  zx_handle_t dup_event;
  zx_handle_t replaced_event;
  while (state.KeepRunning()) {
    state.PauseTiming();
    if (zx_handle_duplicate(event, ZX_RIGHT_SAME_RIGHTS, &dup_event) != 0) {
      state.SkipWithError("Failed to duplicate event");
      return;
    }
    state.ResumeTiming();
    if (zx_handle_replace(dup_event, ZX_RIGHT_SAME_RIGHTS, &replaced_event) != 0) {
      zx_handle_close(dup_event);
      state.SkipWithError("Failed to replace event");
      return;
    }
    state.PauseTiming();
    zx_handle_close(replaced_event);
    state.ResumeTiming();
  }
}

class WaitItems : public benchmark::Fixture {
 private:
  void SetUp(benchmark::State& state) override {
    for (size_t i = 0; i < num_items; i++) {
      if (zx_event_create(0, &(items[i].handle)) != 0) {
        state.SkipWithError("Failed to create event");
        return;
      }
      items[i].waitfor = ZX_EVENT_SIGNALED;
    }
  }

  void TearDown(benchmark::State& state) override {
    for (size_t i = 0; i < num_items; i++) {
      zx_handle_close(items[i].handle);
    }
  }

 protected:
  static constexpr size_t num_items = 4u;
  zx_wait_item_t items[num_items];
};

BENCHMARK_F(WaitItems, WaitForAlreadySignaledEvent)(benchmark::State& state) {
  if (zx_object_signal(items[0].handle, 0u, ZX_EVENT_SIGNALED) != 0) {
    state.SkipWithError("Failed to signal event");
    return;
  }
  while (state.KeepRunning()) {
    zx_signals_t pending = 0;
    if (zx_object_wait_one(items[0].handle, ZX_EVENT_SIGNALED, 0u, &pending) != 0) {
      state.SkipWithError("Failed to get signaled event");
      return;
    }
  }
}

BENCHMARK_F(WaitItems, WaitForManyWithAlreadySignaledEvent)(benchmark::State& state) {
  if (zx_object_signal(items[0].handle, 0u, ZX_EVENT_SIGNALED) != 0) {
    state.SkipWithError("Failed to signal event");
    return;
  }
  while (state.KeepRunning()) {
    if (zx_object_wait_many(items, num_items, 0u) != 0) {
      state.SkipWithError("Failed to get signaled event");
      return;
    }
  }
}
