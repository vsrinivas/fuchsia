// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_MEDIA_STAGEFRIGHT_FOUNDATION_ALOOPERROSTER_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_MEDIA_STAGEFRIGHT_FOUNDATION_ALOOPERROSTER_H_

#include "ALooper.h"

#include <mutex>

namespace android {

struct ALooperRoster {
 public:
  ALooperRoster();
  ALooper::handler_id registerHandler(const sp<ALooper>& looper,
                                      const sp<AHandler>& handler);
  void unregisterHandler(ALooper::handler_id handler_id);
  void unregisterStaleHandlers();

 private:
  std::mutex mutex_;
  ALooper::handler_id next_handler_id_;
};

}  // namespace android

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_MEDIA_STAGEFRIGHT_FOUNDATION_ALOOPERROSTER_H_
