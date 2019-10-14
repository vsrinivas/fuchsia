// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/environment/thread_notification.h"

namespace ledger {

bool ThreadNotification::HasBeenNotified() const {
  std::unique_lock<std::mutex> lock(mutex_);
  return notified_;
}

void ThreadNotification::WaitForNotification() const {
  std::unique_lock<std::mutex> lock(mutex_);
  wake_.wait(lock, [this]() { return notified_; });
}

void ThreadNotification::Notify() {
  std::unique_lock<std::mutex> lock(mutex_);
  notified_ = true;
  wake_.notify_one();
}

}  // namespace ledger
