// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_MUTEX_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_MUTEX_H_

#include <mutex>

namespace android {

// TODO: Maybe map this to fbl::Mutex instead.

// This is not meant to be complete - only meant to get OMX code to compile,
// link, and run without editing the OMX files.

class Mutex {
 public:
  class Autolock {
   public:
    explicit Autolock(Mutex& mutex_to_lock) {
      mutex_ = &mutex_to_lock;
      mutex_->mutex_.lock();
    }
    ~Autolock() { mutex_->mutex_.unlock(); }

   private:
    Mutex* mutex_;
  };
  Mutex();
  explicit Mutex(const char* name);

 private:
  friend class Condition;
  std::mutex mutex_;
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
};

}  // namespace android

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_MUTEX_H_
