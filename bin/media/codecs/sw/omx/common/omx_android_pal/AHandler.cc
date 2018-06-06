// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "AHandler.h"

namespace android {

AHandler::AHandler() : id_(0) {}

ALooper::handler_id AHandler::id() const { return id_; }

wp<ALooper> AHandler::getLooper() const { return looper_; }

wp<AHandler> AHandler::getHandler() const {
  return const_cast<AHandler*>(this);
}

void AHandler::setID(ALooper::handler_id id, const wp<ALooper>& looper) {
  id_ = id;
  looper_ = looper;
}

void AHandler::deliverMessage(const sp<AMessage>& message) {
  onMessageReceived(message);
}

}  // namespace android
