// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/synchronization/mutex.h"

#ifndef NDEBUG

#include "lib/fxl/logging.h"

namespace fxl {

Mutex::Mutex() : impl_(SRWLOCK_INIT) {}

Mutex::~Mutex() {
  FXL_DCHECK(owning_thread_id_ == NULL);
}

void Mutex::Lock() FXL_EXCLUSIVE_LOCK_FUNCTION() {
  AcquireSRWLockExclusive(&impl_);
  CheckUnheldAndMark();
}

void Mutex::Unlock() FXL_UNLOCK_FUNCTION() {
  CheckHeldAndUnmark();
  ReleaseSRWLockExclusive(&impl_);
}

bool Mutex::TryLock() FXL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
  if (TryAcquireSRWLockExclusive(&impl_) != 0) {
    CheckUnheldAndMark();
    return true;
  }
  return false;
}

void Mutex::AssertHeld() FXL_ASSERT_EXCLUSIVE_LOCK() {
  FXL_DCHECK(owning_thread_id_ == GetCurrentThreadId()) << "pthread_mutex_lock";
}

void Mutex::CheckHeldAndUnmark() {
  FXL_DCHECK(owning_thread_id_ == GetCurrentThreadId());
  owning_thread_id_ = NULL;
}

void Mutex::CheckUnheldAndMark() {
  FXL_DCHECK(owning_thread_id_ == NULL);
  owning_thread_id_ = GetCurrentThreadId();
}

}  // namespace fxl

#endif  // NDEBUG
