// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netdevice.h"

#include <lib/fake-bti/bti.h>
#include <lib/virtio/backends/fake.h>

#include <atomic>
#include <queue>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace virtio {

class FakeBackendForNetdeviceTest : public FakeBackend {
 public:
  using Base = FakeBackend;
  static constexpr uint8_t kMac[] = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

  FakeBackendForNetdeviceTest()
      : FakeBackend({{NetworkDevice::kRxId, NetworkDevice::kBacklog},
                     {NetworkDevice::kTxId, NetworkDevice::kBacklog}}) {
    for (size_t i = 0; i < sizeof(virtio_net_config_t); i++) {
      AddClassRegister(static_cast<uint16_t>(i), static_cast<uint8_t>(0));
    }
    SetLinkUp();
  }

  void Terminate() override { sync_completion_signal(&completion_); }
  // We'll trigger interrupts manually during testing, keep the interrupt thread
  // locked until termination.
  zx::result<uint32_t> WaitForInterrupt() override {
    sync_completion_wait(&completion_, ZX_TIME_INFINITE);
    return zx::ok(0);
  }

  void SetLinkUp() { UpdateStatus(true); }
  void SetLinkDown() { UpdateStatus(false); }

  void UpdateStatus(bool link_up) {
    virtio_net_config_t config = {};
    if (link_up) {
      config.status = VIRTIO_NET_S_LINK_UP;
    };
    static_assert(sizeof(kMac) == sizeof(config.mac));
    std::copy(std::begin(kMac), std::end(kMac), config.mac);
    for (size_t i = 0; i < sizeof(config); ++i) {
      SetClassRegister(static_cast<uint16_t>(i), reinterpret_cast<uint8_t*>(&config)[i]);
    }
  }

  bool IsQueueKicked(uint16_t queue_index) { return QueueKicked(queue_index); }

  void DeviceReset() override {
    FakeBackend::DeviceReset();
    rx_ring_started_ = false;
    tx_ring_started_ = false;
  }

  void SetFeature(uint32_t bit) override { feature_bits_ |= bit; }

  bool ReadFeature(uint32_t bit) override {
    switch (bit) {
      case VIRTIO_F_VERSION_1:
        return support_feature_v1_;
      default:
        return FakeBackend::ReadFeature(bit);
    }
  }

  zx_status_t SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
                      zx_paddr_t pa_used) override {
    switch (index) {
      case NetworkDevice::kRxId:
        EXPECT_FALSE(rx_ring_started_);
        rx_ring_started_ = true;
        break;
      case NetworkDevice::kTxId:
        EXPECT_FALSE(tx_ring_started_);
        tx_ring_started_ = true;
        break;
      default:
        ADD_FAILURE("unexpected ring index %d", index);
        return ZX_ERR_INTERNAL;
    }
    EXPECT_EQ(count, NetworkDevice::kBacklog);
    return ZX_OK;
  }

  bool rx_ring_started() const { return rx_ring_started_; }
  bool tx_ring_started() const { return tx_ring_started_; }
  uint32_t feature_bits() const { return feature_bits_; }
  void SetSupportFeatureV1(bool v1) { support_feature_v1_ = v1; }

 private:
  sync_completion_t completion_;
  bool rx_ring_started_ = false;
  bool tx_ring_started_ = false;
  bool support_feature_v1_ = false;
  uint32_t feature_bits_ = 0;
};

