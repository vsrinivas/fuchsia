// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>
#include <zircon/assert.h>

#include <perftest/perftest.h>

#include "assert.h"

namespace {

bool EventSignalTest(perftest::RepeatState* state) {
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  while (state->KeepRunning()) {
    ASSERT_OK(event.signal(0, 0));
  }
  return true;
}

bool EventDuplicateTest(perftest::RepeatState* state) {
  state->DeclareStep("duplicate_handle");
  state->DeclareStep("close_handle");

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  while (state->KeepRunning()) {
    zx::event dup_event;
    ASSERT_OK(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_event));

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
  ASSERT_OK(zx::event::create(0, &event));

  while (state->KeepRunning()) {
    zx::event dup_event;
    ASSERT_OK(event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_event));

    state->NextStep();
    zx::event replaced_event;
    ASSERT_OK(dup_event.replace(ZX_RIGHT_SAME_RIGHTS, &replaced_event));

    state->NextStep();
    // This step covers the work done by replaced_event's destructor.
  }
  return true;
}

bool WaitForAlreadySignaledEventTest(perftest::RepeatState* state) {
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));
  ASSERT_OK(event.signal(0, ZX_EVENT_SIGNALED));

  while (state->KeepRunning()) {
    zx_signals_t pending = 0;
    ASSERT_OK(event.wait_one(ZX_EVENT_SIGNALED, zx::time(0), &pending));
  }
  return true;
}

bool WaitForManyWithAlreadySignaledEventTest(perftest::RepeatState* state) {
  constexpr size_t kNumItems = 4;
  zx::event events[kNumItems];
  zx_wait_item_t wait_items[kNumItems] = {};
  for (size_t i = 0; i < kNumItems; ++i) {
    ASSERT_OK(zx::event::create(0, &events[i]));
    wait_items[i].handle = events[i].get();
    wait_items[i].waitfor = ZX_EVENT_SIGNALED;
  }
  ASSERT_OK(events[0].signal(0, ZX_EVENT_SIGNALED));

  while (state->KeepRunning()) {
    ASSERT_OK(zx::event::wait_many(wait_items, kNumItems, zx::time(0)));
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("Event/Signal", EventSignalTest);
  perftest::RegisterTest("Event/Duplicate", EventDuplicateTest);
  perftest::RegisterTest("Event/Replace", EventReplaceTest);
  perftest::RegisterTest("Event/WaitForAlreadySignaledEvent", WaitForAlreadySignaledEventTest);
  perftest::RegisterTest("Event/WaitForManyWithAlreadySignaledEvent",
                         WaitForManyWithAlreadySignaledEventTest);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace
