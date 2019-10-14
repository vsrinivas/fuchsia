// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENVIRONMENT_TEST_LOOP_NOTIFICATION_H_
#define SRC_LEDGER_BIN_ENVIRONMENT_TEST_LOOP_NOTIFICATION_H_

#include <lib/async-testing/test_loop.h>

#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/environment/notification.h"

namespace ledger {

// A notification implementation for the TestLoop.
class TestLoopNotification : public Notification {
 public:
  // Returns a NotificationFactory for environment using a TestLoop.
  static Environment::NotificationFactory NewFactory(async::TestLoop* test_loop);

  TestLoopNotification(async::TestLoop* test_loop);

  // Notification
  bool HasBeenNotified() const override;
  void WaitForNotification() const override;
  void Notify() override;

 private:
  // The test loop.
  async::TestLoop* test_loop_;

  // Whether the object has been notified.
  bool notified_ = false;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_ENVIRONMENT_TEST_LOOP_NOTIFICATION_H_
