// Copyright 2017 The Fuchisa Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/synchronization/mutex.h"

#ifndef NDEBUG

#include "lib/ftl/logging.h"

namespace ftl {

Mutex::Mutex() : impl_(SRWLOCK_INIT) {}

Mutex::~Mutex() {
  FTL_DCHECK(owning_thread_id_ == NULL);
}

void Mutex::Lock() FTL_EXCLUSIVE_LOCK_FUNCTION() {
  AcquireSRWLockExclusive(&impl_);
  CheckUnheldAndMark();
}

void Mutex::Unlock() FTL_UNLOCK_FUNCTION() {
  CheckHeldAndUnmark();
  ReleaseSRWLockExclusive(&impl_);
}

bool Mutex::TryLock() FTL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
  if (TryAcquireSRWLockExclusive(&impl_) != 0) {
    CheckUnheldAndMark();
    return true;
  }
  return false;
}

void Mutex::AssertHeld() FTL_ASSERT_EXCLUSIVE_LOCK() {
  FTL_DCHECK(owning_thread_id_ == GetCurrentThreadId()) << "pthread_mutex_lock";
}

void Mutex::CheckHeldAndUnmark() {
  FTL_DCHECK(owning_thread_id_ == GetCurrentThreadId());
  owning_thread_id_ = NULL;
}

void Mutex::CheckUnheldAndMark() {
  FTL_DCHECK(owning_thread_id_ == NULL);
  owning_thread_id_ = GetCurrentThreadId();
}

}  // namespace ftl

#endif  // NDEBUG
