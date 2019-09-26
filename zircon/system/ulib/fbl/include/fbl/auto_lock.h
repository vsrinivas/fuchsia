// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_AUTO_LOCK_H_
#define FBL_AUTO_LOCK_H_
#ifdef __cplusplus

#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/null_lock.h>

namespace fbl {

// Default AutoLock will accept any object which represents a "mutex"
// capability, and which supports an Acquire/Release interface.
template <typename T>
class __TA_SCOPED_CAPABILITY AutoLock {
 public:
  __WARN_UNUSED_CONSTRUCTOR explicit AutoLock(T* mutex) __TA_ACQUIRE(mutex) : mutex_(mutex) {
    mutex_->Acquire();
  }
  ~AutoLock() __TA_RELEASE() { release(); }

  // early release the mutex before the object goes out of scope
  void release() __TA_RELEASE() {
    // In typical usage, this conditional will be optimized away so
    // that mutex_->Release() is called unconditionally.
    if (mutex_ != nullptr) {
      mutex_->Release();
      mutex_ = nullptr;
    }
  }

  // suppress default constructors
  DISALLOW_COPY_ASSIGN_AND_MOVE(AutoLock);

 private:
  T* mutex_;
};

// No-op specialization for the fbl::NullLock
template <>
class __TA_SCOPED_CAPABILITY AutoLock<::fbl::NullLock> {
 public:
  __WARN_UNUSED_CONSTRUCTOR explicit AutoLock(::fbl::NullLock* mutex) __TA_ACQUIRE(mutex) {}
  ~AutoLock() __TA_RELEASE() {}
  void release() __TA_RELEASE() {}
  DISALLOW_COPY_ASSIGN_AND_MOVE(AutoLock);
};

// Specialization for the C11 mtx_t type.  Not present in the kernel.
#if !_KERNEL
template <>
class __TA_SCOPED_CAPABILITY AutoLock<mtx_t> {
 public:
  __WARN_UNUSED_CONSTRUCTOR explicit AutoLock(mtx_t* mutex) __TA_ACQUIRE(mutex) : mutex_(mutex) {
    mtx_lock(mutex_);
  }
  ~AutoLock() __TA_RELEASE() { release(); }

  void release() __TA_RELEASE() {
    if (mutex_ != nullptr) {
      mtx_unlock(mutex_);
      mutex_ = nullptr;
    }
  }

  // suppress default constructors
  DISALLOW_COPY_ASSIGN_AND_MOVE(AutoLock);

 private:
  mtx_t* mutex_;
};
#endif  // if !_KERNEL

}  // namespace fbl

#endif  // #ifdef __cplusplus

#endif  // FBL_AUTO_LOCK_H_
