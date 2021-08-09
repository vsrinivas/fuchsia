// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_PUBLIC_LOCKS_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_PUBLIC_LOCKS_H_

#include <mutex>
#include <shared_mutex>

#include <fbl/mutex.h>

namespace network {

// TODO(https://fxbug.dev/75544): Get rid of these classes and this file once std::shared_mutex and
// std::shared_lock have thread analysis annotations.

class __TA_CAPABILITY("shared_mutex") SharedLock {
 public:
  SharedLock() = default;

  void Acquire() __TA_ACQUIRE() { m_.lock(); }
  void Release() __TA_RELEASE() { m_.unlock(); }

  void AcquireShared() __TA_ACQUIRE_SHARED() { m_.lock(); }
  void ReleaseShared() __TA_RELEASE_SHARED() { m_.unlock(); }

  DISALLOW_COPY_ASSIGN_AND_MOVE(SharedLock);

 private:
  std::shared_mutex m_;
};

template <typename T>
class __TA_SCOPED_CAPABILITY SharedAutoLock {
 public:
  __WARN_UNUSED_CONSTRUCTOR explicit SharedAutoLock(T* mutex) __TA_ACQUIRE_SHARED(mutex)
      : mutex_(mutex) {
    mutex_->AcquireShared();
  }
  ~SharedAutoLock() __TA_RELEASE() { release(); }

  // early release the mutex before the object goes out of scope
  void release() __TA_RELEASE() {
    // In typical usage, this conditional will be optimized away so
    // that mutex_->Release() is called unconditionally.
    if (mutex_ != nullptr) {
      mutex_->ReleaseShared();
      mutex_ = nullptr;
    }
  }

  DISALLOW_COPY_ASSIGN_AND_MOVE(SharedAutoLock);

 private:
  T* mutex_;
};
}  // namespace network

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_PUBLIC_LOCKS_H_