class NetworkDeviceTests : public zxtest::Test,
                           public ddk::NetworkDeviceIfcProtocol<NetworkDeviceTests> {
 public:
  static constexpr uint8_t kVmoId = 1;
  static constexpr buffer_metadata_t kFrameMetadata = {
      .port = NetworkDevice::kPortId,
      .frame_type = static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet),
  };
  static constexpr size_t kVmoFrameCount = 256;

  NetworkDeviceTests() {
    // NB: Basic infallible set up is done in the constructor so subclasses can
    // override set up behavior more easily.
    auto backend = std::make_unique<FakeBackendForNetdeviceTest>();
    backend_ = backend.get();
    zx::bti bti(ZX_HANDLE_INVALID);
    fake_bti_create(bti.reset_and_get_address());
    fake_parent_ = MockDevice::FakeRootParent();
    device_ =
        std::make_unique<NetworkDevice>(fake_parent_.get(), std::move(bti), std::move(backend));
  }

  void SetUp() override {
    ASSERT_OK(device_->Init());
    const network_device_ifc_protocol_t protocol = {
        .ops = &network_device_ifc_protocol_ops_,
        .ctx = this,
    };
    ASSERT_OK(device_->NetworkDeviceImplInit(&protocol));
    ASSERT_TRUE(port_.is_valid());
    mac_addr_protocol_t mac_proto;
    port_.GetMac(&mac_proto);
    mac_ = ddk::MacAddrProtocolClient(&mac_proto);
    ASSERT_TRUE(mac_.is_valid());
  }

  void TearDown() override {
    device_->DdkAsyncRemove();
    ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_parent_.get()));
    // Release must have released the memory for us.
    __UNUSED NetworkDevice* ptr = device_.release();
  }

  void PrepareVmo() {
    ASSERT_FALSE(vmo_.is_valid());
    ASSERT_OK(zx::vmo::create(NetworkDevice::kFrameSize * kVmoFrameCount, 0, &vmo_));
    zx::vmo device_vmo;
    ASSERT_OK(vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &device_vmo));
    std::optional<zx_status_t> prepare_result;
    device().NetworkDeviceImplPrepareVmo(
        kVmoId, std::move(device_vmo),
        [](void* cookie, zx_status_t status) {
          *reinterpret_cast<std::optional<zx_status_t>*>(cookie) = status;
        },
        &prepare_result);
    ASSERT_TRUE(prepare_result.has_value());
    ASSERT_OK(prepare_result.value());
  }

  void StartDevice() {
    std::optional<zx_status_t> start_result;
    device().NetworkDeviceImplStart(
        [](void* cookie, zx_status_t status) {
          *reinterpret_cast<std::optional<zx_status_t>*>(cookie) = status;
        },
        &start_result);
    ASSERT_TRUE(start_result.has_value());
    ASSERT_OK(start_result.value());
  }

  // NetworkDevice interface implementation.
  void NetworkDeviceIfcPortStatusChanged(uint8_t port_id, const port_status_t* new_status) {
    EXPECT_EQ(port_id, NetworkDevice::kPortId);
    port_status_queue_.push(*new_status);
  }
  void NetworkDeviceIfcAddPort(uint8_t port_id, const network_port_protocol_t* port) {
    EXPECT_EQ(port_id, NetworkDevice::kPortId);
    ASSERT_FALSE(port_.is_valid());
    port_ = ddk::NetworkPortProtocolClient(port);
  }
  void NetworkDeviceIfcRemovePort(uint8_t port_id) { ADD_FAILURE("Port should never be removed"); }
  void NetworkDeviceIfcCompleteRx(const rx_buffer_t* rx_list, size_t rx_count) {
    for (auto rx : cpp20::span(rx_list, rx_count)) {
      ASSERT_EQ(rx.data_count, 1);
      complete_rx_queue_.push(CompleteRx{
          .meta = rx.meta,
          .part = rx.data_list[0],
      });
    }
  }
  void NetworkDeviceIfcCompleteTx(const tx_result_t* tx_list, size_t tx_count) {
    for (auto tx : cpp20::span(tx_list, tx_count)) {
      complete_tx_queue_.push(tx);
    }
  }
  void NetworkDeviceIfcSnoop(const rx_buffer_t* rx_list, size_t rx_count) {
    ADD_FAILURE("Snoop should never be called, only auto snoop expected");
  }

  struct CompleteRx {
    buffer_metadata_t meta;
    rx_buffer_part part;
  };

  std::optional<CompleteRx> PopRx() { return PopHelper(complete_rx_queue_); }
  std::optional<tx_result_t> PopTx() { return PopHelper(complete_tx_queue_); }
  std::optional<port_status_t> PopStatus() { return PopHelper(port_status_queue_); }

  NetworkDevice& device() { return *device_; }
  ddk::NetworkPortProtocolClient& port() { return port_; }
  ddk::MacAddrProtocolClient& mac() { return mac_; }
  FakeBackendForNetdeviceTest& backend() { return *backend_; }
  zx::vmo& vmo() { return vmo_; }
  // Unsafe access into vrings.
  vring& tx_vring() __TA_NO_THREAD_SAFETY_ANALYSIS { return device().tx_.vring_unsafe(); }
  vring& rx_vring() __TA_NO_THREAD_SAFETY_ANALYSIS { return device().rx_.vring_unsafe(); }

 private:
  template <typename T>
  static std::optional<T> PopHelper(std::queue<T>& queue) {
    if (queue.empty()) {
      return std::nullopt;
    }
    T ret = queue.front();
    queue.pop();
    return ret;
  }

  zx::vmo vmo_;
  std::shared_ptr<MockDevice> fake_parent_;
  std::unique_ptr<NetworkDevice> device_;
  FakeBackendForNetdeviceTest* backend_;
  ddk::NetworkPortProtocolClient port_;
  ddk::MacAddrProtocolClient mac_;
  std::queue<port_status_t> port_status_queue_;
  std::queue<CompleteRx> complete_rx_queue_;
  std::queue<tx_result_t> complete_tx_queue_;
};

