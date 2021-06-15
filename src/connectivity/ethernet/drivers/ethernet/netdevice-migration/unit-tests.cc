// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>

#include <zxtest/zxtest.h>

#include "netdevice_migration.h"

namespace {

class NetdeviceMigrationTest : public zxtest::Test {
  void SetUp() override {}

  void TearDown() override {}

 protected:
  fake_ddk::Bind ddk_;
};

TEST_F(NetdeviceMigrationTest, LifetimeTest) {
  auto device = std::make_unique<netdevice_migration::NetdeviceMigration>(fake_ddk::kFakeParent);
  ASSERT_OK(device->Add());
  device->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
  device->DdkRelease();
  auto __UNUSED temp_ref = device.release();
}

}  // namespace
