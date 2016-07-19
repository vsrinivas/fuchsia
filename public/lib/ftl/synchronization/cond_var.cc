// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/synchronization/cond_var.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include <limits>

#include "lib/ftl/logging.h"
#include "lib/ftl/synchronization/mutex.h"

#define FTL_DCHECK_WITH_ERRNO(condition, fn, error) \
  FTL_DCHECK(condition) << fn << ": " << strerror(error)

namespace ftl {
namespace {

// Helper for |CondVar::WaitWithTimeout()|. Returns true on (definite) time-out.
bool RelativeTimedWait(const struct timespec& timeout_rel,
                       pthread_cond_t* posix_cond_var,
                       pthread_mutex_t* posix_mutex) {
  // Mac has a function to do a relative timed wait directly.
  constexpr long kNanosecondsPerSecond = 1000000000L;
  constexpr clockid_t kClockType = CLOCK_MONOTONIC;

  struct timespec timeout_abs;
  int error = clock_gettime(kClockType, &timeout_abs);
  // Note: The return value of |clock_gettime()| is *not* an error code, unlike
  // the pthreads functions (however, it sets errno).
  FTL_DCHECK_WITH_ERRNO(!error, "clock_gettime", errno);

  timeout_abs.tv_sec += timeout_rel.tv_sec;
  timeout_abs.tv_nsec += timeout_rel.tv_nsec;
  if (timeout_abs.tv_nsec >= kNanosecondsPerSecond) {
    timeout_abs.tv_sec++;
    timeout_abs.tv_nsec -= kNanosecondsPerSecond;
    FTL_DCHECK(timeout_abs.tv_nsec < kNanosecondsPerSecond);
  }

  error = pthread_cond_timedwait(posix_cond_var, posix_mutex, &timeout_abs);
  FTL_DCHECK_WITH_ERRNO(error == 0 || error == ETIMEDOUT || error == EINTR,
                        "pthread_cond_timedwait", error);
  return error == ETIMEDOUT;
}

}  // namespace

CondVar::CondVar() {
  pthread_condattr_t attr;
  int error = pthread_condattr_init(&attr);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_condattr_init", error);
  error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_condattr_setclock", error);
  error = pthread_cond_init(&impl_, &attr);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_cond_init", error);
  error = pthread_condattr_destroy(&attr);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_condattr_destroy", error);
}

CondVar::~CondVar() {
  int error = pthread_cond_destroy(&impl_);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_cond_destroy", error);
}

void CondVar::Wait(Mutex* mutex) {
  FTL_DCHECK(mutex);
  mutex->AssertHeld();

  int error = pthread_cond_wait(&impl_, &mutex->impl_);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_cond_wait", error);
}

bool CondVar::WaitWithTimeout(Mutex* mutex, Duration timeout) {
  constexpr uint64_t kNanosecondsPerSecond = 1000000000ULL;

  // Turn very long waits into "forever". This isn't a huge concern if |time_t|
  // is 64-bit, but overflowing |time_t| is a real risk if it's only 32-bit.
  // (2^31 / 16 seconds = ~4.25 years, so we won't risk overflowing until 2033.)
  constexpr std::chrono::seconds kForeverThresholdSeconds(
      std::numeric_limits<time_t>::max() / 16);
  std::chrono::seconds timeout_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(timeout);
  if (timeout_seconds >= kForeverThresholdSeconds) {
    Wait(mutex);
    return false;  // Did *not* time out.
  }

  FTL_DCHECK(mutex);
  mutex->AssertHeld();

  struct timespec timeout_rel = {};
  timeout_rel.tv_sec = static_cast<time_t>(timeout_seconds.count());
  timeout_rel.tv_nsec =
      static_cast<long>(timeout.count() % kNanosecondsPerSecond);
  return RelativeTimedWait(timeout_rel, &impl_, &mutex->impl_);
}

void CondVar::Signal() {
  int error = pthread_cond_signal(&impl_);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_cond_signal", error);
}

void CondVar::SignalAll() {
  int error = pthread_cond_broadcast(&impl_);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_cond_broadcast", error);
}

}  // namespace ftl
