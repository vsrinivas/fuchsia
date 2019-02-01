// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_STRING8_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_STRING8_H_

#include <utils/Errors.h>
#include <string>

namespace android {

class String8 : public std::string {
 public:
  String8();
  status_t appendFormat(const char* format, ...)
      __attribute__((format(printf, 2, 3)));
  status_t appendFormatV(const char* format, va_list args);
  const char* string() const;

 private:
};

}  // namespace android

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_STRING8_H_
