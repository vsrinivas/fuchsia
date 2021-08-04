// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/ethernet/llcpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "netdevice_migration.h"
#include "src/devices/testing/fake_ddk/include/lib/fake_ddk/fake_ddk.h"
#include "src/lib/testing/predicates/status.h"

namespace {

class MockNetworkDeviceIfc : public ddk::Device<MockNetworkDeviceIfc>,
                             public ddk::NetworkDeviceIfcProtocol<MockNetworkDeviceIfc> {
 public:
  MockNetworkDeviceIfc()
      : ddk::Device<MockNetworkDeviceIfc>(fake_ddk::kFakeDevice),
        proto_({&network_device_ifc_protocol_ops_, this}){};
  void DdkRelease(){};
  network_device_ifc_protocol_t& proto() { return proto_; }

  MOCK_METHOD(void, NetworkDeviceIfcPortStatusChanged,
              (uint8_t id, const port_status_t* new_status));
  MOCK_METHOD(void, NetworkDeviceIfcAddPort, (uint8_t id, const network_port_protocol_t* port));
  MOCK_METHOD(void, NetworkDeviceIfcRemovePort, (uint8_t id));
  MOCK_METHOD(void, NetworkDeviceIfcCompleteRx, (const rx_buffer_t* rx_list, size_t rx_count));
  MOCK_METHOD(void, NetworkDeviceIfcCompleteTx, (const tx_result_t* rx_list, size_t tx_count));
  MOCK_METHOD(void, NetworkDeviceIfcSnoop, (const rx_buffer_t* rx_list, size_t rx_count));

 private:
  network_device_ifc_protocol_t proto_;
};

class MockEthernetImpl : public ddk::Device<MockEthernetImpl>,
                         public ddk::EthernetImplProtocol<MockEthernetImpl>,
                         public fake_ddk::Bind {
 public:
  MockEthernetImpl()
      : ddk::Device<MockEthernetImpl>(fake_ddk::kFakeParent),
        proto_({&ethernet_impl_protocol_ops_, this}) {
    SetProtocol(ZX_PROTOCOL_ETHERNET_IMPL, &proto_);
  };
  void DdkRelease(){};
  fake_ddk::Bind& ddk() { return *this; }

  MOCK_METHOD(zx_status_t, EthernetImplQuery, (uint32_t options, ethernet_info_t* out_info));
  MOCK_METHOD(void, EthernetImplStop, ());
  MOCK_METHOD(zx_status_t, EthernetImplStart, (const ethernet_ifc_protocol_t* ifc));
  MOCK_METHOD(void, EthernetImplQueueTx,
              (uint32_t options, ethernet_netbuf_t* netbuf,
               ethernet_impl_queue_tx_callback callback, void* cookie));
  MOCK_METHOD(zx_status_t, EthernetImplSetParam,
              (uint32_t param, int32_t value, const uint8_t* data_buffer, size_t data_size));
  MOCK_METHOD(void, EthernetImplGetBti, (zx::bti * out_bti));

 private:
  ethernet_impl_protocol_t proto_;
};

class NetdeviceMigrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    device_ = std::make_unique<netdevice_migration::NetdeviceMigration>(fake_ddk::kFakeParent);
    EXPECT_CALL(
        mock_network_device_ifc_,
        NetworkDeviceIfcAddPort(netdevice_migration::NetdeviceMigration::kPortId, testing::_))
        .Times(1);
    ASSERT_OK(device_->NetworkDeviceImplInit(&mock_network_device_ifc_.proto()));
  }

  void TearDown() override {
    device_->DdkRelease();
    auto __UNUSED temp_ref = device_.release();
  }

  MockNetworkDeviceIfc mock_network_device_ifc_;
  MockEthernetImpl mock_ethernet_impl_;
  std::unique_ptr<netdevice_migration::NetdeviceMigration> device_;
};

