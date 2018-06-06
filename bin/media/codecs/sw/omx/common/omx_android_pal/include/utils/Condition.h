// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_CONDITION_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_CONDITION_H_

#include <mutex>

#include <utils/Errors.h>

typedef int64_t nsecs_t;

namespace android {

class Mutex;

class Condition {
 public:
  // no timeout
  status_t wait(Mutex& to_wait_on);
  status_t waitRelative(Mutex& mutex, nsecs_t relative_timeout);
  // signal one waiting thread if there are any
  void signal();
  // signal all waiting threads
  void broadcast();

 private:
  std::condition_variable condition_;
};

}  // namespace android

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_CONDITION_H_
