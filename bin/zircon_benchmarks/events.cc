// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <lib/zx/event.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

class Event : public benchmark::Fixture {};

BENCHMARK_F(Event, Signal)(benchmark::State& state) {
  zx::event event;
  ZX_ASSERT(zx::event::create(0, &event) == ZX_OK);

  while (state.KeepRunning()) {
    ZX_ASSERT(event.signal(0, 0) == ZX_OK);
  }
}

BENCHMARK_F(Event, Duplicate)(benchmark::State& state) {
  zx::event event;
  ZX_ASSERT(zx::event::create(0, &event) == ZX_OK);

  while (state.KeepRunning()) {
    zx::event dup_event;
    ZX_ASSERT(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_event) == ZX_OK);

    state.PauseTiming();
    dup_event.reset();
    state.ResumeTiming();
  }
}

BENCHMARK_F(Event, Replace)(benchmark::State& state) {
  zx::event event;
  ZX_ASSERT(zx::event::create(0, &event) == ZX_OK);

  while (state.KeepRunning()) {
    state.PauseTiming();
    zx::event dup_event;
    ZX_ASSERT(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_event) == ZX_OK);
    state.ResumeTiming();

    zx::event replaced_event;
    ZX_ASSERT(dup_event.replace(ZX_RIGHT_SAME_RIGHTS, &replaced_event)
              == ZX_OK);

    state.PauseTiming();
    replaced_event.reset();
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