class VirtioVersionTests : public NetworkDeviceTests, public zxtest::WithParamInterface<bool> {
 public:
  void SetUp() override {
    backend().SetSupportFeatureV1(IsV1Virtio());
    NetworkDeviceTests::SetUp();
  }

  bool IsV1Virtio() { return GetParam(); }
};

TEST_F(NetworkDeviceTests, PortGetStatus) {
  port_status_t status;
  port().GetStatus(&status);
  EXPECT_EQ(status.mtu, NetworkDevice::kMtu);
  EXPECT_EQ(status.flags,
            static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags::kOnline));
  backend().SetLinkDown();
  port().GetStatus(&status);
  EXPECT_EQ(status.mtu, NetworkDevice::kMtu);
  EXPECT_EQ(status.flags, static_cast<uint32_t>(fuchsia_hardware_network::wire::StatusFlags()));
}

TEST_F(NetworkDeviceTests, MacGetAddr) {
  fuchsia_net::wire::MacAddress addr;
  mac().GetAddress(addr.octets.data());
  EXPECT_BYTES_EQ(addr.octets.data(), FakeBackendForNetdeviceTest::kMac, addr.octets.size());
}

TEST_P(VirtioVersionTests, Start) {
  EXPECT_FALSE(backend().rx_ring_started());
  EXPECT_FALSE(backend().tx_ring_started());
  EXPECT_EQ(backend().DeviceState(), FakeBackend::State::DEVICE_STATUS_ACK);

  ASSERT_NO_FATAL_FAILURE(StartDevice());

  EXPECT_TRUE(backend().rx_ring_started());
  EXPECT_TRUE(backend().tx_ring_started());
  ASSERT_EQ(backend().DeviceState(), FakeBackend::State::DRIVER_OK);
  if (IsV1Virtio()) {
    EXPECT_EQ(backend().feature_bits(), VIRTIO_F_VERSION_1);
  } else {
    EXPECT_EQ(backend().feature_bits(), 0);
  }
}

TEST_F(NetworkDeviceTests, Stop) {
  ASSERT_NO_FATAL_FAILURE(StartDevice());
  ASSERT_NO_FATAL_FAILURE(PrepareVmo());

  constexpr buffer_region_t kDummyRegion = {
      .vmo = kVmoId,
      .offset = 0,
      .length = NetworkDevice::kFrameSize,
  };
  const rx_space_buffer_t rx_spaces[] = {
      {.id = 1, .region = kDummyRegion},
      {.id = 2, .region = kDummyRegion},
  };
  const tx_buffer_t tx_buffers[] = {
      {
          .id = 1,
          .data_list = &kDummyRegion,
          .data_count = 1,
          .meta = kFrameMetadata,
          .head_length = device().virtio_header_len(),
      },
      {
          .id = 2,
          .data_list = &kDummyRegion,
          .data_count = 1,
          .meta = kFrameMetadata,
          .head_length = device().virtio_header_len(),
      },
  };

  // Queue some rx and tx buffers so we observe them being returned on stop.
  device().NetworkDeviceImplQueueRxSpace(rx_spaces, std::size(rx_spaces));
  device().NetworkDeviceImplQueueTx(tx_buffers, std::size(tx_buffers));
  {
    bool callback_called = false;
    device().NetworkDeviceImplStop([](void* cookie) { *reinterpret_cast<bool*>(cookie) = true; },
                                   &callback_called);
    EXPECT_TRUE(callback_called);
  }

  EXPECT_EQ(backend().DeviceState(), FakeBackend::State::DEVICE_RESET);

  for (const auto& space : rx_spaces) {
    SCOPED_TRACE(fxl::StringPrintf("rx space %d", space.id));
    std::optional rx = PopRx();
    ASSERT_TRUE(rx.has_value());
    const CompleteRx& complete = rx.value();
    EXPECT_EQ(complete.part.id, space.id);
    EXPECT_EQ(complete.part.offset, 0);
    EXPECT_EQ(complete.part.length, 0);
  }
  {
    std::optional final = PopRx();
    ASSERT_FALSE(final.has_value(), "extra complete buffer with id %d", final.value().part.id);
  }
  for (const auto& tx_sent : tx_buffers) {
    SCOPED_TRACE(fxl::StringPrintf("tx sent %d", tx_sent.id));
    std::optional tx = PopTx();
    ASSERT_TRUE(tx.has_value());
    const tx_result_t& result = tx.value();
    EXPECT_EQ(result.id, tx_sent.id);
    EXPECT_STATUS(result.status, ZX_ERR_BAD_STATE);
  }
  {
    std::optional final = PopTx();
    ASSERT_FALSE(final.has_value(), "extra complete buffer with id %d", final.value().id);
  }
}

