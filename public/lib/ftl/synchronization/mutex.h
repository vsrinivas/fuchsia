// Copyright 2016 The Fuchisa Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A mutex class, with support for thread annotations.
//
// TODO(vtl): Add support for non-exclusive (reader) locks.

#ifndef LIB_FTL_SYNCHRONIZATION_MUTEX_H_
#define LIB_FTL_SYNCHRONIZATION_MUTEX_H_

#include "lib/ftl/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "lib/ftl/macros.h"
#include "lib/ftl/synchronization/thread_annotations.h"

namespace ftl {

// Mutex -----------------------------------------------------------------------

class CondVar;

class FTL_LOCKABLE Mutex final {
 public:
#ifndef NDEBUG
  Mutex();
  ~Mutex();

  void Lock() FTL_EXCLUSIVE_LOCK_FUNCTION();
  void Unlock() FTL_UNLOCK_FUNCTION();

  bool TryLock() FTL_EXCLUSIVE_TRYLOCK_FUNCTION(true);

  void AssertHeld() FTL_ASSERT_EXCLUSIVE_LOCK();
#elif defined(OS_WIN)
 Mutex() : impl_(SRWLOCK_INIT) {}
 ~Mutex() = default;

  void Lock() FTL_EXCLUSIVE_LOCK_FUNCTION() { AcquireSRWLockExclusive(&impl_); }

  void Unlock() FTL_UNLOCK_FUNCTION() { ReleaseSRWLockExclusive(&impl_); }

  bool TryLock() FTL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
    return (TryAcquireSRWLockExclusive(&impl_) != 0);
  }

  void AssertHeld() FTL_ASSERT_EXCLUSIVE_LOCK() {}
#else
  Mutex() { pthread_mutex_init(&impl_, nullptr); }
  ~Mutex() { pthread_mutex_destroy(&impl_); }

  // Takes an exclusive lock.
  void Lock() FTL_EXCLUSIVE_LOCK_FUNCTION() { pthread_mutex_lock(&impl_); }

  // Releases a lock.
  void Unlock() FTL_UNLOCK_FUNCTION() { pthread_mutex_unlock(&impl_); }

  // Tries to take an exclusive lock, returning true if successful.
  bool TryLock() FTL_EXCLUSIVE_TRYLOCK_FUNCTION(true) {
    return !pthread_mutex_trylock(&impl_);
  }

  // Asserts that an exclusive lock is held by the calling thread. (Does nothing
  // for non-Debug builds.)
  void AssertHeld() FTL_ASSERT_EXCLUSIVE_LOCK() {}
#endif  // NDEBUG

 private:
  friend class CondVar;

#if defined(OS_WIN)
  SRWLOCK impl_;
#ifndef NDEBUG
  void CheckHeldAndUnmark();
  void CheckUnheldAndMark();
  DWORD owning_thread_id_ = NULL;
#endif //  NDEBUG
#else
  pthread_mutex_t impl_;
#endif //  defined(OS_WIN)

  FTL_DISALLOW_COPY_AND_ASSIGN(Mutex);
};

// MutexLocker -----------------------------------------------------------------

class FTL_SCOPED_LOCKABLE MutexLocker final {
 public:
  explicit MutexLocker(Mutex* mutex) FTL_EXCLUSIVE_LOCK_FUNCTION(mutex)
      : mutex_(mutex) {
    mutex_->Lock();
  }
  ~MutexLocker() FTL_UNLOCK_FUNCTION() { mutex_->Unlock(); }

 private:
  Mutex* const mutex_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MutexLocker);
};

}  // namespace ftl

#endif  // LIB_FTL_SYNCHRONIZATION_MUTEX_H_
