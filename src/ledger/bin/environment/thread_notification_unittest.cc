// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/environment/thread_notification.h"

#include <thread>

#include <gtest/gtest.h>

namespace ledger {
namespace {

TEST(ThreadNotification, NotifyAcrossThread) {
  for (size_t i = 0; i < 1000; ++i) {
    bool called = false;
    ThreadNotification notification;
    std::thread t([&] {
      called = true;
      notification.Notify();
    });
    notification.WaitForNotification();
    EXPECT_TRUE(called);
    t.join();
  }
}

}  // namespace
}  // namespace ledger
