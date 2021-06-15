// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "netdevice_migration.h"
#include "src/connectivity/ethernet/drivers/ethernet/test_util.h"

namespace {

TEST(NetdeviceMigrationTest, LifetimeTest) {
  ethernet_testing::EthernetTester tester;
  auto device = std::make_unique<netdevice_migration::NetdeviceMigration>(fake_ddk::kFakeParent);
  ASSERT_OK(device->Init());
  device->DdkAsyncRemove();
  EXPECT_TRUE(tester.ddk().Ok());
  device->DdkRelease();
  auto __UNUSED temp_ref = device.release();
}

}  // namespace
