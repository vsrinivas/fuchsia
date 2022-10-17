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
  const device_info_t& Info() { return netdev_.info_; }
  const ethernet_ifc_protocol_t& EthernetIfcProto() { return netdev_.ethernet_ifc_proto_; }
  const network_device_impl_protocol_ops_t& NetworkDeviceImplProtoOps() {
    return netdev_.network_device_impl_protocol_ops_;
  }
  const std::array<uint8_t, MAC_SIZE>& Mac() { return netdev_.mac_; }
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
  template <typename T, typename F>
  T WithNetbufPool(F fn) __TA_EXCLUDES(netdev_.tx_lock_) {
    std::lock_guard<std::mutex> lock(netdev_.tx_lock_);
    NetbufPool& netbuf_pool = netdev_.netbuf_pool_;
    return fn(netbuf_pool);
  }

 private:
  NetdeviceMigration& netdev_;
};

}  // namespace netdevice_migration

namespace {

constexpr uint8_t kVmoId = 13;
constexpr uint32_t kFifoDepth = netdevice_migration::NetdeviceMigration::kFifoDepth;
// Include arbitrary bytes to exercise fuchsia.hardware.ethernet API contract.
constexpr size_t kNetbufSz = sizeof(ethernet_netbuf_t) + 40;

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
    zx::result device = netdevice_migration::NetdeviceMigration::Create(parent_.get());
    ASSERT_OK(device.status_value());
    device_ = std::move(device.value());
  }

  void CreateDeviceFails(zx_status_t expected) {
    zx::result device = netdevice_migration::NetdeviceMigration::Create(parent_.get());
    ASSERT_STATUS(device.status_value(), expected);
  }

  void SetUpWithFeatures(uint32_t features) {
    EXPECT_CALL(mock_ethernet_impl_, EthernetImplQuery(0, testing::_))
        .WillOnce([features](uint32_t options, ethernet_info_t* out_info) -> zx_status_t {
          *out_info = {
              .features = features,
              .mtu = ETH_MTU_SIZE,
              .mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
              .netbuf_size = kNetbufSz,
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
    NetdevImplPrepareVmo(vmo_id, std::move(vmo));
  }

  void NetdevImplPrepareVmo(uint8_t vmo_id, zx::vmo vmo) {
    bool called = false;
    device_->NetworkDeviceImplPrepareVmo(
        vmo_id, std::move(vmo),
        [](void* cookie, zx_status_t status) {
          *static_cast<bool*>(cookie) = true;
          ASSERT_OK(status);
        },
        &called);
    // Synchronous behavior offered by helper function is true iff the
    // implementation calls the callback inline.
    ASSERT_TRUE(called);
  }

  // Use a helper method rather than a parameterized test so that we can leverage test fixtures for
  // alternate SetUp() implementations (parameterized tests can only use one test fixture).
  void QueueTx(bool has_phys) {
    ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(kVmoId));
    ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
    netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
    const uint8_t* vmo_start = helper.WithVmoStore<uint8_t*>(
        [](netdevice_migration::NetdeviceMigrationVmoStore& vmo_store) {
          auto* vmo = vmo_store.GetVmo(kVmoId);
          return vmo->data().data();
        });
    constexpr uint32_t kBufId = 42;
    buffer_region_t region = {.vmo = kVmoId, .length = ETH_MTU_SIZE};
    tx_buffer_t buf = {.id = kBufId, .data_list = &region, .data_count = 1};
    EXPECT_CALL(
        MockEthernet(),
        EthernetImplQueueTx(0,
                            testing::Pointee(testing::FieldsAre(
                                testing::A<const uint8_t*>(), region.length,
                                testing::A<zx_paddr_t>(), static_cast<short>(0u), 0)),
                            testing::An<ethernet_impl_queue_tx_callback>(), testing::A<void*>()))
        .WillOnce([has_phys, vmo_start](uint32_t options, ethernet_netbuf_t* netbuf,
                                        ethernet_impl_queue_tx_callback callback, void* cookie) {
          ASSERT_EQ(netbuf->data_buffer, vmo_start);
          ASSERT_EQ(netbuf->data_size, ETH_MTU_SIZE);
          if (has_phys) {
            ASSERT_NE(netbuf->phys, 0ul);
          } else {
            ASSERT_EQ(netbuf->phys, 0ul);
          }
          callback(cookie, ZX_OK, netbuf);
        });
    EXPECT_CALL(MockNetworkDevice(),
                NetworkDeviceIfcCompleteTx(testing::Pointee(testing::FieldsAre(kBufId, ZX_OK)), 1))
        .Times(1);
    Device().NetworkDeviceImplQueueTx(&buf, 1);
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

TEST_F(NetdeviceMigrationDefaultSetupTest, DeviceInfoPreconditions) {
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  const device_info_t& info = helper.Info();
  // buffer_alignment > max_buffer_length leads to either unnecessary wasting of contiguous memory,
  // or for the configuration to be rejected altogether.
  ASSERT_LE(info.buffer_alignment, info.max_buffer_length);
}

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
            .netbuf_size = sizeof(ethernet_netbuf_t),
        };
        return ZX_OK;
      });
  EXPECT_CALL(MockEthernet(), EthernetImplGetBti(testing::_)).Times(0);
  ASSERT_NO_FATAL_FAILURE(CreateDevice());
}

