// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/synchronization/cond_var.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/mutex.h"
#include "lib/fxl/time/time_point.h"

namespace fxl {

CondVar::CondVar() {
  InitializeConditionVariable(&cv_);
}

CondVar::~CondVar() = default;

void CondVar::Wait(Mutex* mutex) {
  WaitWithTimeout(mutex, TimeDelta::FromMilliseconds(INFINITE));
}

bool CondVar::WaitWithTimeout(Mutex* mutex, TimeDelta timeout) {
  int64_t duration = timeout.ToMilliseconds();
  bool timed_out = false;
#ifndef NDEBUG
  mutex->CheckHeldAndUnmark();
#endif
  if (!SleepConditionVariableSRW(&cv_, &(mutex->impl_), duration, 0)) {
    // On failure, we only expect the CV to timeout. Any other error value means
    // that we've unexpectedly woken up.
    // Note that WAIT_TIMEOUT != ERROR_TIMEOUT. WAIT_TIMEOUT is used with the
    // WaitFor* family of functions as a direct return value. ERROR_TIMEOUT is
    // used with GetLastError().
    FXL_DCHECK(static_cast<DWORD>(ERROR_TIMEOUT) == GetLastError());
    timed_out = true;
  }
#ifndef NDEBUG
  mutex->CheckUnheldAndMark();
#endif
  return timed_out;
}

void CondVar::Signal() {
  WakeConditionVariable(&cv_);
}

void CondVar::SignalAll() {
  WakeAllConditionVariable(&cv_);
}
}  // namespace fxl
