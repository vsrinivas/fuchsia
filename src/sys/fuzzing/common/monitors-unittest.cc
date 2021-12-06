// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/monitors.h"

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/testing/monitor.h"

namespace fuzzing {

TEST(MonitorClientsTest, Update) {
  MonitorClients monitors;

  FakeMonitor monitor1, monitor2;
  monitors.Add(monitor1.NewBinding());
  monitors.Add(monitor2.NewBinding());

  Status status;
  status.set_runs(32);
  monitors.SetStatus(std::move(status));

  monitors.Update(UpdateReason::NEW);

  UpdateReason reason1;
  auto status1 = monitor1.NextStatus(&reason1);
  EXPECT_EQ(reason1, UpdateReason::NEW);
  EXPECT_EQ(status1.runs(), 32U);

  UpdateReason reason2;
  auto status2 = monitor2.NextStatus(&reason2);
  EXPECT_EQ(reason2, UpdateReason::NEW);
  EXPECT_EQ(status2.runs(), 32U);
}

TEST(MonitorClientsTest, Finish) {
  MonitorClients monitors;

  FakeMonitor monitor1, monitor2;
  monitors.Add(monitor1.NewBinding());
  monitors.Add(monitor2.NewBinding());

  monitors.Update(UpdateReason::DONE);

  EXPECT_EQ(monitor1.NextReason(), UpdateReason::DONE);
  EXPECT_EQ(monitor2.NextReason(), UpdateReason::DONE);

  monitor1.AwaitClose();
  monitor2.AwaitClose();
}

}  // namespace fuzzing