TEST_F(NetdeviceMigrationTest, InvalidNetbufSzRemovesDriver) {
  EXPECT_CALL(MockEthernet(), EthernetImplQuery(0, testing::_))
      .WillOnce([](uint32_t options, ethernet_info_t* out_info) -> zx_status_t {
        *out_info = {
            .netbuf_size = sizeof(ethernet_netbuf_t) / 2,
        };
        return ZX_OK;
      });
  ASSERT_NO_FATAL_FAILURE(CreateDeviceFails(ZX_ERR_NOT_SUPPORTED));
}

TEST_F(NetdeviceMigrationDefaultSetupTest, ObservesNetbufSz) {
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  helper.WithNetbufPool<void>([](netdevice_migration::NetbufPool& netbuf_pool) {
    std::optional netbuf = netbuf_pool.pop();
    ASSERT_TRUE(netbuf.has_value());
    ASSERT_GE(netbuf->size(), kNetbufSz);
  });
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
      .Times(std::size(spaces));
  Device().NetworkDeviceImplQueueRxSpace(spaces, std::size(spaces));
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  helper.WithRxSpaces<void>([](auto& rx_spaces) { EXPECT_TRUE(rx_spaces.empty()); });
  Device().NetworkDeviceImplQueueRxSpace(spaces, std::size(spaces));
  helper.WithRxSpaces<void>([&spaces](auto& rx_spaces) {
    ASSERT_EQ(rx_spaces.size(), std::size(spaces));
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
  device->NetworkDeviceImplQueueRxSpace(spaces, std::size(spaces));
  ASSERT_TRUE(Parent().GetLatestChild()->AsyncRemoveCalled());
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(&Parent()));
  ASSERT_EQ(Parent().child_count(), 0u);
}

INSTANTIATE_TEST_SUITE_P(
    NetdeviceMigration, QueueRxSpaceFailedPreconditionTest, testing::Values(ZX_PAGE_SIZE, 0),
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
  Device().NetworkDeviceImplQueueRxSpace(spaces, std::size(spaces));
  constexpr uint8_t rcvd[] = {0, 1, 2, 3, 4, 5, 6, 7};
  EXPECT_CALL(
      MockNetworkDevice(),
      NetworkDeviceIfcCompleteRx(
          testing::Pointee(testing::FieldsAre(
              testing::FieldsAre(
                  netdevice_migration::NetdeviceMigration::kPortId, testing::A<frame_info_t>(),
                  static_cast<uint32_t>(fuchsia_hardware_network::wire::InfoType::kNoInfo),
                  static_cast<uint32_t>(fuchsia_hardware_network::RxFlags()),
                  static_cast<uint8_t>(fuchsia_hardware_network::FrameType::kEthernet)),
              testing::Pointee(
                  testing::FieldsAre(kSpaceId, 0, static_cast<uint32_t>(std::size(rcvd)))),
              std::size(spaces))),
          std::size(spaces)));
  Device().EthernetIfcRecv(rcvd, sizeof(rcvd), 0);
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  helper.WithVmoStore<void>([&rcvd](auto& vmo_store) {
    auto* vmo = vmo_store.GetVmo(kVmoId);
    cpp20::span<uint8_t> data = vmo->data();
    data = data.subspan(0, std::size(rcvd));
    for (size_t i = 0; i < std::size(rcvd); ++i) {
      EXPECT_EQ(data[i], rcvd[i]);
    }
  });
}

TEST_F(NetdeviceMigrationDefaultSetupTest, EthernetIfcRecvNoBuffers) {
  ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(kVmoId));
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  constexpr uint8_t bytes[] = {0, 1, 2, 3, 4, 5, 6, 7};
  // CompleteRx will not be called so do not set mock expectation.
  Device().EthernetIfcRecv(bytes, sizeof(bytes), 0);
}

