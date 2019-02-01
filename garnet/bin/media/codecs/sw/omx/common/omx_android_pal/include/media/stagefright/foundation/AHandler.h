// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_MEDIA_STAGEFRIGHT_FOUNDATION_AHANDLER_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_MEDIA_STAGEFRIGHT_FOUNDATION_AHANDLER_H_

// Some android code relies on this being pulled in:
#include <media/stagefright/foundation/ALooper.h>

#include <utils/RefBase.h>

namespace android {

struct AMessage;

struct AHandler : public RefBase {
 public:
  AHandler();
  ALooper::handler_id id() const;
  wp<ALooper> getLooper() const;
  wp<AHandler> getHandler() const;

 protected:
  virtual void onMessageReceived(const sp<AMessage>& msg) = 0;

 private:
  friend struct AMessage;
  friend struct ALooperRoster;
  ALooper::handler_id id_;
  wp<ALooper> looper_;
  void setID(ALooper::handler_id id, const wp<ALooper>& looper);
  void deliverMessage(const sp<AMessage>& message);
  DISALLOW_EVIL_CONSTRUCTORS(AHandler);
};

}  // namespace android

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_MEDIA_STAGEFRIGHT_FOUNDATION_AHANDLER_H_
