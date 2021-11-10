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
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/testing/predicates/status.h"

namespace netdevice_migration {

class NetdeviceMigrationTestHelper {
 public:
  explicit NetdeviceMigrationTestHelper(NetdeviceMigration& netdev) : netdev_(netdev) {}
  // Returns true iff the driver is ready to send frames.
  bool IsTxStarted() __TA_EXCLUDES(netdev_.tx_lock_) {
    std::lock_guard<std::mutex> tx_lock(netdev_.tx_lock_);
    return netdev_.tx_started_;
  }
  // Returns true iff the driver is ready to receive frames.
  bool IsRxStarted() __TA_EXCLUDES(netdev_.rx_lock_) {
    std::lock_guard<std::mutex> rx_lock(netdev_.rx_lock_);
    return netdev_.rx_started_;
  }
  const ethernet_ifc_protocol_t& EthernetIfcProto() { return netdev_.ethernet_ifc_proto_; }
  const zx::bti& Bti() { return netdev_.eth_bti_; }
  template <typename T, typename F>
  T WithRxSpaces(F fn) __TA_EXCLUDES(netdev_.rx_lock_) {
    std::lock_guard<std::mutex> rx_lock(netdev_.rx_lock_);
    std::queue<rx_space_buffer_t>& rx_spaces = netdev_.rx_spaces_;
    fn(rx_spaces);
  }
  template <typename T, typename F>
  T WithVmoStore(F fn) __TA_EXCLUDES(netdev_.vmo_lock_) {
    fbl::AutoLock lock(&netdev_.vmo_lock_);
    NetdeviceMigrationVmoStore& vmo_store = netdev_.vmo_store_;
    return fn(vmo_store);
  }