struct RecvFailedPreconditionInput {
  const char* name;
  const size_t buf_len;
  const uint8_t vmo_id;
};

class RecvFailedPreconditionTest : public NetdeviceMigrationDefaultSetupTest,
                                   public testing::WithParamInterface<RecvFailedPreconditionInput> {
};

TEST_P(RecvFailedPreconditionTest, RemovesDriver) {
  RecvFailedPreconditionInput input = GetParam();
  constexpr uint32_t kSpaceId = 42;
  ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(kVmoId));
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  rx_space_buffer_t spaces[] = {
      {
          .id = kSpaceId,
          .region =
              {
                  .vmo = input.vmo_id,
                  .offset = 0,
                  .length = ZX_PAGE_SIZE / 2,
              },
      },
  };
  Device().NetworkDeviceImplQueueRxSpace(spaces, std::size(spaces));
  uint8_t bytes[input.buf_len];
  auto* device = TakeDevice();
  ASSERT_OK(device->DeviceAdd());
  ASSERT_EQ(Parent().child_count(), 1u);
  // CompleteRx will not be called so do not set mock expectation.
  device->EthernetIfcRecv(bytes, input.buf_len, 0);
  ASSERT_TRUE(Parent().GetLatestChild()->AsyncRemoveCalled());
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(&Parent()));
  ASSERT_EQ(Parent().child_count(), 0u);
}

INSTANTIATE_TEST_SUITE_P(
    NetdeviceMigration, RecvFailedPreconditionTest,
    testing::Values(
        RecvFailedPreconditionInput{
            .name = "BufferTooBig",
            .buf_len = ZX_PAGE_SIZE,
            .vmo_id = kVmoId,
        },
        RecvFailedPreconditionInput{
            .name = "UnknownVmoId", .buf_len = ETH_FRAME_MAX_SIZE, .vmo_id = 24}),
    [](const testing::TestParamInfo<RecvFailedPreconditionTest::ParamType>& info) {
      RecvFailedPreconditionInput input = info.param;
      return input.name;
    });

TEST_F(NetdeviceMigrationDefaultSetupTest, NetworkDeviceImplQueueTx) {
  ASSERT_NO_FATAL_FAILURE(QueueTx(false));
}

TEST_F(NetdeviceMigrationEthernetDmaSetupTest, NetworkDeviceImplQueueTxDma) {
  ASSERT_NO_FATAL_FAILURE(QueueTx(true));
}

struct FillTxQueueInput {
  const char* name;
  uint32_t buffer_count;
  uint32_t tx_queue_calls;
};

struct OutOfLineCallbacks {
  const char* name;
  bool enabled;
};

class FillTxQueueTest
    : public NetdeviceMigrationDefaultSetupTest,
      public testing::WithParamInterface<std::tuple<FillTxQueueInput, OutOfLineCallbacks>> {};

