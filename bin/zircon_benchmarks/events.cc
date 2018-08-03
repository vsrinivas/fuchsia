// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>
#include <perftest/perftest.h>
#include <zircon/assert.h>

namespace {

bool EventSignalTest(perftest::RepeatState* state) {
  zx::event event;
  ZX_ASSERT(zx::event::create(0, &event) == ZX_OK);

  while (state->KeepRunning()) {
    ZX_ASSERT(event.signal(0, 0) == ZX_OK);
  }
  return true;
}

bool EventDuplicateTest(perftest::RepeatState* state) {
  state->DeclareStep("duplicate_handle");
  state->DeclareStep("close_handle");

  zx::event event;
  ZX_ASSERT(zx::event::create(0, &event) == ZX_OK);

  while (state->KeepRunning()) {
    zx::event dup_event;
    ZX_ASSERT(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_event) == ZX_OK);

    state->NextStep();
    // This step covers the work done by dup_event's destructor.
  }
  return true;
}

bool EventReplaceTest(perftest::RepeatState* state) {
  state->DeclareStep("duplicate_handle");
  state->DeclareStep("replace_handle");
  state->DeclareStep("close_handle");

  zx::event event;
  ZX_ASSERT(zx::event::create(0, &event) == ZX_OK);

  while (state->KeepRunning()) {
    zx::event dup_event;
    ZX_ASSERT(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_event) == ZX_OK);

    state->NextStep();
    zx::event replaced_event;
    ZX_ASSERT(dup_event.replace(ZX_RIGHT_SAME_RIGHTS, &replaced_event) ==
              ZX_OK);

    state->NextStep();
    // This step covers the work done by replaced_event's destructor.
  }
  return true;
}

bool WaitForAlreadySignaledEventTest(perftest::RepeatState* state) {
  zx::event event;
  ZX_ASSERT(zx::event::create(0, &event) == ZX_OK);
  ZX_ASSERT(event.signal(0, ZX_EVENT_SIGNALED) == ZX_OK);

  while (state->KeepRunning()) {
    zx_signals_t pending = 0;
    ZX_ASSERT(event.wait_one(ZX_EVENT_SIGNALED, zx::time(0), &pending) ==
              ZX_OK);
  }
  return true;
}

bool WaitForManyWithAlreadySignaledEventTest(perftest::RepeatState* state) {
  constexpr size_t kNumItems = 4;
  zx::event events[kNumItems];
  zx_wait_item_t wait_items[kNumItems] = {};
  for (size_t i = 0; i < kNumItems; ++i) {
    ZX_ASSERT(zx::event::create(0, &events[i]) == ZX_OK);
    wait_items[i].handle = events[i].get();
    wait_items[i].waitfor = ZX_EVENT_SIGNALED;
  }
  ZX_ASSERT(events[0].signal(0, ZX_EVENT_SIGNALED) == ZX_OK);

  while (state->KeepRunning()) {
    ZX_ASSERT(zx::event::wait_many(wait_items, kNumItems, zx::time(0)) ==
              ZX_OK);
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("Event/Signal", EventSignalTest);
  perftest::RegisterTest("Event/Duplicate", EventDuplicateTest);
  perftest::RegisterTest("Event/Replace", EventReplaceTest);
  perftest::RegisterTest("Event/WaitForAlreadySignaledEvent",
                         WaitForAlreadySignaledEventTest);
  perftest::RegisterTest("Event/WaitForManyWithAlreadySignaledEvent",
                         WaitForManyWithAlreadySignaledEventTest);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
