// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ALooperRoster.h"

#include "AHandler.h"

namespace android {

ALooperRoster::ALooperRoster() : next_handler_id_(1) {}

ALooper::handler_id ALooperRoster::registerHandler(
    const sp<ALooper>& looper, const sp<AHandler>& handler) {
  ALooper::handler_id handler_id;
  {  // scope lock
    std::unique_lock<std::mutex> lock(mutex_);
    handler_id = next_handler_id_++;
  }  // ~lock
  handler->setID(handler_id, looper);
  return handler_id;
}

void ALooperRoster::unregisterHandler(ALooper::handler_id handler_id) {}

void ALooperRoster::unregisterStaleHandlers() {}

}  // namespace android