TEST_P(FillTxQueueTest, Succeeds) {
  FillTxQueueInput input = std::get<0>(GetParam());
  OutOfLineCallbacks ool = std::get<1>(GetParam());
  zx::vmo vmo;
  constexpr uint64_t kVmoSize = ZX_PAGE_SIZE;
  ASSERT_OK(zx::vmo::create(kVmoSize, 0, &vmo));
  ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(kVmoId, std::move(vmo)));
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  const uint8_t* vmo_start =
      helper.WithVmoStore<uint8_t*>([](netdevice_migration::NetdeviceMigrationVmoStore& vmo_store) {
        auto* vmo = vmo_store.GetVmo(kVmoId);
        return vmo->data().data();
      });
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  for (uint32_t call = 0; call < input.tx_queue_calls; ++call) {
    tx_buffer_t buffers[input.buffer_count];
    buffer_region_t region = {.vmo = kVmoId, .length = ETH_MTU_SIZE};
    for (uint32_t buf_id = 0; buf_id < input.buffer_count; ++buf_id) {
      tx_buffer_t buf = {.id = ((input.buffer_count * call) + buf_id) % kFifoDepth,
                         .data_list = &region,
                         .data_count = 1};
      buffers[buf_id] = buf;
    }
    struct CallbackRecord {
      ethernet_netbuf_t* netbuf;
      ethernet_impl_queue_tx_callback cb;
      void* cookie;
    };
    std::vector<CallbackRecord> callbacks;
    EXPECT_CALL(
        MockEthernet(),
        EthernetImplQueueTx(0,
                            testing::Pointee(testing::FieldsAre(
                                testing::A<const uint8_t*>(), ETH_MTU_SIZE,
                                testing::A<zx_paddr_t>(), static_cast<short>(0u), 0)),
                            testing::An<ethernet_impl_queue_tx_callback>(), testing::A<void*>()))
        .WillRepeatedly([vmo_start, ool, &callbacks](uint32_t options, ethernet_netbuf_t* netbuf,
                                                     ethernet_impl_queue_tx_callback callback,
                                                     void* cookie) {
          EXPECT_EQ(netbuf->data_buffer, vmo_start);
          EXPECT_EQ(netbuf->data_size, ETH_MTU_SIZE);
          EXPECT_EQ(netbuf->phys, 0ul);
          if (ool.enabled) {
            callbacks.push_back({.netbuf = netbuf, .cb = callback, .cookie = cookie});
          } else {
            callback(cookie, ZX_OK, netbuf);
          }
        });
    for (uint32_t buf_id = 0; buf_id < input.buffer_count; ++buf_id) {
      EXPECT_CALL(MockNetworkDevice(),
                  NetworkDeviceIfcCompleteTx(
                      testing::Pointee(testing::FieldsAre(
                          ((input.buffer_count * call) + buf_id) % kFifoDepth, ZX_OK)),
                      1))
          .Times(1);
    }
    Device().NetworkDeviceImplQueueTx(buffers, input.buffer_count);
    if (ool.enabled) {
      for (CallbackRecord& callback : callbacks) {
        callback.cb(callback.cookie, ZX_OK, callback.netbuf);
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(NetdeviceMigration, FillTxQueueTest,
                         testing::Combine(testing::Values(
                                              FillTxQueueInput{
                                                  .name = "FillQueueInOneCall",
                                                  .buffer_count = kFifoDepth,
                                                  .tx_queue_calls = 1,
                                              },
                                              FillTxQueueInput{
                                                  .name = "FillQueueAcrossTwoCalls",
                                                  .buffer_count = (3 * kFifoDepth) / 4,
                                                  .tx_queue_calls = 2,
                                              }),
                                          testing::Values(
                                              OutOfLineCallbacks{
                                                  .name = "OutOfLineCallbacks",
                                                  .enabled = true,
                                              },
                                              OutOfLineCallbacks{
                                                  .name = "InLineCallbacks",
                                                  .enabled = false,
                                              })),
                         [](const testing::TestParamInfo<FillTxQueueTest::ParamType>& info) {
                           FillTxQueueInput input = std::get<0>(info.param);
                           OutOfLineCallbacks callbacks = std::get<1>(info.param);
                           return fxl::StringPrintf("%s_%s", input.name, callbacks.name);
                         });

TEST_F(NetdeviceMigrationDefaultSetupTest, NetworkDeviceImplQueueTxNotStarted) {
  ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(kVmoId));
  constexpr uint32_t kBufId = 42;
  tx_buffer_t buf = {.id = kBufId};
  EXPECT_CALL(MockNetworkDevice(),
              NetworkDeviceIfcCompleteTx(
                  testing::Pointee(testing::FieldsAre(kBufId, ZX_ERR_UNAVAILABLE)), 1))
      .Times(1);
  Device().NetworkDeviceImplQueueTx(&buf, 1);
}

TEST_F(NetdeviceMigrationEthernetDmaSetupTest, NetworkDeviceImplQueueTxOutOfRangeOfVmo) {
  ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(kVmoId));
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  constexpr uint32_t kBufId = 42;
  buffer_region_t part = {.vmo = kVmoId, .offset = ZX_PAGE_SIZE, .length = ETH_MTU_SIZE};
  tx_buffer_t buf = {.id = kBufId, .data_list = &part, .data_count = 1};
  EXPECT_CALL(
      MockNetworkDevice(),
      NetworkDeviceIfcCompleteTx(testing::Pointee(testing::FieldsAre(kBufId, ZX_ERR_INTERNAL)), 1))
      .Times(1);
  Device().NetworkDeviceImplQueueTx(&buf, 1);
}

struct QueueTxFailedPreconditionInput {
  const char* name;
  size_t bufs;
  size_t parts;
  size_t buf_len;
  uint8_t vmo_id;
};

class QueueTxFailedPreconditionTest
    : public NetdeviceMigrationDefaultSetupTest,
      public testing::WithParamInterface<QueueTxFailedPreconditionInput> {};

TEST_P(QueueTxFailedPreconditionTest, RemovesDriver) {
  QueueTxFailedPreconditionInput param = GetParam();
  ASSERT_NO_FATAL_FAILURE(NetdevImplPrepareVmo(kVmoId));
  ASSERT_NO_FATAL_FAILURE(NetdevImplStart(ZX_OK));
  auto* device = TakeDevice();
  ASSERT_OK(device->DeviceAdd());
  ASSERT_EQ(Parent().child_count(), 1u);
  std::vector<buffer_region_t> regions;
  for (size_t i = 0; i < param.parts; ++i) {
    regions.push_back({.vmo = param.vmo_id, .length = param.buf_len});
  }
  constexpr uint32_t kBufId = 42;
  std::vector<tx_buffer_t> buffers;
  for (size_t i = 0; i < param.bufs; ++i) {
    buffers.push_back({.id = kBufId, .data_list = regions.data(), .data_count = regions.size()});
  }
  device->NetworkDeviceImplQueueTx(buffers.data(), buffers.size());
  ASSERT_TRUE(Parent().GetLatestChild()->AsyncRemoveCalled());
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(&Parent()));
  ASSERT_EQ(Parent().child_count(), 0u);
}

