// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/ethernet/llcpp/fidl.h>

#include <array>

#include <fbl/auto_lock.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "netdevice_migration.h"
#include "src/devices/testing/fake-bti/include/lib/fake-bti/bti.h"
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
  void CreateDevice() {
    zx::status device = netdevice_migration::NetdeviceMigration::Create(fake_ddk::kFakeParent);
    ASSERT_OK(device.status_value());
    device_ = std::move(device.value());
  }

  testing::StrictMock<MockNetworkDeviceIfc> mock_network_device_ifc_;
  testing::StrictMock<MockEthernetImpl> mock_ethernet_impl_;
  std::unique_ptr<netdevice_migration::NetdeviceMigration> device_;
};

class NetdeviceMigrationDefaultSetupTest : public NetdeviceMigrationTest {
 protected:
  void SetUp() override {
    EXPECT_CALL(mock_ethernet_impl_, EthernetImplQuery(0, testing::_))
        .WillOnce([](uint32_t options, ethernet_info_t* out_info) -> zx_status_t {
          *out_info = {
              .features = 0,
          };
          return ZX_OK;
        });
    ASSERT_NO_FATAL_FAILURE(CreateDevice());
    EXPECT_CALL(
        mock_network_device_ifc_,
        NetworkDeviceIfcAddPort(netdevice_migration::NetdeviceMigration::kPortId, testing::_))
        .Times(1);
    ASSERT_OK(device_->NetworkDeviceImplInit(&mock_network_device_ifc_.proto()));
  }
};

TEST_F(NetdeviceMigrationDefaultSetupTest, LifetimeTest) {
  ASSERT_OK(device_->DeviceAdd());
  device_->DdkAsyncRemove();
  EXPECT_TRUE(mock_ethernet_impl_.ddk().Ok());
}

TEST_F(NetdeviceMigrationDefaultSetupTest, NetworkDeviceImplInit) {
  ASSERT_STATUS(device_->NetworkDeviceImplInit(&mock_network_device_ifc_.proto()),
                ZX_ERR_ALREADY_BOUND);
}

TEST_F(NetdeviceMigrationDefaultSetupTest, NetworkDeviceImplStartStop) {
  struct CallbackReturn {
    bool called;
    std::optional<zx_status_t> status;
  };
  auto start_cb = [](void* ctx, zx_status_t status) {
    auto* callback_called = static_cast<CallbackReturn*>(ctx);
    *callback_called = {
        .called = true,
        .status = status,
    };
  };
  constexpr struct {
    const char* name;
    void (*start_callback)(void*, zx_status_t);
    void (*stop_callback)(void*);
    bool device_started;
    // Only used if start_callback is not nullptr.
    zx_status_t start_status;
  } kTestSteps[] = {
      {
          .name = "failed start",
          .start_callback = start_cb,
          .start_status = ZX_ERR_INTERNAL,
      },
      {
          .name = "successful start",
          .start_callback = start_cb,
          .device_started = true,
          .start_status = ZX_OK,
      },
      {
          .name = "already bound start",
          .start_callback = start_cb,
          .device_started = true,
          .start_status = ZX_ERR_ALREADY_BOUND,
      },
      {
          .name = "stop",
          .stop_callback =
              [](void* ctx) {
                auto* callback_called = static_cast<CallbackReturn*>(ctx);
                *callback_called = {
                    .called = true,
                };
              },
      },
  };
  for (const auto& step : kTestSteps) {
    SCOPED_TRACE(step.name);
    CallbackReturn result = {};
    if (step.start_callback) {
      if (step.start_status != ZX_ERR_ALREADY_BOUND) {
        EXPECT_CALL(mock_ethernet_impl_, EthernetImplStart(testing::_))
            .WillOnce(testing::Return(step.start_status));
      }
      device_->NetworkDeviceImplStart(step.start_callback, &result);
      ASSERT_TRUE(result.status.has_value());
      ASSERT_STATUS(*result.status, step.start_status);
    } else if (step.stop_callback) {
      EXPECT_CALL(mock_ethernet_impl_, EthernetImplStop()).Times(1);
      device_->NetworkDeviceImplStop(step.stop_callback, &result);
      ASSERT_FALSE(result.status.has_value());
    }
    EXPECT_TRUE(result.called);
    EXPECT_EQ(device_->IsStarted(), step.device_started);
  }
}

