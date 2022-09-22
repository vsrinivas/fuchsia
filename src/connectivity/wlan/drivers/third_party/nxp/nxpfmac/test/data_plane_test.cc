// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/data_plane.h"

#include <arpa/inet.h>
#include <lib/mock-function/mock-function.h>

#include <atomic>
#include <memory>
#include <thread>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mlan_mocks.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/mock_bus.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/test_data_plane.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/wlan_interface.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

using wlan::nxpfmac::DataPlane;

namespace {

constexpr uint8_t kVmoId = 0;

struct MockDataPlaneIfc : public wlan::nxpfmac::DataPlaneIfc {
  void OnEapolTransmitted(wlan::drivers::components::Frame&& frame, zx_status_t status) override {
    on_eapol_transmitted_.Call(std::move(frame), status);
  }
  void OnEapolReceived(wlan::drivers::components::Frame&& frame) override {
    on_eapol_received_.Call(std::move(frame));
  }

  mock_function::MockFunction<void, wlan::drivers::components::Frame&&, zx_status_t>
      on_eapol_transmitted_;
  mock_function::MockFunction<void, wlan::drivers::components::Frame&&> on_eapol_received_;
};

struct DataPlaneTest : public zxtest::Test {
  void SetUp() override {
    ASSERT_OK(wlan::nxpfmac::TestDataPlane::Create(&ifc_, &bus_, mlan_mocks_.GetAdapter(),
                                                   &test_data_plane_));
    data_plane_ = test_data_plane_->GetDataPlane();
    net_device_ = test_data_plane_->GetNetDevice();
    ASSERT_OK(device_get_protocol(net_device_, ZX_PROTOCOL_NETWORK_DEVICE_IMPL, &netdev_proto_));
    ASSERT_OK(
        network_device_impl_init(&netdev_proto_, netdev_ifc_proto_.ctx, netdev_ifc_proto_.ops));
    // Ensure that the netdevice has prepared a VMO, otherwise it won't complete TX or RX frames.
    ASSERT_OK(zx::vmo::create(2048, 0, &netdev_vmo_));
    network_device_impl_prepare_vmo_callback callback = [](void*, int) {};
    network_device_impl_prepare_vmo(&netdev_proto_, kVmoId, netdev_vmo_.get(), callback, nullptr);
  }

  static void OnCompleteRx(void* ctx, const rx_buffer_t* rx_list, size_t rx_count) {
    auto test = static_cast<DataPlaneTest*>(ctx);
    test->complete_rx_.Call(ctx, rx_list, rx_count);
  }

  static void OnCompleteTx(void* ctx, const tx_result_t* tx_list, size_t tx_count) {
    auto test = static_cast<DataPlaneTest*>(ctx);
    test->complete_tx_.Call(ctx, tx_list, tx_count);
  }

  std::unique_ptr<wlan::nxpfmac::TestDataPlane> test_data_plane_;
  DataPlane* data_plane_ = nullptr;
  std::unique_ptr<std::thread> async_remove_watcher_;
  std::atomic<bool> async_remove_watcher_running_{true};

  zx_device* net_device_ = nullptr;
  network_device_ifc_protocol_ops_t netdev_ifc_proto_ops_{.complete_rx = &OnCompleteRx,
                                                          .complete_tx = &OnCompleteTx};
  network_device_ifc_protocol_t netdev_ifc_proto_{.ops = &netdev_ifc_proto_ops_, .ctx = this};
  network_device_impl_protocol_t netdev_proto_;
  mock_function::MockFunction<void, void*, const rx_buffer_t*, size_t> complete_rx_;
  mock_function::MockFunction<void, void*, const tx_result_t*, size_t> complete_tx_;
  zx::vmo netdev_vmo_;

