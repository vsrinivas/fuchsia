// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <lib/zx/event.h>
#include <zircon/assert.h>
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

class WaitItems : public benchmark::Fixture {};

BENCHMARK_F(WaitItems, WaitForAlreadySignaledEvent)(benchmark::State& state) {
  zx::event event;
  ZX_ASSERT(zx::event::create(0, &event) == ZX_OK);
  ZX_ASSERT(event.signal(0, ZX_EVENT_SIGNALED) == ZX_OK);

  while (state.KeepRunning()) {
    zx_signals_t pending = 0;
    ZX_ASSERT(event.wait_one(ZX_EVENT_SIGNALED, zx::time(0), &pending)
              == ZX_OK);
  }
}

BENCHMARK_F(WaitItems, WaitForManyWithAlreadySignaledEvent)(benchmark::State& state) {
  constexpr size_t kNumItems = 4;
  zx::event events[kNumItems];
  zx_wait_item_t wait_items[kNumItems] = {};
  for (size_t i = 0; i < kNumItems; ++i) {
    ZX_ASSERT(zx::event::create(0, &events[i]) == ZX_OK);
    wait_items[i].handle = events[i].get();
    wait_items[i].waitfor = ZX_EVENT_SIGNALED;
  }
  ZX_ASSERT(events[0].signal(0, ZX_EVENT_SIGNALED) == ZX_OK);

  while (state.KeepRunning()) {
    ZX_ASSERT(zx::event::wait_many(wait_items, kNumItems, zx::time(0))
              == ZX_OK);
  }
}
