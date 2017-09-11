// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/synchronization/cond_var.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include <limits>

#include "lib/fxl/build_config.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"
#include "lib/fxl/synchronization/mutex.h"

#define FXL_DCHECK_WITH_ERRNO(condition, fn, error) \
  FXL_DCHECK(condition) << fn << ": " << strerror(error)

namespace fxl {
namespace {

// Helper for |CondVar::WaitWithTimeout()|. Returns true on (definite) time-out.
// |timeout_rel| must be small enough to not overflow when added to
// |TimePoint::now()|.
bool RelativeTimedWait(TimeDelta timeout_rel,
                       pthread_cond_t* posix_cond_var,
                       pthread_mutex_t* posix_mutex) {
// Mac has a function to do a relative timed wait directly.
#if defined(OS_MACOSX)
  struct timespec timespec_rel = timeout_rel.ToTimespec();

  int error = pthread_cond_timedwait_relative_np(posix_cond_var, posix_mutex,
                                                 &timespec_rel);
  FXL_DCHECK_WITH_ERRNO(error == 0 || error == ETIMEDOUT || error == EINTR,
                        "pthread_cond_timedwait_relative_np", error);
  return error == ETIMEDOUT;
#else
  TimePoint timeout_abs = TimePoint::Now() + timeout_rel;

  struct timespec timespec_abs = (timeout_abs - TimePoint()).ToTimespec();

  int error;
// Older Android doesn't have |pthread_condattr_setclock()|, but they have
// |pthread_cond_timedwait_monotonic_np()|.
#if defined(OS_ANDROID) && defined(HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC)
  error = pthread_cond_timedwait_monotonic_np(posix_cond_var, posix_mutex,
                                              &timespec_abs);
  FXL_DCHECK_WITH_ERRNO(error == 0 || error == ETIMEDOUT || error == EINTR,
                        "pthread_cond_timedwait_monotonic_np", error);
#else
  error = pthread_cond_timedwait(posix_cond_var, posix_mutex, &timespec_abs);
  FXL_DCHECK_WITH_ERRNO(error == 0 || error == ETIMEDOUT || error == EINTR,
                        "pthread_cond_timedwait", error);
#endif  // defined(OS_ANDROID) && defined(HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC)
  return error == ETIMEDOUT;
#endif  // defined(OS_MACOSX)
}

}  // namespace

CondVar::CondVar() {
// Mac and older Android don't have |pthread_condattr_setclock()| (but they have
// other timed wait functions we can use).
#if !defined(OS_MACOSX) && \
    !(defined(OS_ANDROID) && defined(HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC))
  pthread_condattr_t attr;
  int error = pthread_condattr_init(&attr);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_condattr_init", error);
  error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_condattr_setclock", error);
  error = pthread_cond_init(&impl_, &attr);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_cond_init", error);
  error = pthread_condattr_destroy(&attr);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_condattr_destroy", error);
#else
  int error = pthread_cond_init(&impl_, nullptr);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_cond_init", error);
#endif  // !defined(OS_MACOSX) && !(defined(OS_ANDROID)...)
}

CondVar::~CondVar() {
  int error = pthread_cond_destroy(&impl_);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_cond_destroy", error);
}

void CondVar::Wait(Mutex* mutex) {
  FXL_DCHECK(mutex);
  mutex->AssertHeld();

  int error = pthread_cond_wait(&impl_, &mutex->impl_);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_cond_wait", error);
}

bool CondVar::WaitWithTimeout(Mutex* mutex, TimeDelta timeout) {
  // Turn very long waits into "forever". This isn't a huge concern if |time_t|
  // is 64-bit, but overflowing |time_t| is a real risk if it's only 32-bit.
  // (2^31 / 16 seconds = ~4.25 years, so we won't risk overflowing until 2033.)
  constexpr TimeDelta kForeverThreshold =
      TimeDelta::FromSeconds(std::numeric_limits<int32_t>::max() / 16);
  if (timeout >= kForeverThreshold) {
    Wait(mutex);
    return false;  // Did *not* time out.
  }

  FXL_DCHECK(mutex);
  mutex->AssertHeld();

  return RelativeTimedWait(timeout, &impl_, &mutex->impl_);
}

void CondVar::Signal() {
  int error = pthread_cond_signal(&impl_);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_cond_signal", error);
}

void CondVar::SignalAll() {
  int error = pthread_cond_broadcast(&impl_);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_cond_broadcast", error);
}

}  // namespace fxl
