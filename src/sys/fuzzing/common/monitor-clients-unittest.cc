// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/monitor-clients.h"

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/common/testing/monitor.h"

namespace fuzzing {

// Test fixtures.

class MonitorClientsTest : public AsyncTest {};

// Unit tests.

TEST_F(MonitorClientsTest, Update) {
  MonitorClients monitors(executor());

  FakeMonitor monitor1(executor());
  FakeMonitor monitor2(executor());
  monitors.Add(monitor1.NewBinding());
  monitors.Add(monitor2.NewBinding());

  Status status;
  monitors.Update(UpdateReason::INIT);

  status.set_runs(32);
  monitors.set_status(std::move(status));

  monitors.Update(UpdateReason::NEW);
  FUZZING_EXPECT_OK(monitors.AwaitAcknowledgement());
  RunUntilIdle();

  ASSERT_FALSE(monitor1.empty());
  EXPECT_EQ(monitor1.reason(), UpdateReason::INIT);
  EXPECT_FALSE(monitor1.status().has_runs());
  EXPECT_TRUE(monitor1.is_bound());

  // Monitors should have received both updates.
  monitor1.pop_front();
  ASSERT_FALSE(monitor1.empty());
  EXPECT_EQ(monitor1.reason(), UpdateReason::NEW);
  ASSERT_TRUE(monitor1.status().has_runs());
  EXPECT_EQ(monitor1.status().runs(), 32U);

  // Second monitor should have received identical updates.
  ASSERT_FALSE(monitor2.empty());
  EXPECT_EQ(monitor2.reason(), UpdateReason::INIT);
  EXPECT_FALSE(monitor2.status().has_runs());
  EXPECT_TRUE(monitor2.is_bound());

  monitor2.pop_front();
  ASSERT_FALSE(monitor2.empty());
  EXPECT_EQ(monitor2.reason(), UpdateReason::NEW);
  ASSERT_TRUE(monitor2.status().has_runs());
  EXPECT_EQ(monitor2.status().runs(), 32U);
}

TEST_F(MonitorClientsTest, Finish) {
  MonitorClients monitors(executor());

  FakeMonitor monitor1(executor());
  FakeMonitor monitor2(executor());
  monitors.Add(monitor1.NewBinding());
  monitors.Add(monitor2.NewBinding());

  monitors.Update(UpdateReason::DONE);
  FUZZING_EXPECT_OK(monitors.AwaitAcknowledgement());
  RunUntilIdle();

  ASSERT_FALSE(monitor1.empty());
  EXPECT_EQ(monitor1.reason(), UpdateReason::DONE);
  EXPECT_FALSE(monitor1.is_bound());

  ASSERT_FALSE(monitor2.empty());
  EXPECT_EQ(monitor2.reason(), UpdateReason::DONE);
  EXPECT_FALSE(monitor2.is_bound());
}

}  // namespace fuzzing
