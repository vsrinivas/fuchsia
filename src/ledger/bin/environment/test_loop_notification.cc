// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/environment/test_loop_notification.h"

#include "src/ledger/lib/logging/logging.h"

namespace ledger {

Environment::NotificationFactory TestLoopNotification::NewFactory(async::TestLoop* test_loop) {
  return [test_loop] { return std::make_unique<TestLoopNotification>(test_loop); };
}

TestLoopNotification::TestLoopNotification(async::TestLoop* test_loop) : test_loop_(test_loop) {}

bool TestLoopNotification::HasBeenNotified() const { return notified_; }

void TestLoopNotification::WaitForNotification() const {
  bool notified = test_loop_->BlockCurrentSubLoopAndRunOthersUntil([this] { return notified_; });
  LEDGER_CHECK(notified);
}

void TestLoopNotification::Notify() { notified_ = true; }

}  // namespace ledger