 private:
  NetdeviceMigration& netdev_;
};

}  // namespace netdevice_migration

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

  void SetUpWithFeatures(uint32_t features) {
    EXPECT_CALL(mock_ethernet_impl_, EthernetImplQuery(0, testing::_))
        .WillOnce([features](uint32_t options, ethernet_info_t* out_info) -> zx_status_t {
          *out_info = {
              .features = features,
              .mtu = ETH_MTU_SIZE,
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

  void NetdevImplStart(zx_status_t expected) {
    if (expected != ZX_ERR_ALREADY_BOUND) {
      EXPECT_CALL(mock_ethernet_impl_, EthernetImplStart(testing::_))
          .WillOnce(testing::Return(expected));
    }
    std::optional<zx_status_t> callback_status;
    device_->NetworkDeviceImplStart(
        [](void* ctx, zx_status_t status) {
          *static_cast<std::optional<zx_status_t>*>(ctx) = status;
        },
        &callback_status);
    ASSERT_TRUE(callback_status.has_value());
    ASSERT_EQ(callback_status.value(), expected);
  }

  void NetdevImplPrepareVmo(uint8_t vmo_id) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
    device_->NetworkDeviceImplPrepareVmo(vmo_id, std::move(vmo));
  }

  MockDevice& Parent() { return *parent_; }
  testing::StrictMock<const MockNetworkDeviceIfc>& MockNetworkDevice() {
    return mock_network_device_ifc_;
  }
  testing::StrictMock<const MockEthernetImpl>& MockEthernet() { return mock_ethernet_impl_; }
  netdevice_migration::NetdeviceMigration& Device() { return *device_; }
  netdevice_migration::NetdeviceMigration* TakeDevice() { return device_.release(); }

 private:
  const std::shared_ptr<MockDevice> parent_;
  testing::StrictMock<const MockNetworkDeviceIfc> mock_network_device_ifc_;
  testing::StrictMock<const MockEthernetImpl> mock_ethernet_impl_;
  std::unique_ptr<netdevice_migration::NetdeviceMigration> device_;
};

class NetdeviceMigrationDefaultSetupTest : public NetdeviceMigrationTest {
 protected:
  void SetUp() override { SetUpWithFeatures(0); }
};

class NetdeviceMigrationEthernetDmaSetupTest : public NetdeviceMigrationTest {
 protected:
  void SetUp() override {
    EXPECT_CALL(MockEthernet(), EthernetImplGetBti(testing::_))
        .WillOnce([](zx::bti* out_bti) -> zx_status_t {
          return fake_bti_create(out_bti->reset_and_get_address());
        });
    SetUpWithFeatures(ETHERNET_FEATURE_DMA);
  }
};

TEST_F(NetdeviceMigrationDefaultSetupTest, LifetimeTest) {
  ASSERT_EQ(Parent().child_count(), 0u);
  ASSERT_OK(TakeDevice()->DeviceAdd());
  ASSERT_EQ(Parent().child_count(), 1u);
  MockDevice* device = Parent().GetLatestChild();
  ASSERT_NE(device, nullptr);
  device->ReleaseOp();
}

TEST_F(NetdeviceMigrationDefaultSetupTest, NetworkDeviceImplInit) {
  ASSERT_STATUS(Device().NetworkDeviceImplInit(&MockNetworkDevice().proto()), ZX_ERR_ALREADY_BOUND);
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
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  for (const auto& step : kTestSteps) {
    SCOPED_TRACE(step.name);
    if (step.start_status.has_value()) {
      ASSERT_NO_FATAL_FAILURE(NetdevImplStart(step.start_status.value()));
    } else {
      bool callback_called = false;
      EXPECT_CALL(MockEthernet(), EthernetImplStop()).Times(1);
      Device().NetworkDeviceImplStop([](void* ctx) { *static_cast<bool*>(ctx) = true; },
                                     &callback_called);
      EXPECT_TRUE(callback_called);
    }
    EXPECT_EQ(helper.IsTxStarted(), step.device_started);
    EXPECT_EQ(helper.IsRxStarted(), step.device_started);
  }
}

TEST_F(NetdeviceMigrationDefaultSetupTest, EthernetIfcStatus) {
  port_status_t status;
  Device().NetworkPortGetStatus(&status);
  EXPECT_EQ(status.mtu, ETH_MTU_SIZE);
  EXPECT_EQ(status.flags, static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags()));

  EXPECT_CALL(MockNetworkDevice(),
              NetworkDeviceIfcPortStatusChanged(
                  netdevice_migration::NetdeviceMigration::kPortId,
                  testing::Pointee(testing::FieldsAre(
                      ETH_MTU_SIZE, static_cast<uint32_t>(
                                        fuchsia_hardware_network::wire::StatusFlags::kOnline)))))
      .Times(1);
  Device().EthernetIfcStatus(
      static_cast<uint32_t>(fuchsia_hardware_ethernet::wire::DeviceStatus::kOnline));
  Device().NetworkPortGetStatus(&status);
  EXPECT_EQ(status.mtu, ETH_MTU_SIZE);
  EXPECT_EQ(status.flags,
            static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline));
}

TEST_F(NetdeviceMigrationDefaultSetupTest, EthernetIfcStatusCalledFromEthernetImplStart) {
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  ethernet_ifc_protocol_t proto = helper.EthernetIfcProto();
  EXPECT_CALL(MockEthernet(), EthernetImplStart(&proto))
      .WillOnce([](const ethernet_ifc_protocol_t* proto) -> zx_status_t {
        auto client = ddk::EthernetIfcProtocolClient(proto);
        client.Status(
            static_cast<uint32_t>(fuchsia_hardware_ethernet::wire::DeviceStatus::kOnline));
        return ZX_OK;
      });
  EXPECT_CALL(MockNetworkDevice(),
              NetworkDeviceIfcPortStatusChanged(
                  netdevice_migration::NetdeviceMigration::kPortId,
                  testing::Pointee(testing::FieldsAre(
                      ETH_MTU_SIZE, static_cast<uint32_t>(
                                        fuchsia_hardware_network::wire::StatusFlags::kOnline)))))
      .Times(1);
  ASSERT_OK(MockEthernet().EthernetImplStart(&proto));
  port_status_t status;
  Device().NetworkPortGetStatus(&status);
  EXPECT_EQ(status.mtu, ETH_MTU_SIZE);
  EXPECT_EQ(status.flags,
            static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline));
}