TEST_F(NetdeviceMigrationDefaultSetupTest, EthernetIfcStatus) {
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

TEST_F(NetdeviceMigrationDefaultSetupTest, EthernetIfcStatusCalledFromEthernetImplStart) {
  ethernet_ifc_protocol_t proto = device_->EthernetIfcProto();
  EXPECT_CALL(mock_ethernet_impl_, EthernetImplStart(&proto))
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

TEST_F(NetdeviceMigrationTest, NetworkDeviceImplPrepareReleaseVmo) {
  constexpr size_t kVmoSize = ZX_PAGE_SIZE;
  EXPECT_CALL(mock_ethernet_impl_, EthernetImplQuery(0, testing::_))
      .WillOnce([](uint32_t options, ethernet_info_t* out_info) -> zx_status_t {
        *out_info = {
            .features = ETHERNET_FEATURE_DMA,
        };
        return ZX_OK;
      });
  EXPECT_CALL(mock_ethernet_impl_, EthernetImplGetBti(testing::_))
      .WillOnce([](zx::bti* out_bti) -> zx_status_t {
        return fake_bti_create(out_bti->reset_and_get_address());
      });
  ASSERT_NO_FATAL_FAILURE(CreateDevice());
  std::array<fake_bti_pinned_vmo_info_t, 3> pinned_vmos;
  size_t pinned;
  ASSERT_OK(fake_bti_get_pinned_vmos(device_->Bti().get(), pinned_vmos.data(), pinned_vmos.size(),
                                     &pinned));
  ASSERT_EQ(pinned, 0u);

  for (uint8_t vmo_id = 1; vmo_id <= pinned_vmos.size(); vmo_id++) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));
    device_->NetworkDeviceImplPrepareVmo(vmo_id, std::move(vmo));
    ASSERT_OK(fake_bti_get_pinned_vmos(device_->Bti().get(), pinned_vmos.data(), pinned_vmos.size(),
                                       &pinned));
    ASSERT_EQ(pinned, vmo_id);
    device_->WithVmoStore<void>(
        [vmo_id, kVmoSize](netdevice_migration::NetdeviceMigrationVmoStore& vmo_store) {
          auto* stored = vmo_store.GetVmo(vmo_id);
          ASSERT_NE(stored, nullptr);
          auto data = stored->data();
          ASSERT_EQ(data.size(), kVmoSize);
        });
  }

  for (uint8_t vmo_id = pinned_vmos.size(); vmo_id > 0;) {
    device_->NetworkDeviceImplReleaseVmo(vmo_id--);
    ASSERT_OK(fake_bti_get_pinned_vmos(device_->Bti().get(), pinned_vmos.data(), pinned_vmos.size(),
                                       &pinned));
    ASSERT_EQ(pinned, vmo_id);
  }
}

TEST_F(NetdeviceMigrationTest, NetworkDeviceDoesNotGetBtiIfEthDoesNotSupportDma) {
  EXPECT_CALL(mock_ethernet_impl_, EthernetImplQuery(0, testing::_))
      .WillOnce([](uint32_t options, ethernet_info_t* out_info) -> zx_status_t {
        *out_info = {
            .features = 0,
        };
        return ZX_OK;
      });
  EXPECT_CALL(mock_ethernet_impl_, EthernetImplGetBti(testing::_)).Times(0);
  ASSERT_NO_FATAL_FAILURE(CreateDevice());
}
}  // namespace
