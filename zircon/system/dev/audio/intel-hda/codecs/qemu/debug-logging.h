// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CODECS_QEMU_DEBUG_LOGGING_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CODECS_QEMU_DEBUG_LOGGING_H_

#include <inttypes.h>
#include <stdio.h>

// TODO(johngro) : replace this with a system which...
//
// 1) Uses low overhead loging service infrastructure instead of printf.
// 2) Uses C/C++ functions (either template parameter packs, or c-style
//    var-args) instead of preprocessor macros.

#define VERBOSE_LOGGING 0
#define DEBUG_LOGGING (VERBOSE_LOGGING || 0)

#define LOG_EX(obj, ...)      \
  do {                        \
    (obj).PrintDebugPrefix(); \
    printf(__VA_ARGS__);      \
  } while (false)

#define LOG(...) LOG_EX(*this, __VA_ARGS__)

#define DEBUG_LOG_EX(obj, ...)  \
  do {                          \
    if (DEBUG_LOGGING) {        \
      (obj).PrintDebugPrefix(); \
      printf(__VA_ARGS__);      \
    }                           \
  } while (false)

#define DEBUG_LOG(...) DEBUG_LOG_EX(*this, __VA_ARGS__)

#define VERBOSE_LOG_EX(obj, ...) \
  do {                           \
    if (VERBOSE_LOGGING) {       \
      (obj).PrintDebugPrefix();  \
      printf(__VA_ARGS__);       \
    }                            \
  } while (false)

#define VERBOSE_LOG(...) VERBOSE_LOG_EX(*this, __VA_ARGS__)

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_CODECS_QEMU_DEBUG_LOGGING_H_
