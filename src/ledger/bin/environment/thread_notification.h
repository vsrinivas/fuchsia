// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENVIRONMENT_THREAD_NOTIFICATION_H_
#define SRC_LEDGER_BIN_ENVIRONMENT_THREAD_NOTIFICATION_H_

#include <condition_variable>
#include <mutex>

#include "src/ledger/bin/environment/notification.h"

namespace ledger {

class ThreadNotification : public Notification {
 public:
  // Notification
  bool HasBeenNotified() const override;
  void WaitForNotification() const override;
  void Notify() override;

 private:
  // The variable to wait on for notifications.
  mutable std::condition_variable wake_;

  // Guards the members below.
  mutable std::mutex mutex_;

  // Whether the object has been notified.
  bool notified_ = false;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_ENVIRONMENT_THREAD_NOTIFICATION_H_
