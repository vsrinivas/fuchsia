// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_STRING16_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_STRING16_H_

#include <string>

namespace android {

class String16 : public std::u16string {
 public:
  explicit String16(const char* from_string);

 private:
};

}  // namespace android

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_STRING16_H_
