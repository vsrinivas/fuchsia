// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A mutex class, with support for thread annotations.
//
// TODO(vtl): Add support for non-exclusive (reader) locks.

#ifndef LIB_FXL_SYNCHRONIZATION_MUTEX_H_
#define LIB_FXL_SYNCHRONIZATION_MUTEX_H_

#include "lib/fxl/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace fxl {

// Mutex -----------------------------------------------------------------------

class CondVar;

class FXL_LOCKABLE FXL_EXPORT Mutex final {
 public:
#ifndef NDEBUG
  Mutex();
  ~Mutex();

  void Lock() FXL_EXCLUSIVE_LOCK_FUNCTION();
  void Unlock() FXL_UNLOCK_FUNCTION();

  bool TryLock() FXL_EXCLUSIVE_TRYLOCK_FUNCTION(true);

  void AssertHeld() FXL_ASSERT_EXCLUSIVE_LOCK();
#elif defined(OS_WIN)
  Mutex() : impl_(SRWLOCK_INIT) {}
  ~Mutex() = default;

  void Lock() FXL_EXCLUSIVE_LOCK_FUNCTION() { AcquireSRWLockExclusive(&impl_); }

  void Unlock() FXL_UNLOCK_FUNCTION() { ReleaseSRWLockExclusive(&impl_); }

  bool TryLock() FXL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
    return (TryAcquireSRWLockExclusive(&impl_) != 0);
  }

  void AssertHeld() FXL_ASSERT_EXCLUSIVE_LOCK() {}
#else
  Mutex() { pthread_mutex_init(&impl_, nullptr); }
  ~Mutex() { pthread_mutex_destroy(&impl_); }

  // Takes an exclusive lock.
  void Lock() FXL_EXCLUSIVE_LOCK_FUNCTION() { pthread_mutex_lock(&impl_); }

  // Releases a lock.
  void Unlock() FXL_UNLOCK_FUNCTION() { pthread_mutex_unlock(&impl_); }

  // Tries to take an exclusive lock, returning true if successful.
  bool TryLock() FXL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
    return !pthread_mutex_trylock(&impl_);
  }

  // Asserts that an exclusive lock is held by the calling thread. (Does nothing
  // for non-Debug builds.)
  void AssertHeld() FXL_ASSERT_EXCLUSIVE_LOCK() {}
#endif  // NDEBUG

 private:
  friend class CondVar;

#if defined(OS_WIN)
  SRWLOCK impl_;
#ifndef NDEBUG
  void CheckHeldAndUnmark();
  void CheckUnheldAndMark();
  DWORD owning_thread_id_ = NULL;
#endif  //  NDEBUG
#else
  pthread_mutex_t impl_;
#endif  //  defined(OS_WIN)

  FXL_DISALLOW_COPY_AND_ASSIGN(Mutex);
};

// MutexLocker -----------------------------------------------------------------

class FXL_SCOPED_LOCKABLE MutexLocker final {
 public:
  explicit MutexLocker(Mutex* mutex) FXL_EXCLUSIVE_LOCK_FUNCTION(mutex)
      : mutex_(mutex) {
    mutex_->Lock();
  }
  ~MutexLocker() FXL_UNLOCK_FUNCTION() { mutex_->Unlock(); }

 private:
  Mutex* const mutex_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MutexLocker);
};

}  // namespace fxl

#endif  // LIB_FXL_SYNCHRONIZATION_MUTEX_H_
