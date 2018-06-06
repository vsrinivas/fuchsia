// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_LOG_LOG_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_LOG_LOG_H_

// Some android source relies on this being pulled in:
#include <unistd.h>

#include <stdint.h>

// This is a convenient place to pull in these non-log-related definitions for
// "typeof" and "__unused" without resorting to cflags
// -includedefine_typeof_and_unused.h which editors don't tend to pick up on.
#include "define_typeof_and_unused.h"

#ifndef LOG_TAG
#define LOG_TAG nullptr
#endif

enum {
  ANDROID_LOG_UNKNOWN = 0,
  ANDROID_LOG_DEFAULT,
  ANDROID_LOG_VERBOSE,
  ANDROID_LOG_DEBUG,
  ANDROID_LOG_INFO,
  ANDROID_LOG_WARN,
  ANDROID_LOG_ERROR,
  ANDROID_LOG_FATAL,
  ANDROID_LOG_SILENT,
};

#define android_errorWriteLog(tag, subTag) \
  __android_log_error_write(tag, subTag, -1, nullptr, 0)

#define android_printLog(prio, tag, ...) \
  __android_log_print(prio, tag, __VA_ARGS__)

#define LOG_PRI(priority, tag, ...) android_printLog(priority, tag, __VA_ARGS__)
#define ALOG(priority, tag, ...) LOG_PRI(ANDROID_##priority, tag, __VA_ARGS__)

#define __android_second(dummy, second, ...) second
#define __android_rest(first, ...) , ##__VA_ARGS__
#define android_printAssert(condition, tag, ...)                \
  __android_log_assert(condition, tag,                          \
                       __android_second(0, ##__VA_ARGS__, NULL) \
                           __android_rest(__VA_ARGS__))

#define LOG_ALWAYS_FATAL_IF(condition, ...)                              \
  ((condition)                                                           \
       ? ((void)android_printAssert(#condition, LOG_TAG, ##__VA_ARGS__)) \
       : (void)0)

#define LOG_ALWAYS_FATAL(...) \
  (((void)android_printAssert(NULL, LOG_TAG, ##__VA_ARGS__)))

#define ALOGV(...) ((void)ALOG(LOG_VERBOSE, LOG_TAG, __VA_ARGS__))
#define ALOGE(...) ((void)ALOG(LOG_ERROR, LOG_TAG, __VA_ARGS__))
#define ALOGW(...) ((void)ALOG(LOG_WARN, LOG_TAG, __VA_ARGS__))

inline void fake_alogi(...) {}
#define ALOGI(...) fake_alogi(__VA_ARGS__)

#define LOG_FATAL_IF(cond, ...) LOG_ALWAYS_FATAL_IF(cond, ##__VA_ARGS__)

#define LOG_FATAL(...) LOG_ALWAYS_FATAL(__VA_ARGS__)

#define ALOG_ASSERT(cond, ...) LOG_FATAL_IF(!(cond), ##__VA_ARGS__)

extern "C" {

int __android_log_error_write(int tag, const char* subTag, int32_t uid,
                              const char* data, uint32_t dataLength);

int __android_log_print(int priority, const char* tag, const char* format, ...);

void __android_log_assert(const char* condition, const char* tag,
                          const char* format, ...);
}

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_LOG_LOG_H_