TEST_F(NetworkDeviceTests, UpdateStatus) {
  const struct {
    const char* name;
    fit::function<void()> set_state;
    fuchsia_hardware_network::wire::StatusFlags expect;
  } kTests[] = {
      {
          .name = "link down",
          .set_state = fit::bind_member(&backend(), &FakeBackendForNetdeviceTest::SetLinkDown),
          .expect = fuchsia_hardware_network::wire::StatusFlags(),
      },
      {
          .name = "link up",
          .set_state = fit::bind_member(&backend(), &FakeBackendForNetdeviceTest::SetLinkUp),
          .expect = fuchsia_hardware_network::wire::StatusFlags::kOnline,
      },
  };
  for (const auto& test : kTests) {
    SCOPED_TRACE(test.name);
    test.set_state();
    device().IrqConfigChange();
    std::optional status = PopStatus();
    ASSERT_TRUE(status.has_value());
    const port_status_t& observed = status.value();
    EXPECT_EQ(observed.mtu, NetworkDevice::kMtu);
    EXPECT_EQ(observed.flags, static_cast<uint32_t>(test.expect));
  }
}

TEST_P(VirtioVersionTests, Rx) {
  ASSERT_NO_FATAL_FAILURE(StartDevice());
  ASSERT_NO_FATAL_FAILURE(PrepareVmo());
  const rx_space_buffer_t rx_space[] = {
      {
          .id = 1,
          .region =
              {
                  .vmo = kVmoId,
                  .offset = 0,
                  .length = NetworkDevice::kFrameSize,
              },
      },
      {
          .id = 2,
          .region =
              {
                  .vmo = kVmoId,
                  .offset = NetworkDevice::kFrameSize,
                  .length = NetworkDevice::kFrameSize,
              },
      },
  };
  device().NetworkDeviceImplQueueRxSpace(rx_space, std::size(rx_space));
  EXPECT_TRUE(backend().IsQueueKicked(NetworkDevice::kRxId));

  // Check build descriptors and write into registers as the device does.
  vring& vring = rx_vring();
  size_t avail_ring_offset = std::size(rx_space);
  constexpr uint32_t kReceivedLenMultiplier = 10;
  for (const auto& space : rx_space) {
    uint16_t desc_idx = vring.avail->ring[vring.avail->idx - avail_ring_offset--];
    vring_desc& desc = vring.desc[desc_idx];
    EXPECT_EQ(desc.flags, VRING_DESC_F_WRITE);
    EXPECT_EQ(desc.len, space.region.length);
    EXPECT_EQ(desc.addr, FAKE_BTI_PHYS_ADDR + space.region.offset);
    EXPECT_EQ(desc.next, 0);

    vring.used->ring[vring.used->idx++] = {
        .id = desc_idx,
        .len = device().virtio_header_len() + space.id * kReceivedLenMultiplier,
    };
  }

  // Call irq handler and verify all buffer are returned.
  device().IrqRingUpdate();
  for (const auto& expect : rx_space) {
    SCOPED_TRACE(fxl::StringPrintf("expect id %d", expect.id));
    std::optional rx = PopRx();
    ASSERT_TRUE(rx.has_value());
    const CompleteRx& result = rx.value();
    EXPECT_EQ(result.part.id, expect.id);
    EXPECT_EQ(result.part.offset, device().virtio_header_len());
    EXPECT_EQ(result.part.length, expect.id * kReceivedLenMultiplier);

    EXPECT_EQ(result.meta.info_type,
              static_cast<uint32_t>(fuchsia_hardware_network::wire::InfoType::kNoInfo));
    EXPECT_EQ(result.meta.frame_type,
              static_cast<uint8_t>(fuchsia_hardware_network::wire::FrameType::kEthernet));
    EXPECT_EQ(result.meta.flags, 0);
    EXPECT_EQ(result.meta.port, NetworkDevice::kPortId);
  }
}

