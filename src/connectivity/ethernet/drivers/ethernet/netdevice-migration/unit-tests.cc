// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.ethernet/cpp/wire.h>

#include <array>

#include <fbl/auto_lock.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "netdevice_migration.h"
#include "src/devices/testing/fake-bti/include/lib/fake-bti/bti.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/lib/testing/predicates/status.h"

namespace {

class MockNetworkDeviceIfc : public ddk::Device<MockNetworkDeviceIfc>,
                             public ddk::NetworkDeviceIfcProtocol<MockNetworkDeviceIfc> {
 public:
  explicit MockNetworkDeviceIfc(MockDevice* parent)
      : ddk::Device<MockNetworkDeviceIfc>(parent),
        proto_({&network_device_ifc_protocol_ops_, this}) {}
  void DdkRelease() {}
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
                         public ddk::EthernetImplProtocol<MockEthernetImpl> {
 public:
  explicit MockEthernetImpl(MockDevice* parent)
      : ddk::Device<MockEthernetImpl>(parent), proto_({&ethernet_impl_protocol_ops_, this}) {
    parent->AddProtocol(ZX_PROTOCOL_ETHERNET_IMPL, proto_.ops, proto_.ctx);
  }
  void DdkRelease() {}

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
  NetdeviceMigrationTest()
      : parent_(MockDevice::FakeRootParent()),
        mock_network_device_ifc_(parent_.get()),
        mock_ethernet_impl_(parent_.get()) {}

  void CreateDevice() {
    zx::status device = netdevice_migration::NetdeviceMigration::Create(parent_.get());
    ASSERT_OK(device.status_value());
    device_ = std::move(device.value());
  }

  const std::shared_ptr<MockDevice> parent_;
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
  ASSERT_EQ(parent_->child_count(), 0u);
  ASSERT_OK(device_.release()->DeviceAdd());
  ASSERT_EQ(parent_->child_count(), 1u);
  MockDevice* device = parent_->GetLatestChild();
  ASSERT_NE(device, nullptr);
  device->ReleaseOp();
}

TEST_F(NetdeviceMigrationDefaultSetupTest, NetworkDeviceImplInit) {
  ASSERT_STATUS(device_->NetworkDeviceImplInit(&mock_network_device_ifc_.proto()),
                ZX_ERR_ALREADY_BOUND);
}

TEST_F(NetdeviceMigrationDefaultSetupTest, NetworkDeviceImplStartStop) {
  constexpr struct {
    const char* name;
    bool device_started;
    // Step calls ImplStart if set, ImplStop otherwise.
    std::optional<zx_status_t> start_status;
  } kTestSteps[] = {
      {
          .name = "failed start",
          .start_status = ZX_ERR_INTERNAL,
      },
      {
          .name = "successful start",
          .device_started = true,
          .start_status = ZX_OK,
      },
      {
          .name = "already bound start",
          .device_started = true,
          .start_status = ZX_ERR_ALREADY_BOUND,
      },
      {
          .name = "stop",
      },
  };
  for (const auto& step : kTestSteps) {
    SCOPED_TRACE(step.name);
    if (step.start_status.has_value()) {
      const zx_status_t start_status = step.start_status.value();
      if (start_status != ZX_ERR_ALREADY_BOUND) {
        EXPECT_CALL(mock_ethernet_impl_, EthernetImplStart(testing::_))
            .WillOnce(testing::Return(start_status));
      }
      std::optional<zx_status_t> callback_status;
      device_->NetworkDeviceImplStart(
          [](void* ctx, zx_status_t status) {
            std::optional<zx_status_t>* ptr = static_cast<std::optional<zx_status_t>*>(ctx);
            *ptr = status;
          },
          &callback_status);
      ASSERT_TRUE(callback_status.has_value());
      ASSERT_STATUS(callback_status.value(), start_status);
    } else {
      bool callback_called = false;
      EXPECT_CALL(mock_ethernet_impl_, EthernetImplStop()).Times(1);
      device_->NetworkDeviceImplStop(
          [](void* ctx) {
            bool* ptr = static_cast<bool*>(ctx);
            *ptr = true;
          },
          &callback_called);
      EXPECT_TRUE(callback_called);
    }
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
  constexpr uint8_t kVMOs = 3;
  std::array<fake_bti_pinned_vmo_info_t, kVMOs> pinned_vmos;
  size_t pinned;
  ASSERT_OK(fake_bti_get_pinned_vmos(device_->Bti().get(), pinned_vmos.data(), pinned_vmos.size(),
                                     &pinned));
  ASSERT_EQ(pinned, 0u);

  for (uint8_t vmo_id = 1; vmo_id <= kVMOs; vmo_id++) {
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