INSTANTIATE_TEST_SUITE_P(
    NetdeviceMigration, QueueTxFailedPreconditionTest,
    testing::Values(
        QueueTxFailedPreconditionInput{
            .name = "TooManyBuffers",
            .bufs = kFifoDepth + 1,
            .parts = 1,
            .buf_len = ETH_FRAME_MAX_SIZE,
            .vmo_id = kVmoId,
        },
        QueueTxFailedPreconditionInput{.name = "MoreThanOneBufferPart",
                                       .bufs = 1,
                                       .parts = 2,
                                       .buf_len = ETH_FRAME_MAX_SIZE,
                                       .vmo_id = kVmoId},
        QueueTxFailedPreconditionInput{.name = "BufferTooLong",
                                       .bufs = 1,
                                       .parts = 1,
                                       .buf_len = ZX_PAGE_SIZE,
                                       .vmo_id = kVmoId},
        QueueTxFailedPreconditionInput{.name = "UnknownVmoId",
                                       .bufs = 1,
                                       .parts = 1,
                                       .buf_len = ETH_FRAME_MAX_SIZE,
                                       .vmo_id = 42}),
    [](const testing::TestParamInfo<QueueTxFailedPreconditionTest::ParamType>& info) {
      QueueTxFailedPreconditionInput input = info.param;
      return input.name;
    });

TEST_F(NetdeviceMigrationDefaultSetupTest, MacAddrGetAddress) {
  uint8_t out[MAC_SIZE];
  Device().MacAddrGetAddress(out);
  uint8_t expected[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  for (size_t i = 0; i < MAC_SIZE; ++i) {
    EXPECT_EQ(out[i], expected[i]);
  }
}

TEST_F(NetdeviceMigrationDefaultSetupTest, MacAddrGetFeatures) {
  features_t out;
  Device().MacAddrGetFeatures(&out);
  EXPECT_EQ(out.multicast_filter_count,
            netdevice_migration::NetdeviceMigration::kMulticastFilterMax);
  EXPECT_EQ(out.supported_modes,
            netdevice_migration::NetdeviceMigration::kSupportedMacFilteringModes);
}

struct MacAddrSetModeFailedPreconditionInput {
  const char* name;
  mode_t mode;
  size_t mcast_macs;
};

class MacAddrSetModeFailedPreconditionTest
    : public NetdeviceMigrationDefaultSetupTest,
      public testing::WithParamInterface<MacAddrSetModeFailedPreconditionInput> {};

TEST_P(MacAddrSetModeFailedPreconditionTest, RemovesDriver) {
  MacAddrSetModeFailedPreconditionInput param = GetParam();
  auto* device = TakeDevice();
  ASSERT_OK(device->DeviceAdd());
  ASSERT_EQ(Parent().child_count(), 1u);
  device->MacAddrSetMode(param.mode, nullptr, param.mcast_macs);
  ASSERT_TRUE(Parent().GetLatestChild()->AsyncRemoveCalled());
  ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(&Parent()));
  ASSERT_EQ(Parent().child_count(), 0u);
}