TEST_F(NetdeviceMigrationEthernetDmaSetupTest, NetworkDeviceImplPrepareReleaseVmo) {
  constexpr size_t kVmoSize = ZX_PAGE_SIZE;
  constexpr uint8_t kVMOs = 3;
  std::array<fake_bti_pinned_vmo_info_t, kVMOs> pinned_vmos;
  size_t pinned;
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  ASSERT_OK(fake_bti_get_pinned_vmos(helper.Bti().get(), pinned_vmos.data(), pinned_vmos.size(),
                                     &pinned));
  ASSERT_EQ(pinned, 0u);

  for (uint8_t vmo_id = 1; vmo_id <= kVMOs; vmo_id++) {
    ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(vmo_id));
    ASSERT_OK(fake_bti_get_pinned_vmos(helper.Bti().get(), pinned_vmos.data(), pinned_vmos.size(),
                                       &pinned));
    ASSERT_EQ(pinned, vmo_id);
    helper.WithVmoStore<void>(
        [vmo_id, kVmoSize](netdevice_migration::NetdeviceMigrationVmoStore& vmo_store) {
          auto* stored = vmo_store.GetVmo(vmo_id);
          ASSERT_NE(stored, nullptr);
          auto data = stored->data();
          ASSERT_EQ(data.size(), kVmoSize);
        });
  }

  for (uint8_t vmo_id = pinned_vmos.size(); vmo_id > 0;) {
    Device().NetworkDeviceImplReleaseVmo(vmo_id--);
    ASSERT_OK(fake_bti_get_pinned_vmos(helper.Bti().get(), pinned_vmos.data(), pinned_vmos.size(),
                                       &pinned));
    ASSERT_EQ(pinned, vmo_id);
  }
}

TEST_F(NetdeviceMigrationTest, NetworkDeviceDoesNotGetBtiIfEthDoesNotSupportDma) {
  EXPECT_CALL(MockEthernet(), EthernetImplQuery(0, testing::_))
      .WillOnce([](uint32_t options, ethernet_info_t* out_info) -> zx_status_t {
        *out_info = {
            .features = 0,
        };
        return ZX_OK;
      });
  EXPECT_CALL(MockEthernet(), EthernetImplGetBti(testing::_)).Times(0);
  ASSERT_NO_FATAL_FAILURE(CreateDevice());
}

TEST_F(NetdeviceMigrationDefaultSetupTest, NetworkDeviceImplQueueRxSpace) {
  // Literals have been arbitrarily selected in order to have distinct space
  // buffers to assert on, while observing preconditions on length.
  constexpr rx_space_buffer_t spaces[] = {
      {
          .region =
              {
                  .offset = 42,
                  .length = ETH_MTU_SIZE,
              },
      },
      {
          .region =
              {
                  .offset = 0,
                  .length = ZX_PAGE_SIZE / 2,
              },
      },
      {
          .region =
              {
                  .offset = 13,
                  .length = ETH_MTU_SIZE + 100,
              },
      },
  };
  // An unstarted netdevice will immediately return queued buffers.
  EXPECT_CALL(MockNetworkDevice(), NetworkDeviceIfcCompleteRx(testing::_, 1))
      .Times(countof(spaces));
  Device().NetworkDeviceImplQueueRxSpace(spaces, countof(spaces));
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  helper.WithRxSpaces<void>([](auto& rx_spaces) { EXPECT_TRUE(rx_spaces.empty()); });
  Device().NetworkDeviceImplQueueRxSpace(spaces, countof(spaces));
  helper.WithRxSpaces<void>([&spaces](auto& rx_spaces) {
    ASSERT_EQ(rx_spaces.size(), countof(spaces));
    for (const rx_space_buffer_t& space : spaces) {
      EXPECT_EQ(rx_spaces.front().region.offset, space.region.offset);
      EXPECT_EQ(rx_spaces.front().region.length, space.region.length);
      rx_spaces.pop();
    }
  });
}

class QueueRxSpaceFailedPreconditionTest : public NetdeviceMigrationDefaultSetupTest,
                                           public testing::WithParamInterface<uint64_t> {};

