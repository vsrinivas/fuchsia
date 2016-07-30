// Copyright 2016 The Fuchisa Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/synchronization/mutex.h"

#ifndef NDEBUG
#include <errno.h>
#include <string.h>

#include "lib/ftl/logging.h"

#define FTL_DCHECK_WITH_ERRNO(condition, fn, error) \
  FTL_DCHECK(condition) << fn << ": " << strerror(error)

namespace ftl {

Mutex::Mutex() {
  pthread_mutexattr_t attr;
  int error = pthread_mutexattr_init(&attr);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_mutexattr_init", error);
  error = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_mutexattr_settype", error);
  error = pthread_mutex_init(&impl_, &attr);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_mutex_init", error);
  error = pthread_mutexattr_destroy(&attr);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_mutexattr_destroy", error);
}

Mutex::~Mutex() {
  int error = pthread_mutex_destroy(&impl_);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_mutex_destroy", error);
}

void Mutex::Lock() FTL_EXCLUSIVE_LOCK_FUNCTION() {
  int error = pthread_mutex_lock(&impl_);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_mutex_lock", error);
}

void Mutex::Unlock() FTL_UNLOCK_FUNCTION() {
  int error = pthread_mutex_unlock(&impl_);
  FTL_DCHECK_WITH_ERRNO(!error, "pthread_mutex_unlock", error);
}

bool Mutex::TryLock() FTL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
  int error = pthread_mutex_trylock(&impl_);
  FTL_DCHECK_WITH_ERRNO(!error || error == EBUSY, "pthread_mutex_trylock",
                        error);
  return !error;
}

void Mutex::AssertHeld() FTL_ASSERT_EXCLUSIVE_LOCK() {
  int error = pthread_mutex_lock(&impl_);
  FTL_DCHECK_WITH_ERRNO(error == EDEADLK, "pthread_mutex_lock", error);
}

}  // namespace ftl

#endif  // NDEBUG