TEST_F(NetdeviceMigrationTest, LifetimeTest) {
  EXPECT_CALL(mock_ethernet_impl_, EthernetImplQuery(0, testing::_))
      .Times(1)
      .WillOnce(testing::Return(ZX_OK));
  EXPECT_CALL(mock_ethernet_impl_, EthernetImplGetBti(testing::_)).Times(1);
  ASSERT_OK(device_->Init());
  device_->DdkAsyncRemove();
  EXPECT_TRUE(mock_ethernet_impl_.ddk().Ok());
}

TEST_F(NetdeviceMigrationTest, NetworkDeviceImplInit) {
  ASSERT_STATUS(device_->NetworkDeviceImplInit(&mock_network_device_ifc_.proto()),
                ZX_ERR_ALREADY_BOUND);
}

TEST_F(NetdeviceMigrationTest, NetworkDeviceImplStartStop) {
  auto callback = [](void* ctx) {
    auto* callback_called = static_cast<bool*>(ctx);
    *callback_called = true;
  };

  bool callback_called = false;
  EXPECT_CALL(mock_ethernet_impl_, EthernetImplStart(testing::_))
      .Times(1)
      .WillOnce(testing::Return(ZX_OK));
  device_->NetworkDeviceImplStart(callback, &callback_called);
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(device_->IsStarted());

  callback_called = false;
  device_->NetworkDeviceImplStart(callback, &callback_called);
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(device_->IsStarted());

  callback_called = false;
  EXPECT_CALL(mock_ethernet_impl_, EthernetImplStop()).Times(1);
  device_->NetworkDeviceImplStop(callback, &callback_called);
  EXPECT_TRUE(callback_called);
  EXPECT_FALSE(device_->IsStarted());
}

TEST_F(NetdeviceMigrationTest, EthernetIfcStatus) {
  port_status_t status;
  device_->NetworkPortGetStatus(&status);
  EXPECT_EQ(status.mtu, ETH_MTU_SIZE);
  EXPECT_EQ(status.flags, static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags()));

  EXPECT_CALL(mock_network_device_ifc_,
              NetworkDeviceIfcPortStatusChanged(
                  netdevice_migration::NetdeviceMigration::kPortId,
                  testing::Pointee(testing::FieldsAre(
                      ETH_MTU_SIZE, static_cast<uint32_t>(
                                        fuchsia_hardware_network::wire::StatusFlags::kOnline)))))
      .Times(1);
  device_->EthernetIfcStatus(
      static_cast<uint32_t>(fuchsia_hardware_ethernet::wire::DeviceStatus::kOnline));
  device_->NetworkPortGetStatus(&status);
  EXPECT_EQ(status.mtu, ETH_MTU_SIZE);
  EXPECT_EQ(status.flags,
            static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline));
}

TEST_F(NetdeviceMigrationTest, EthernetIfcStatusCalledFromEthernetImplStart) {
  ethernet_ifc_protocol_t proto = device_->EthernetIfcProto();
  EXPECT_CALL(mock_ethernet_impl_, EthernetImplStart(&proto))
      .Times(1)
      .WillOnce([](const ethernet_ifc_protocol_t* proto) -> zx_status_t {
        auto client = ddk::EthernetIfcProtocolClient(proto);
        client.Status(
            static_cast<uint32_t>(fuchsia_hardware_ethernet::wire::DeviceStatus::kOnline));
        return ZX_OK;
      });
  EXPECT_CALL(mock_network_device_ifc_,
              NetworkDeviceIfcPortStatusChanged(
                  netdevice_migration::NetdeviceMigration::kPortId,
                  testing::Pointee(testing::FieldsAre(
                      ETH_MTU_SIZE, static_cast<uint32_t>(
                                        fuchsia_hardware_network::wire::StatusFlags::kOnline)))))
      .Times(1);
  ASSERT_OK(mock_ethernet_impl_.EthernetImplStart(&proto));
  port_status_t status;
  device_->NetworkPortGetStatus(&status);
  EXPECT_EQ(status.mtu, ETH_MTU_SIZE);
  EXPECT_EQ(status.flags,
            static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline));
}
}  // namespace
