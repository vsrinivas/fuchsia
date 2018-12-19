// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/system_monitor/dockyard/dockyard.h"

#include "gtest/gtest.h"

namespace dockyard {
namespace {

class SystemMonitorDockyardTest : public ::testing::Test {
};

TEST_F(SystemMonitorDockyardTest, CallAdd) {
  EXPECT_EQ(8, dockyard::AddNumbers(3, 5));
}

}  // namespace
}  // namespace dockyard
