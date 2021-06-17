// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "netdevice_migration.h"
#include "src/connectivity/ethernet/drivers/ethernet/test_util.h"

namespace {

class NetdeviceMigrationTest : public zxtest::Test {
 protected:
  void SetUp() override {
    device_ = std::make_unique<netdevice_migration::NetdeviceMigration>(fake_ddk::kFakeParent);
    ASSERT_OK(device_->NetworkDeviceImplInit(&fake_network_device_ifc_protocol_));
  }

  void TearDown() override {
    device_->DdkRelease();
    auto __UNUSED temp_ref = device_.release();
  }

  ethernet_testing::EthernetTester tester_;
  std::unique_ptr<netdevice_migration::NetdeviceMigration> device_;
  network_device_ifc_protocol_ops_t fake_network_device_ifc_ops_ = {
      .port_status_changed =
          [](void* ctx, uint8_t id, const port_status_t* new_status) {
            ADD_FAILURE("fake PortStatusChanged() called");
          },
      .add_port = [](void* ctx, uint8_t id,
                     const network_port_protocol_t* port) { ADD_FAILURE("fake AddPort() called"); },
      .remove_port = [](void* ctx, uint8_t id) { ADD_FAILURE("fake RemovePort() called"); },
      .complete_rx = [](void* ctx, const rx_buffer_t* rx_list,
                        size_t rx_count) { ADD_FAILURE("fake CompleteRx() called"); },
      .complete_tx = [](void* ctx, const tx_result_t* tx_list,
                        size_t tx_count) { ADD_FAILURE("fake CompleteTx() called"); },
      .snoop = [](void* ctx, const rx_buffer_t* rx_list,
                  size_t rx_count) { ADD_FAILURE("fake Snoop() called"); },
  };
  const network_device_ifc_protocol_t fake_network_device_ifc_protocol_ = {
      .ops = &fake_network_device_ifc_ops_,
      .ctx = this,
  };
};

TEST_F(NetdeviceMigrationTest, LifetimeTest) {
  ASSERT_OK(device_->Init());
  device_->DdkAsyncRemove();
  EXPECT_TRUE(tester_.ddk().Ok());
}

TEST_F(NetdeviceMigrationTest, NetworkDeviceImplInit) {
  ASSERT_STATUS(device_->NetworkDeviceImplInit(&fake_network_device_ifc_protocol_),
                ZX_ERR_ALREADY_BOUND);
}

TEST_F(NetdeviceMigrationTest, NetworkDeviceImplStartStop) {
  auto callback = [](void* ctx) {
    auto* callback_called = static_cast<bool*>(ctx);
    *callback_called = true;
  };

  bool callback_called = false;
  device_->NetworkDeviceImplStart(callback, &callback_called);
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(tester_.ethmac().StartCalled(), 1);
  EXPECT_EQ(tester_.ethmac().StopCalled(), 0);
  EXPECT_TRUE(device_->IsStarted());

  callback_called = false;
  device_->NetworkDeviceImplStart(callback, &callback_called);
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(tester_.ethmac().StartCalled(), 1);
  EXPECT_EQ(tester_.ethmac().StopCalled(), 0);
  EXPECT_TRUE(device_->IsStarted());

  callback_called = false;
  device_->NetworkDeviceImplStop(callback, &callback_called);
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(tester_.ethmac().StartCalled(), 1);
  EXPECT_EQ(tester_.ethmac().StopCalled(), 1);
  EXPECT_FALSE(device_->IsStarted());
}
}  // namespace