TEST_P(QueueRxSpaceFailedPreconditionTest, RemovesDriver) {
  constexpr uint32_t kSpaceId = 13;
  const rx_space_buffer_t spaces[] = {
      {
          .id = kSpaceId,
          .region =
              {
                  .length = GetParam(),
              },
      },
  };
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  auto* device = TakeDevice();
  ASSERT_OK(device->DeviceAdd());
  ASSERT_EQ(Parent().child_count(), 1u);
  // CompleteRx will not be called so set no call expectations.
  device->NetworkDeviceImplQueueRxSpace(spaces, countof(spaces));
  ASSERT_TRUE(Parent().GetLatestChild()->AsyncRemoveCalled());
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(&Parent()));
  ASSERT_EQ(Parent().child_count(), 0u);
}

INSTANTIATE_TEST_SUITE_P(
    NetdeviceMigration, QueueRxSpaceFailedPreconditionTest, testing::Values(ZX_PAGE_SIZE, 0, 100),
    [](const testing::TestParamInfo<QueueRxSpaceFailedPreconditionTest::ParamType>& info) {
      switch (info.param) {
        case ZX_PAGE_SIZE:
          return std::string("TooBig");
        case 0:
          return std::string("TooSmall");
        default:
          return fxl::StringPrintf("UnknownParam_%lu", info.param);
      }
    });

TEST_F(NetdeviceMigrationDefaultSetupTest, EthernetIfcRecv) {
  constexpr uint8_t kVmoId = 13;
  constexpr uint32_t kSpaceId = 42;
  ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(kVmoId));
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  constexpr rx_space_buffer_t spaces[] = {
      {
          .id = kSpaceId,
          .region =
              {
                  .vmo = kVmoId,
                  .offset = 0,
                  .length = ZX_PAGE_SIZE / 2,
              },
      },
  };
  Device().NetworkDeviceImplQueueRxSpace(spaces, countof(spaces));
  constexpr uint8_t rcvd[] = {0, 1, 2, 3, 4, 5, 6, 7};
  EXPECT_CALL(MockNetworkDevice(), NetworkDeviceIfcCompleteRx(
                                       testing::Pointee(testing::FieldsAre(
                                           testing::A<buffer_metadata_t>(),
                                           testing::Pointee(testing::FieldsAre(
                                               kSpaceId, 0, static_cast<uint32_t>(countof(rcvd)))),
                                           countof(spaces))),
                                       countof(spaces)));
  Device().EthernetIfcRecv(rcvd, sizeof(rcvd), 0);
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  helper.WithVmoStore<void>([&rcvd](auto& vmo_store) {
    auto* vmo = vmo_store.GetVmo(kVmoId);
    cpp20::span<uint8_t> data = vmo->data();
    data = data.subspan(0, countof(rcvd));
    for (size_t i = 0; i < countof(rcvd); ++i) {
      EXPECT_EQ(data[i], rcvd[i]);
    }
  });
}

TEST_F(NetdeviceMigrationDefaultSetupTest, EthernetIfcRecvTooBig) {
  constexpr uint8_t kVmoId = 13;
  constexpr uint32_t kSpaceId = 42;
  ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(kVmoId));
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  constexpr rx_space_buffer_t spaces[] = {
      {
          .id = kSpaceId,
          .region =
              {
                  .vmo = kVmoId,
                  .offset = 0,
                  .length = ZX_PAGE_SIZE / 2,
              },
      },
  };
  Device().NetworkDeviceImplQueueRxSpace(spaces, countof(spaces));
  std::array<uint8_t, ZX_PAGE_SIZE> rcvd;
  rcvd.fill(0);
  auto* device = TakeDevice();
  ASSERT_OK(device->DeviceAdd());
  ASSERT_EQ(Parent().child_count(), 1u);
  // CompleteRx will not be called so do not set mock expectation.
  device->EthernetIfcRecv(rcvd.data(), rcvd.size(), 0);
  ASSERT_TRUE(Parent().GetLatestChild()->AsyncRemoveCalled());
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(&Parent()));
  ASSERT_EQ(Parent().child_count(), 0u);
}

TEST_F(NetdeviceMigrationDefaultSetupTest, EthernetIfcRecvNoBuffers) {
  constexpr uint8_t kVmoId = 13;
  ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(kVmoId));
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  constexpr uint8_t bytes[] = {0, 1, 2, 3, 4, 5, 6, 7};
  // CompleteRx will not be called so do not set mock expectation.
  Device().EthernetIfcRecv(bytes, sizeof(bytes), 0);
}
}  // namespace
