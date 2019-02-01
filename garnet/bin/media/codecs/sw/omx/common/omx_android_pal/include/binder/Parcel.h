// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_BINDER_PARCEL_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_BINDER_PARCEL_H_

#include <stdint.h>
#include <utils/Errors.h>

namespace android {

class Parcel {
 public:
  int32_t readInt32() const;
  const char* readCString() const;
  int64_t readInt64() const;
  float readFloat() const;
  double readDouble() const;
  status_t writeInt32(int32_t value);
  status_t writeCString(const char* string);
  status_t writeInt64(int64_t value);
  status_t writeFloat(float value);
  status_t writeDouble(double value);

 private:
};

}  // namespace android

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_BINDER_PARCEL_H_
