// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/synchronization/waitable_event.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

#include <errno.h>
#include <time.h>

namespace fxl {

// Waits with a timeout on |condition()|. Returns true on timeout, or false if
// |condition()| ever returns true. |condition()| should have no side effects
// (and will always be called with |*mutex| held).
template <typename ConditionFn>
bool WaitWithTimeoutImpl(Mutex* mutex,
                         CondVar* cv,
                         ConditionFn condition,
                         TimeDelta timeout)
    FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
  mutex->AssertHeld();

  if (condition())
    return false;

  // We may get spurious wakeups.
  TimeDelta wait_remaining = timeout;
  TimePoint start = TimePoint::Now();
  while (true) {
    if (cv->WaitWithTimeout(mutex, wait_remaining))
      return true;  // Definitely timed out.

    // We may have been awoken.
    if (condition())
      return false;

    // Or the wakeup may have been spurious.
    TimePoint now = TimePoint::Now();
    FXL_DCHECK(now >= start);
    TimeDelta elapsed = now - start;
    // It's possible that we may have timed out anyway.
    if (elapsed >= timeout)
      return true;

    // Otherwise, recalculate the amount that we have left to wait.
    wait_remaining = timeout - elapsed;
  }
}

// AutoResetWaitableEvent ------------------------------------------------------

void AutoResetWaitableEvent::Signal() {
  MutexLocker locker(&mutex_);
  signaled_ = true;
  cv_.Signal();
}

void AutoResetWaitableEvent::Reset() {
  MutexLocker locker(&mutex_);
  signaled_ = false;
}

void AutoResetWaitableEvent::Wait() {
  MutexLocker locker(&mutex_);
  while (!signaled_)
    cv_.Wait(&mutex_);
  signaled_ = false;
}

bool AutoResetWaitableEvent::WaitWithTimeout(TimeDelta timeout) {
  MutexLocker locker(&mutex_);

  if (signaled_) {
    signaled_ = false;
    return false;
  }

  // We may get spurious wakeups.
  TimeDelta wait_remaining = timeout;
  TimePoint start = TimePoint::Now();
  while (true) {
    if (cv_.WaitWithTimeout(&mutex_, wait_remaining))
      return true;  // Definitely timed out.

    // We may have been awoken.
    if (signaled_)
      break;

    // Or the wakeup may have been spurious.
    TimePoint now = TimePoint::Now();
    FXL_DCHECK(now >= start);
    TimeDelta elapsed = now - start;
    // It's possible that we may have timed out anyway.
    if (elapsed >= timeout)
      return true;

    // Otherwise, recalculate the amount that we have left to wait.
    wait_remaining = timeout - elapsed;
  }

  signaled_ = false;
  return false;
}

bool AutoResetWaitableEvent::IsSignaledForTest() {
  MutexLocker locker(&mutex_);
  return signaled_;
}

// ManualResetWaitableEvent ----------------------------------------------------

void ManualResetWaitableEvent::Signal() {
  MutexLocker locker(&mutex_);
  signaled_ = true;
  signal_id_++;
  cv_.SignalAll();
}

void ManualResetWaitableEvent::Reset() {
  MutexLocker locker(&mutex_);
  signaled_ = false;
}

void ManualResetWaitableEvent::Wait() {
  MutexLocker locker(&mutex_);

  if (signaled_)
    return;

  auto last_signal_id = signal_id_;
  do {
    cv_.Wait(&mutex_);
  } while (signal_id_ == last_signal_id);
}

bool ManualResetWaitableEvent::WaitWithTimeout(TimeDelta timeout) {
  MutexLocker locker(&mutex_);

  auto last_signal_id = signal_id_;
  // Disable thread-safety analysis for the lambda: We could annotate it with
  // |FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_)|, but then the analyzer currently
  // isn't able to figure out that |WaitWithTimeoutImpl()| calls it while
  // holding |mutex_|.
  bool rv = WaitWithTimeoutImpl(
      &mutex_, &cv_,
      [this, last_signal_id]() FXL_NO_THREAD_SAFETY_ANALYSIS {
        // Also check |signaled_| in case we're already signaled.
        return signaled_ || signal_id_ != last_signal_id;
      },
      timeout);
  FXL_DCHECK(rv || signaled_ || signal_id_ != last_signal_id);
  return rv;
}

bool ManualResetWaitableEvent::IsSignaledForTest() {
  MutexLocker locker(&mutex_);
  return signaled_;
}

}  // namespace fxl
