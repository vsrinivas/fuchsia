// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "abs_clock/clock.h"

#include <lib/sync/completion.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/object.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <memory>
#include <queue>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

namespace abs_clock {

struct SleepingThread {
  zx::time wake_time;               // Time to wake this thread.
  sync_completion_t* notification;  // Notified when thread should wake.

  bool operator>(const SleepingThread& other) const { return wake_time > other.wake_time; }
};

struct FakeClockState {
  fbl::Mutex mutex;
  zx::time current_time __TA_GUARDED(mutex);
  std::priority_queue<SleepingThread, std::vector<SleepingThread>, std::greater<SleepingThread>>
      sleeping_threads __TA_GUARDED(mutex);
};

FakeClock::FakeClock() : FakeClock(zx::time(0)) {}

FakeClock::FakeClock(zx::time start_time) : state_(std::make_unique<FakeClockState>()) {
  state_->current_time = start_time;
}

FakeClock::~FakeClock() {
  fbl::AutoLock l(&state_->mutex);

  // Wake all remaining threads.
  while (!state_->sleeping_threads.empty()) {
    const SleepingThread& top = state_->sleeping_threads.top();
    sync_completion_signal(top.notification);
    state_->sleeping_threads.pop();
  }
}

void FakeClock::AdvanceTime(zx::duration duration) {
  fbl::AutoLock l(&state_->mutex);

  // Advance time.
  state_->current_time += duration;

  // Wake all threads due to be woken.
  while (!state_->sleeping_threads.empty()) {
    const SleepingThread& top = state_->sleeping_threads.top();
    if (top.wake_time > state_->current_time) {
      break;
    }
    sync_completion_signal(top.notification);
    state_->sleeping_threads.pop();
  }
}

zx::time FakeClock::Now() {
  fbl::AutoLock l(&state_->mutex);
  return state_->current_time;
}

void FakeClock::SleepUntil(zx::time deadline) {
  sync_completion_t notification;

  {
    fbl::AutoLock l(&state_->mutex);

    // If the time has already passed, we have nothing to do.
    if (state_->current_time >= deadline) {
      return;
    }

    // Otherwise, go to sleep.
    state_->sleeping_threads.push({deadline, &notification});
  }

  sync_completion_wait(&notification, ZX_TIME_INFINITE);
}

}  // namespace abs_clock