  MockDataPlaneIfc ifc_;
  wlan::nxpfmac::MockBus bus_;
  wlan::nxpfmac::MlanMockAdapter mlan_mocks_;
};

TEST_F(DataPlaneTest, HasNetDeviceProtocol) {
  // Test that a DataPlane object creates a NetworkDeviceImpl device.

  zx_device* net_device = test_data_plane_->GetNetDevice();
  network_device_impl_protocol_t proto;
  ASSERT_OK(device_get_protocol(net_device, ZX_PROTOCOL_NETWORK_DEVICE_IMPL, &proto));
}

TEST_F(DataPlaneTest, DeferRxWork) {
  // Test that the data plane correctly defers RX work and calls mlan_rx_process.

  sync_completion_t mlan_rx_process_called;
  mlan_mocks_.SetOnMlanRxProcess([&](t_void*, t_u8*) {
    sync_completion_signal(&mlan_rx_process_called);
    return MLAN_STATUS_SUCCESS;
  });

  data_plane_->DeferRxWork();
  ASSERT_OK(sync_completion_wait(&mlan_rx_process_called, ZX_TIME_INFINITE));
}

TEST_F(DataPlaneTest, HasNetDeviceProto) {
  // Test that the data plane present a working network device ifc protocol.

  const network_device_ifc_protocol_t netdev_ifc_proto = data_plane_->NetDevIfcProto();
  ASSERT_NOT_NULL(netdev_ifc_proto.ctx);
  ASSERT_NOT_NULL(netdev_ifc_proto.ops);
}

TEST_F(DataPlaneTest, CompleteTxData) {
  // Test that calling CompleteTx on the data plane correctly propagates the data to net device.

  complete_tx_.ExpectCallWithMatcher(
      [&](void*, const tx_result_t* tx_list, size_t tx_count) { EXPECT_EQ(1u, tx_count); });

  wlan::drivers::components::Frame frame(nullptr, kVmoId, 0, 0, nullptr, 0, 0);
  data_plane_->CompleteTx(std::move(frame), ZX_OK);

  complete_tx_.VerifyAndClear();
}

TEST_F(DataPlaneTest, CompleteRxData) {
  // Test that calling CompleteRx on the data plane correctly propagates the data to net device.

  complete_rx_.ExpectCallWithMatcher(
      [&](void*, const rx_buffer_t* rx_list, size_t rx_count) { EXPECT_EQ(1u, rx_count); });

  wlan::drivers::components::Frame frame(nullptr, kVmoId, 0, 0, nullptr, 0, 0);
  data_plane_->CompleteRx(std::move(frame));

  complete_rx_.VerifyAndClear();
}

TEST_F(DataPlaneTest, CompleteTxEapolFrame) {
  // Test that calling CompleteTx with an EAPOL frame correctly calls the data plane interface and
  // does not propagate the frame to net device.

  ethhdr eapol_data{.h_dest{0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
                    .h_source{0x11, 0x12, 0x13, 0x14, 0x15, 0x16},
                    .h_proto = htons(ETH_P_PAE)};

  complete_tx_.ExpectNoCall();
  ifc_.on_eapol_transmitted_.ExpectCallWithMatcher(
      [&](wlan::drivers::components::Frame&& frame, zx_status_t status) {
        EXPECT_OK(status);
        EXPECT_EQ(sizeof(eapol_data), frame.Size());
        EXPECT_BYTES_EQ(&eapol_data, frame.Data(), frame.Size());
      });

  wlan::drivers::components::Frame frame(
      nullptr, kVmoId, 0, 0, reinterpret_cast<uint8_t*>(&eapol_data), sizeof(eapol_data), 0);
  data_plane_->CompleteTx(std::move(frame), ZX_OK);

  complete_tx_.VerifyAndClear();
  ifc_.on_eapol_transmitted_.VerifyAndClear();
}

TEST_F(DataPlaneTest, CompleteRxEapolFrame) {
  // Test that calling CompleteRx with an EAPOL frame correctly calls the data plane interface and
  // does not propagate the frame to net device.

  ethhdr eapol_data{.h_dest{0x21, 0x22, 0x23, 0x24, 0x25, 0x26},
                    .h_source{0x31, 0x32, 0x33, 0x34, 0x35, 0x36},
                    .h_proto = htons(ETH_P_PAE)};

  complete_rx_.ExpectNoCall();
  ifc_.on_eapol_received_.ExpectCallWithMatcher([&](wlan::drivers::components::Frame&& frame) {
    EXPECT_EQ(sizeof(eapol_data), frame.Size());
    EXPECT_BYTES_EQ(&eapol_data, frame.Data(), frame.Size());
  });

  wlan::drivers::components::Frame frame(
      nullptr, kVmoId, 0, 0, reinterpret_cast<uint8_t*>(&eapol_data), sizeof(eapol_data), 0);
  data_plane_->CompleteRx(std::move(frame));

  complete_rx_.VerifyAndClear();
  ifc_.on_eapol_received_.VerifyAndClear();
}

TEST_F(DataPlaneTest, GetInfo) {
  // Test that NetDevGetInfo return some kind of reasonable values.

  device_info_t info;
  data_plane_->NetDevGetInfo(&info);

  ASSERT_GT(info.tx_depth, 0);
  ASSERT_GT(info.rx_depth, 0);
  ASSERT_LT(info.rx_threshold, info.rx_depth);
  // NetDevice implementation isn't ready for multiple buffer parts yet.
  ASSERT_EQ(1u, info.max_buffer_parts);
  ASSERT_GT(info.max_buffer_length, 0);
  ASSERT_EQ(wlan::nxpfmac::kMockBusBufferAlignment, info.buffer_alignment);
  ASSERT_GT(info.min_rx_buffer_length, 0);
}

TEST_F(DataPlaneTest, QueueTx) {
  std::vector<pmlan_buffer> sent_packets;
  mlan_mocks_.SetOnMlanSendPacket([&](t_void*, pmlan_buffer buffer) -> mlan_status {
    sent_packets.push_back(buffer);
    return MLAN_STATUS_PENDING;
  });

  bool trigger_main_process_called = false;
  bus_.SetTriggerMainProcess([&]() -> zx_status_t {
    trigger_main_process_called = true;
    return ZX_OK;
  });

  constexpr size_t kHeadRoom = sizeof(wlan::drivers::components::Frame) + sizeof(mlan_buffer);
  uint8_t first_data[kHeadRoom + 1024];
  uint8_t second_data[kHeadRoom + 512];

  wlan::drivers::components::Frame frames[] = {
      wlan::drivers::components::Frame(nullptr, kVmoId, 0, 0, first_data, sizeof(first_data), 0),
      wlan::drivers::components::Frame(nullptr, kVmoId, 0, 0, second_data, sizeof(second_data), 1)};

  frames[0].ShrinkHead(kHeadRoom);
  frames[1].ShrinkHead(kHeadRoom);

  data_plane_->NetDevQueueTx(frames);

  ASSERT_EQ(std::size(frames), sent_packets.size());

  for (size_t i = 0; i < std::size(frames); ++i) {
    EXPECT_EQ(frames[i].PortId(), sent_packets[i]->bss_index);
    EXPECT_EQ(frames[i].Size(), sent_packets[i]->data_len);
    EXPECT_EQ(frames[i].Data(), sent_packets[i]->pbuf);
  }

  ASSERT_TRUE(trigger_main_process_called);
}

TEST_F(DataPlaneTest, PrepareVmo) {
  // Test that the data plane passes on NetDevPrepareVmo calls to the bus.

  uint8_t test_byte = 12;
  const zx_handle_t vmo_handle = netdev_vmo_.get();

  bool prepare_vmo_called = false;
  bus_.SetPrepareVmo(
      [&](uint8_t vmo_id, zx::vmo vmo, uint8_t* mapped_addr, size_t mapped_size) -> zx_status_t {
        EXPECT_EQ(kVmoId, vmo_id);
        EXPECT_EQ(vmo_handle, vmo.get());
        EXPECT_EQ(&test_byte, mapped_addr);
        EXPECT_EQ(sizeof(test_byte), mapped_size);
        prepare_vmo_called = true;
        return ZX_OK;
      });

  data_plane_->NetDevPrepareVmo(kVmoId, std::move(netdev_vmo_), &test_byte, sizeof(test_byte));

  ASSERT_TRUE(prepare_vmo_called);
}

TEST_F(DataPlaneTest, ReleaseVmo) {
  // Test that the data plane passes on NetDevReleaseVmo to the bus.

  constexpr uint8_t kTestVmoId = 42;

  bool release_vmo_called = false;
  bus_.SetReleaseVmo([&](uint8_t vmo_id) -> zx_status_t {
    EXPECT_EQ(kTestVmoId, vmo_id);
    release_vmo_called = true;
    return ZX_OK;
  });

  data_plane_->NetDevReleaseVmo(kTestVmoId);

  ASSERT_TRUE(release_vmo_called);
}

}  // namespace
