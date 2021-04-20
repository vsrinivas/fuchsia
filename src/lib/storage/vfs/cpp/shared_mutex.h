// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_SHARED_MUTEX_H_
#define SRC_LIB_STORAGE_VFS_CPP_SHARED_MUTEX_H_

#include <zircon/compiler.h>

#include <shared_mutex>

namespace fs {

// Provides a wrapper around std::shared_mutex and std::shared_lock that has thread safety analysis
// annotations. The current implementations do not have these so breaks thread safety analysis. This
// wrapper contains only the parts of the API we need.
//
// TODO: this can be removed and callers replaced with std::shared_mutex and std::shared_lock if we
// update to an implementation that is annotated with thread capabilities properly.
class __TA_CAPABILITY("shared_mutex") SharedMutex {
 public:
  SharedMutex() = default;
  SharedMutex(const SharedMutex&) = delete;

  // Exclusive locking
  void lock() __TA_ACQUIRE() { mutex_.lock(); }
  void unlock() __TA_RELEASE() { mutex_.unlock(); }

  // Shared locking.
  void lock_shared() __TA_ACQUIRE_SHARED() { mutex_.lock_shared(); }
  void unlock_shared() __TA_RELEASE_SHARED() { mutex_.unlock_shared(); }

 private:
  std::shared_mutex mutex_;
};

class __TA_SCOPED_CAPABILITY SharedLock {
 public:
  __WARN_UNUSED_CONSTRUCTOR explicit SharedLock(SharedMutex& m) __TA_ACQUIRE_SHARED(m) : lock_(m) {}
  // It seems like this should be __TA_RELEASE_SHARED instead of __TA_RELEASE but
  // __TA_RELEASE_SHARED gives errors when a ScopedLock goes out of scope:
  //
  //   releasing mutex using shared access, expected exclusive access
  //
  // This is probably a compiler bug.
  ~SharedLock() __TA_RELEASE() {}

  void lock() __TA_ACQUIRE_SHARED() { lock_.lock(); }
  // Same comment about __TA_RELEASE vs. __TA_RELEASE_SHARED as in the destructor.
  void unlock() __TA_RELEASE() { lock_.unlock(); }

 private:
  std::shared_lock<SharedMutex> lock_;
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_SHARED_MUTEX_H_