TEST_P(VirtioVersionTests, Tx) {
  ASSERT_NO_FATAL_FAILURE(StartDevice());
  ASSERT_NO_FATAL_FAILURE(PrepareVmo());
  const uint16_t header_len = device().virtio_header_len();
  const buffer_region_t buffer_regions[] = {
      {
          .vmo = kVmoId,
          .offset = 0,
          .length = static_cast<uint64_t>(header_len + 25),
      },
      {
          .vmo = kVmoId,
          .offset = NetworkDevice::kFrameSize,
          .length = static_cast<uint64_t>(header_len + 88),
      },
  };
  const tx_buffer_t tx_buffers[] = {
      {
          .id = 1,
          .data_list = &buffer_regions[0],
          .data_count = 1,
          .meta = kFrameMetadata,
          .head_length = header_len,
      },
      {
          .id = 2,
          .data_list = &buffer_regions[1],
          .data_count = 1,
          .meta = kFrameMetadata,
          .head_length = header_len,
      },
  };
  constexpr uint8_t kInitValue = 0xAA;
  {
    std::array<uint8_t, sizeof(virtio_net_hdr_t) + 1> header;
    header.fill(kInitValue);
    // Write garbage to the VMO where virtio headers are inserted.
    for (const auto& region : buffer_regions) {
      ASSERT_OK(vmo().write(header.data(), region.offset, header.size()));
    }
  }
  device().NetworkDeviceImplQueueTx(tx_buffers, std::size(tx_buffers));
  EXPECT_TRUE(backend().IsQueueKicked(NetworkDevice::kTxId));

  for (const auto& region : buffer_regions) {
    SCOPED_TRACE(fxl::StringPrintf("region at %zu", region.offset));
    virtio_net_hdr_t header;
    ASSERT_OK(vmo().read(reinterpret_cast<uint8_t*>(&header), region.offset,
                         device().virtio_header_len()));
    EXPECT_EQ(header.base.flags, 0);
    EXPECT_EQ(header.base.gso_type, 0);
    EXPECT_EQ(header.base.hdr_len, 0);
    EXPECT_EQ(header.base.gso_size, 0);
    EXPECT_EQ(header.base.csum_start, 0);
    EXPECT_EQ(header.base.csum_offset, 0);

    if (IsV1Virtio()) {
      EXPECT_EQ(header.num_buffers, 0);
    } else {
      // Num buffers is not present if the V1 feature flag is not set, this
      // should be considered part of the payload.
      union {
        uint16_t value;
        std::array<uint8_t, sizeof(uint16_t)> bytes;
      } v;
      v.bytes.fill(kInitValue);
      EXPECT_EQ(header.num_buffers, v.value);
    }

    // The byte immediately after the header is payload, and thus should not
    // have been touched.
    uint8_t next;
    ASSERT_OK(vmo().read(&next, region.offset + device().virtio_header_len(), 1));
    EXPECT_EQ(next, kInitValue);
  }

  // Check build descriptors and write into registers as the device does.
  vring& vring = tx_vring();
  size_t avail_ring_offset = std::size(tx_buffers);
  for (const auto& tx_buffer : tx_buffers) {
    uint16_t desc_idx = vring.avail->ring[vring.avail->idx - avail_ring_offset--];
    vring_desc& desc = vring.desc[desc_idx];
    const buffer_region_t& region = tx_buffer.data_list[0];
    EXPECT_EQ(desc.flags, 0);
    EXPECT_EQ(desc.len, region.length);
    EXPECT_EQ(desc.addr, FAKE_BTI_PHYS_ADDR + region.offset);
    EXPECT_EQ(desc.next, 0);

    vring.used->ring[vring.used->idx++] = {
        .id = desc_idx,
    };
  }
  // Call irq handler and verify all buffer are returned.
  device().IrqRingUpdate();
  for (const auto& expect : tx_buffers) {
    SCOPED_TRACE(fxl::StringPrintf("expect id %d", expect.id));
    std::optional tx = PopTx();
    ASSERT_TRUE(tx.has_value());
    const tx_result_t& result = tx.value();
    EXPECT_EQ(result.id, expect.id);
    EXPECT_OK(result.status);
  }
}

INSTANTIATE_TEST_SUITE_P(NetworkDeviceTests, VirtioVersionTests, zxtest::Values(true, false),
                         [](const zxtest::TestParamInfo<VirtioVersionTests::ParamType>& info) {
                           if (info.param) {
                             return "V1Feature";
                           }
                           return "NoV1Feature";
                         });

}  // namespace virtio