INSTANTIATE_TEST_SUITE_P(
    NetDeviceMigration, MacAddrSetModeFailedPreconditionTest,
    testing::Values(
        MacAddrSetModeFailedPreconditionInput{
            .name = "TooManyMulticastMacFilters",
            .mode = MODE_MULTICAST_FILTER,
            .mcast_macs = netdevice_migration::NetdeviceMigration::kMulticastFilterMax + 1,
        },
        MacAddrSetModeFailedPreconditionInput{
            .name = "InvalidMode",
            .mode = MODE_MULTICAST_FILTER | MODE_MULTICAST_PROMISCUOUS | MODE_PROMISCUOUS,
            .mcast_macs = netdevice_migration::NetdeviceMigration::kMulticastFilterMax,
        }),
    [](const testing::TestParamInfo<MacAddrSetModeFailedPreconditionTest::ParamType>& info) {
      MacAddrSetModeFailedPreconditionInput input = info.param;
      return input.name;
    });

TEST_F(NetdeviceMigrationDefaultSetupTest, MacAddrSetMode) {
  EXPECT_CALL(MockEthernet(),
              EthernetImplSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 0, nullptr, 0))
      .WillOnce([](uint32_t p, int32_t v, const uint8_t* data, size_t data_len) { return ZX_OK; });
  EXPECT_CALL(MockEthernet(), EthernetImplSetParam(ETHERNET_SETPARAM_PROMISC, 0, nullptr, 0))
      .WillOnce([](uint32_t p, int32_t v, const uint8_t* data, size_t data_len) { return ZX_OK; });
  std::array<uint8_t, netdevice_migration::NetdeviceMigration::kMulticastFilterMax * MAC_SIZE>
      mac_filter;
  EXPECT_CALL(MockEthernet(),
              EthernetImplSetParam(ETHERNET_SETPARAM_MULTICAST_FILTER,
                                   netdevice_migration::NetdeviceMigration::kMulticastFilterMax,
                                   testing::Pointer(mac_filter.data()), mac_filter.size()))
      .WillOnce([](uint32_t p, int32_t v, const uint8_t* data, size_t data_len) { return ZX_OK; });
  Device().MacAddrSetMode(MODE_MULTICAST_FILTER, mac_filter.data(),
                          netdevice_migration::NetdeviceMigration::kMulticastFilterMax);

  EXPECT_CALL(MockEthernet(), EthernetImplSetParam(ETHERNET_SETPARAM_PROMISC, 0, nullptr, 0))
      .WillOnce([](uint32_t p, int32_t v, const uint8_t* data, size_t data_len) { return ZX_OK; });
  EXPECT_CALL(MockEthernet(),
              EthernetImplSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0))
      .WillOnce([](uint32_t p, int32_t v, const uint8_t* data, size_t data_len) { return ZX_OK; });
  Device().MacAddrSetMode(MODE_MULTICAST_PROMISCUOUS, nullptr, 0u);

  EXPECT_CALL(MockEthernet(), EthernetImplSetParam(ETHERNET_SETPARAM_PROMISC, 1, nullptr, 0))
      .WillOnce([](uint32_t p, int32_t v, const uint8_t* data, size_t data_len) { return ZX_OK; });
  Device().MacAddrSetMode(MODE_PROMISCUOUS, nullptr, 0u);
}

TEST_F(NetdeviceMigrationDefaultSetupTest, GetMac) {
  mac_addr_protocol_t mac;
  Device().NetworkPortGetMac(&mac);
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  std::array<uint8_t, MAC_SIZE> addr = {};
  mac.ops->get_address(mac.ctx, addr.data());
  for (size_t i = 0; i < addr.size(); ++i) {
    EXPECT_EQ(addr[i], helper.Mac()[i]);
  }
}

TEST_F(NetdeviceMigrationDefaultSetupTest, NetworkDeviceImplProto) {
  EXPECT_EQ(Device().ddk_proto_id_, ZX_PROTOCOL_NETWORK_DEVICE_IMPL);
  netdevice_migration::NetdeviceMigrationTestHelper helper(Device());
  EXPECT_EQ(Device().ddk_proto_ops_, &helper.NetworkDeviceImplProtoOps());
}

}  // namespace
