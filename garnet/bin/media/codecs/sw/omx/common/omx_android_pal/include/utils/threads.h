// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_THREADS_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_THREADS_H_

#include <utils/Condition.h>
#include <utils/Mutex.h>
#include <utils/Thread.h>
#include <thread>

using android_thread_id_t = void*;

inline android_thread_id_t androidGetThreadId() {
  return (android_thread_id_t)pthread_self();
}

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_THREADS_H_
