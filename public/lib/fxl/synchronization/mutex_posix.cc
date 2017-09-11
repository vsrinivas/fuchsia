// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/synchronization/mutex.h"

#ifndef NDEBUG
#include <errno.h>
#include <string.h>

#include "lib/fxl/logging.h"

#define FXL_DCHECK_WITH_ERRNO(condition, fn, error) \
  FXL_DCHECK(condition) << fn << ": " << strerror(error)

namespace fxl {

Mutex::Mutex() {
  pthread_mutexattr_t attr;
  int error = pthread_mutexattr_init(&attr);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_mutexattr_init", error);
  error = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_mutexattr_settype", error);
  error = pthread_mutex_init(&impl_, &attr);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_mutex_init", error);
  error = pthread_mutexattr_destroy(&attr);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_mutexattr_destroy", error);
}

Mutex::~Mutex() {
  int error = pthread_mutex_destroy(&impl_);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_mutex_destroy", error);
}

void Mutex::Lock() FXL_EXCLUSIVE_LOCK_FUNCTION() {
  int error = pthread_mutex_lock(&impl_);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_mutex_lock", error);
}

void Mutex::Unlock() FXL_UNLOCK_FUNCTION() {
  int error = pthread_mutex_unlock(&impl_);
  FXL_DCHECK_WITH_ERRNO(!error, "pthread_mutex_unlock", error);
}

bool Mutex::TryLock() FXL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
  int error = pthread_mutex_trylock(&impl_);
  FXL_DCHECK_WITH_ERRNO(!error || error == EBUSY, "pthread_mutex_trylock",
                        error);
  return !error;
}

void Mutex::AssertHeld() FXL_ASSERT_EXCLUSIVE_LOCK() {
  int error = pthread_mutex_lock(&impl_);
  FXL_DCHECK_WITH_ERRNO(error == EDEADLK, "pthread_mutex_lock", error);
}

}  // namespace fxl

#endif  // NDEBUG
