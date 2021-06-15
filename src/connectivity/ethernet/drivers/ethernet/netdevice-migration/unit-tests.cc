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

TEST(NetdeviceMigrationTest, NetworkDeviceImplInit) {
  ethernet_testing::EthernetTester tester;
  auto device = std::make_unique<netdevice_migration::NetdeviceMigration>(fake_ddk::kFakeParent);
  network_device_ifc_protocol_ops_t fakeNetworkDeviceIfcOps = {
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
  network_device_ifc_protocol_t fakeNetworkDeviceIfcProtocol = {
      .ops = &fakeNetworkDeviceIfcOps,
      .ctx = nullptr,
  };
  ASSERT_OK(device->NetworkDeviceImplInit(&fakeNetworkDeviceIfcProtocol));
  ASSERT_STATUS(device->NetworkDeviceImplInit(&fakeNetworkDeviceIfcProtocol), ZX_ERR_ALREADY_BOUND);
  device->DdkRelease();
  auto __UNUSED temp_ref = device.release();
}

}  // namespace
