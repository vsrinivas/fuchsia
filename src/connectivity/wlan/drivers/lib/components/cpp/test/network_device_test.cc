// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/mock-function/mock-function.h>
#include <lib/stdcompat/span.h>

#include <functional>

#include <wlan/drivers/components/frame_container.h>
#include <wlan/drivers/components/frame_storage.h>
#include <wlan/drivers/components/network_device.h>
#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/lib/components/cpp/test/test_network_device_ifc.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {

constexpr const char kDriverName[] = "test-driver";

using wlan::drivers::components::Frame;
using wlan::drivers::components::FrameContainer;
using wlan::drivers::components::FrameStorage;
using wlan::drivers::components::NetworkDevice;

// Test implementation of the NetworkDevice base class we're testing
struct TestNetworkDevice : public NetworkDevice::Callbacks {
  explicit TestNetworkDevice(zx_device_t* parent) : network_device_(parent, this) {}
  void NetDevRelease() override {
    if (release_.HasExpectations()) {
      release_.Call();
    }
  }
  zx_status_t NetDevInit() override {
    if (init_.HasExpectations()) {
      return init_.Call();
    }
    return ZX_OK;
  }
  void NetDevStart(NetworkDevice::Callbacks::StartTxn txn) override {
    txn.Reply(ZX_OK);
    start_.Call(std::move(txn));
  }
  void NetDevStop(NetworkDevice::Callbacks::StopTxn txn) override {
    txn.Reply();
    stop_.Call(std::move(txn));
  }
  void NetDevGetInfo(device_info_t* out_info) override { get_info_.Call(out_info); }
  void NetDevQueueTx(cpp20::span<Frame> buffers) override { queue_tx_.Call(buffers); }
  void NetDevQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count,
                          uint8_t* vmo_addrs[]) override {
    queue_rx_space_.Call(buffers_list, buffers_count, vmo_addrs);
  }
  zx_status_t NetDevPrepareVmo(uint8_t vmo_id, zx::vmo vmo, uint8_t* mapped_addr,
                               size_t mapped_size) override {
    vmo_addrs_[vmo_id] = mapped_addr;
    prepare_vmo_.Call(vmo_id, std::move(vmo), mapped_addr, mapped_size);
    return ZX_OK;
  }
  void NetDevReleaseVmo(uint8_t vmo_id) override { release_vmo_.Call(vmo_id); }
  void NetDevSetSnoopEnabled(bool snoop) override { set_snoop_enabled_.Call(snoop); }

  mock_function::MockFunction<void> release_;
  mock_function::MockFunction<zx_status_t> init_;
  mock_function::MockFunction<void, NetworkDevice::Callbacks::StartTxn> start_;
  mock_function::MockFunction<void, NetworkDevice::Callbacks::StopTxn> stop_;
  mock_function::MockFunction<void, device_info_t*> get_info_;
  mock_function::MockFunction<void, cpp20::span<Frame>> queue_tx_;
  mock_function::MockFunction<void, const rx_space_buffer_t*, size_t, uint8_t**> queue_rx_space_;
  mock_function::MockFunction<void, uint8_t, zx::vmo&&, uint8_t*, size_t> prepare_vmo_;
  mock_function::MockFunction<void, uint8_t> release_vmo_;
  mock_function::MockFunction<void, bool> set_snoop_enabled_;

  std::array<uint8_t*, MAX_VMOS> vmo_addrs_ = {};

  NetworkDevice network_device_;
};

TEST(NetworkDeviceTest, Constructible) { TestNetworkDevice device(nullptr); }

TEST(NetworkDeviceTest, InitRelease) {
  auto parent = MockDevice::FakeRootParent();
  TestNetworkDevice device(parent.get());

  device.network_device_.Init(kDriverName);

  // Initializing the device should have added a device as a child of the parent
  ASSERT_EQ(parent->child_count(), 1u);

  auto child = *parent->children().begin();

  // Destroying the parent should release the device, make sure the call is propagated to the base
  device.release_.ExpectCall();
  parent.reset();
  device.release_.VerifyAndClear();
}

TEST(NetworkDeviceTest, PrepareVmo) {
  constexpr uint8_t kVmoId = 7;
  constexpr const char kTestData[] = "This is some test data, we're writing it to the VMO";
  auto parent = MockDevice::FakeRootParent();
  TestNetworkDevice device(parent.get());

  zx::vmo created_vmo;
  // The VMO size will be rounded up to page size so just make sure we already have a multiple of it
  // so tests will pass regardless of page size.
  const size_t kVmoSize = zx_system_get_page_size();
  zx::vmo::create(kVmoSize, 0, &created_vmo);

  ASSERT_EQ(created_vmo.write(kTestData, 0, sizeof(kTestData)), ZX_OK);

  device.prepare_vmo_.ExpectCallWithMatcher(
      [&](uint8_t vmo_id, zx::vmo prepared_vmo, uint8_t* mapped_addr, size_t mapped_size) {
        EXPECT_EQ(vmo_id, kVmoId);
        ASSERT_EQ(mapped_size, kVmoSize);
        // We should be able to read our test data from the mapped address
        ASSERT_STREQ(reinterpret_cast<const char*>(mapped_addr), kTestData);
      });

  zx_status_t status = ZX_ERR_UNAVAILABLE;
  auto callback = [](void* ctx, zx_status_t result) { *static_cast<zx_status_t*>(ctx) = result; };
  device.network_device_.NetworkDeviceImplPrepareVmo(kVmoId, std::move(created_vmo), callback,
                                                     &status);
  device.prepare_vmo_.VerifyAndClear();
  EXPECT_EQ(status, ZX_OK);
}

TEST(NetworkDeviceTest, PrepareInvalidVmo) {
  constexpr uint8_t kVmoId = 7;
  auto parent = MockDevice::FakeRootParent();
  TestNetworkDevice device(parent.get());

  // Preparing an invalid VMO handle should result in a callback with a failed status code.
  zx_status_t status = ZX_OK;
  auto callback = [](void* ctx, zx_status_t result) { *static_cast<zx_status_t*>(ctx) = result; };
  device.network_device_.NetworkDeviceImplPrepareVmo(kVmoId, zx::vmo(ZX_HANDLE_INVALID), callback,
                                                     &status);
  EXPECT_NE(status, ZX_OK);
}

TEST(NetworkDeviceTest, ReleaseVmo) {
  constexpr uint8_t kVmoId = 9;
  auto parent = MockDevice::FakeRootParent();
  TestNetworkDevice device(parent.get());

  zx::vmo created_vmo;
  // The VMO size will be rounded up to page size so just make sure we already have a multiple of it
  // so tests will pass regardless of page size.
  const size_t kVmoSize = zx_system_get_page_size();
  zx::vmo::create(kVmoSize, 0, &created_vmo);

  device.prepare_vmo_.ExpectCallWithMatcher(
      [&](uint8_t vmo_id, zx::vmo, uint8_t*, size_t) { ASSERT_EQ(vmo_id, kVmoId); });
  device.network_device_.NetworkDeviceImplPrepareVmo(
      kVmoId, std::move(created_vmo), [](void*, zx_status_t) {}, nullptr);

  device.release_vmo_.ExpectCall(kVmoId);
  device.network_device_.NetworkDeviceImplReleaseVmo(kVmoId);
  device.release_vmo_.VerifyAndClear();
}

TEST(NetworkDeviceTest, Start) {
  auto parent = MockDevice::FakeRootParent();
  TestNetworkDevice device(parent.get());
  network_device_impl_start_callback callback = [](void*, zx_status_t) {};
  void* cookie = &callback;
  device.start_.ExpectCallWithMatcher([](NetworkDevice::Callbacks::StartTxn) {});
  device.network_device_.NetworkDeviceImplStart(callback, cookie);
  device.start_.VerifyAndClear();
}

TEST(NetworkDeviceTest, Stop) {
  auto parent = MockDevice::FakeRootParent();
  TestNetworkDevice device(parent.get());
  network_device_impl_stop_callback callback = [](void*) {};
  void* cookie = &callback;
  device.stop_.ExpectCallWithMatcher([](NetworkDevice::Callbacks::StopTxn) {});
  device.network_device_.NetworkDeviceImplStop(callback, cookie);
  device.stop_.VerifyAndClear();
}

TEST(NetworkDeviceTest, NetDevIfcProto) {
  auto parent = MockDevice::FakeRootParent();
  TestNetworkDevice device(parent.get());
  wlan::drivers::components::test::TestNetworkDeviceIfc netdev_ifc;
  network_device_ifc_protocol_t proto = netdev_ifc.GetProto();

  ASSERT_OK(device.network_device_.NetworkDeviceImplInit(&proto));

  EXPECT_EQ(device.network_device_.NetDevIfcProto().ctx, proto.ctx);
  EXPECT_EQ(device.network_device_.NetDevIfcProto().ops, proto.ops);
}

struct NetworkDeviceTestFixture : public ::zxtest::Test {
  static constexpr uint8_t kVmoId = 13;
  static constexpr uint8_t kPortId = 8;

  NetworkDeviceTestFixture() : parent_(MockDevice::FakeRootParent()), device_(parent_.get()) {}

  void SetUp() override {
    kVmoSize = zx_system_get_page_size();
    device_.network_device_.NetworkDeviceImplInit(&netdev_ifc_.GetProto());
    // Make sure we have these correct or tests are gonna start crashing.
    ASSERT_EQ(device_.network_device_.NetDevIfcProto().ctx, netdev_ifc_.GetProto().ctx);
    ASSERT_EQ(device_.network_device_.NetDevIfcProto().ops, netdev_ifc_.GetProto().ops);

    // Some of the operations in NetworkDevice require an actual VMO, create and prepare one.
    ASSERT_EQ(zx::vmo::create(kVmoSize, 0, &vmo_), ZX_OK);
    // There needs to be an expectation for any call that happens so expect this prepare.
    device_.prepare_vmo_.ExpectCallWithMatcher(
        [&](uint8_t vmo_id, zx::vmo, uint8_t*, size_t) { ASSERT_EQ(vmo_id, kVmoId); });
    device_.network_device_.NetworkDeviceImplPrepareVmo(
        kVmoId, std::move(vmo_), [](void*, zx_status_t) {}, nullptr);

    // Make sure the device starts.
    bool started = false;
    device_.start_.ExpectCallWithMatcher([](NetworkDevice::Callbacks::StartTxn) {});
    device_.network_device_.NetworkDeviceImplStart(
        [](void* cookie, zx_status_t) { *reinterpret_cast<bool*>(cookie) = true; }, &started);
    ASSERT_TRUE(started);

    std::lock_guard lock(frame_storage_);
    frame_storage_.Store(CreateTestFrame(0, 256, &frame_storage_));
    frame_storage_.Store(CreateTestFrame(0, 256, &frame_storage_));
    frame_storage_.Store(CreateTestFrame(0, 256, &frame_storage_));
  }

  // In order to be able to use ASSERT it seems the method has to return void. So do this little
  // song and dance to have a method that's easy to use while being able to ASSERT that its
  // parameters are valid.
  Frame CreateTestFrame(size_t offset = 0, uint32_t size = 256, FrameStorage* storage = nullptr) {
    Frame frame;
    CreateTestFrameImpl(offset, size, storage, &frame);
    return frame;
  }

  static void FillTxBuffers(tx_buffer_t* buffers, buffer_region_t* regions, size_t count,
                            size_t frame_size = 256, uint16_t head_length = 0,
                            uint16_t tail_length = 0) {
    size_t vmo_offset = 0;
    uint32_t buffer_id = 0;
    for (size_t i = 0; i < count; ++i) {
      regions[i].length = frame_size;
      regions[i].vmo = kVmoId;
      regions[i].offset = vmo_offset;
      vmo_offset += frame_size;
      buffers[i] = {};
      buffers[i].data_count = 1;
      buffers[i].data_list = &regions[i];
      buffers[i].head_length = head_length;
      buffers[i].tail_length = tail_length;
      buffers[i].meta.port = kPortId;
      buffers[i].id = buffer_id++;
    }
  }

  FrameContainer AcquireTestFrames(size_t count) {
    std::lock_guard lock(frame_storage_);
    return frame_storage_.Acquire(count);
  }

  size_t StorageSize() {
    std::lock_guard lock(frame_storage_);
    return frame_storage_.size();
  }

  std::shared_ptr<MockDevice> parent_;
  TestNetworkDevice device_;
  wlan::drivers::components::test::TestNetworkDeviceIfc netdev_ifc_;
  zx::vmo vmo_;
  size_t kVmoSize;
  FrameStorage frame_storage_;

 private:
  void CreateTestFrameImpl(size_t offset, uint32_t size, FrameStorage* storage, Frame* frame) {
    ASSERT_LE(offset + size, kVmoSize);
    static uint16_t buffer_id = 1;
    *frame = Frame(storage, kVmoId, offset, buffer_id++, device_.vmo_addrs_[kVmoId], size, kPortId);
  }
};

TEST_F(NetworkDeviceTestFixture, CompleteTx) {
  FrameContainer frames = AcquireTestFrames(3);

  netdev_ifc_.complete_tx_.ExpectCallWithMatcher([&](const tx_result_t* results, size_t count) {
    ASSERT_EQ(count, 3u);
    EXPECT_EQ(results[0].id, frames[0].BufferId());
    EXPECT_EQ(results[0].status, ZX_OK);
    EXPECT_EQ(results[1].id, frames[1].BufferId());
    EXPECT_EQ(results[1].status, ZX_OK);
    EXPECT_EQ(results[2].id, frames[2].BufferId());
    EXPECT_EQ(results[2].status, ZX_OK);
  });

  device_.network_device_.CompleteTx(frames, ZX_OK);
  netdev_ifc_.complete_tx_.VerifyAndClear();
}

TEST_F(NetworkDeviceTestFixture, CompleteTxInvalidVmos) {
  FrameContainer frames = AcquireTestFrames(3);

  // Replace with a frame with an invalid VMO id, this frame should not be completed
  frames[0] = Frame(nullptr, kVmoId + 1, 0, 100, device_.vmo_addrs_[kVmoId], 256, kPortId);

  // Make sure we don't accidentally picked the same buffer ID
  ASSERT_NE(frames[0].BufferId(), frames[1].BufferId());
  ASSERT_NE(frames[1].BufferId(), frames[2].BufferId());

  netdev_ifc_.complete_tx_.ExpectCallWithMatcher([&](const tx_result_t* results, size_t count) {
    // Size should only be 2, the first frame should not have completed
    ASSERT_EQ(count, 2u);
    EXPECT_EQ(results[0].id, frames[1].BufferId());
    EXPECT_EQ(results[0].status, ZX_OK);
    EXPECT_EQ(results[1].id, frames[2].BufferId());
    EXPECT_EQ(results[1].status, ZX_OK);
  });
  device_.network_device_.CompleteTx(frames, ZX_OK);
  netdev_ifc_.complete_tx_.VerifyAndClear();
}

TEST_F(NetworkDeviceTestFixture, CompleteTxNoFrames) {
  FrameContainer frames;
  // Should not have been called because there were no frames to complete
  netdev_ifc_.complete_tx_.ExpectNoCall();
  device_.network_device_.CompleteTx(frames, ZX_OK);
  netdev_ifc_.complete_tx_.VerifyAndClear();
}

TEST_F(NetworkDeviceTestFixture, CompleteTxStatusPropagates) {
  constexpr zx_status_t kStatus = ZX_ERR_NO_RESOURCES;
  FrameContainer frames = AcquireTestFrames(3);

  netdev_ifc_.complete_tx_.ExpectCallWithMatcher([&](const tx_result_t* results, size_t count) {
    ASSERT_EQ(count, 3u);
    EXPECT_EQ(results[0].status, kStatus);
    EXPECT_EQ(results[1].status, kStatus);
    EXPECT_EQ(results[2].status, kStatus);
  });

  device_.network_device_.CompleteTx(frames, kStatus);

  netdev_ifc_.complete_tx_.VerifyAndClear();
}

TEST_F(NetworkDeviceTestFixture, CompleteRx) {
  size_t storage_size_after_acquire = 0;
  {
    FrameContainer frames = AcquireTestFrames(3);
    storage_size_after_acquire = StorageSize();

    netdev_ifc_.complete_rx_.ExpectCallWithMatcher([&](const rx_buffer_t* buffers, size_t count) {
      auto verifyBuffer = [](const rx_buffer_t& buffer, const Frame& frame) {
        ASSERT_EQ(buffer.data_count, 1u);
        EXPECT_EQ(buffer.meta.port, frame.PortId());
        EXPECT_EQ(buffer.data_list[0].id, frame.BufferId());
        EXPECT_EQ(buffer.data_list[0].length, frame.Size());
        EXPECT_EQ(buffer.data_list[0].offset, frame.Headroom());
      };
      ASSERT_EQ(count, 3u);
      verifyBuffer(buffers[0], frames[0]);
      verifyBuffer(buffers[1], frames[1]);
      verifyBuffer(buffers[2], frames[2]);
    });

    device_.network_device_.CompleteRx(std::move(frames));
    netdev_ifc_.complete_rx_.VerifyAndClear();
  }
  std::lock_guard lock(frame_storage_);
  // The frames should not have been returned to storage
  EXPECT_EQ(frame_storage_.size(), storage_size_after_acquire);
}

TEST_F(NetworkDeviceTestFixture, CompleteRxIgnoreInvalidVmoId) {
  size_t storage_size_after_acquire = 0;
  {
    FrameContainer frames = AcquireTestFrames(3);
    storage_size_after_acquire = StorageSize();

    // Replace the frame with something that has an invalid VMO id, this frame should be discarded
    frames[0] = Frame(nullptr, kVmoId + 1, 0, 0, device_.vmo_addrs_[kVmoId], 256, kPortId);

    netdev_ifc_.complete_rx_.ExpectCallWithMatcher([&](const rx_buffer_t* buffers, size_t count) {
      // Only two results should be present
      ASSERT_EQ(count, 2u);
      EXPECT_EQ(buffers[0].data_list[0].id, frames[1].BufferId());
      EXPECT_EQ(buffers[1].data_list[0].id, frames[2].BufferId());
    });

    device_.network_device_.CompleteRx(std::move(frames));
    netdev_ifc_.complete_rx_.VerifyAndClear();
  }
  std::lock_guard lock(frame_storage_);
  // The frame with invalid VMO id should have been returned to storage, only frames that truly
  // completed should be released.
  EXPECT_EQ(frame_storage_.size(), storage_size_after_acquire + 1);
}

TEST_F(NetworkDeviceTestFixture, CompleteRxEmptyFrames) {
  size_t storage_size_after_acquire = 0;
  {
    FrameContainer frames = AcquireTestFrames(3);
    storage_size_after_acquire = StorageSize();
    // Set the size of one frame to 0, this should make it so that it's not reported back to netdev.
    frames[1].SetSize(0);

    netdev_ifc_.complete_rx_.ExpectCallWithMatcher([&](const rx_buffer_t* buffers, size_t count) {
      // All results should be present
      ASSERT_EQ(count, 3u);
      EXPECT_EQ(buffers[0].data_list[0].id, frames[0].BufferId());
      EXPECT_EQ(buffers[1].data_list[0].id, frames[1].BufferId());
      EXPECT_EQ(buffers[2].data_list[0].id, frames[2].BufferId());
    });

    device_.network_device_.CompleteRx(std::move(frames));
    netdev_ifc_.complete_rx_.VerifyAndClear();
  }
  std::lock_guard lock(frame_storage_);
  // The frames should not have been returned to storage.
  EXPECT_EQ(frame_storage_.size(), storage_size_after_acquire);
}

TEST_F(NetworkDeviceTestFixture, GetInfo) {
  // This call should just forward to the base object, the network device can't fill out this info.
  device_info_t info;
  device_.get_info_.ExpectCall(&info);
  device_.network_device_.NetworkDeviceImplGetInfo(&info);
  device_.get_info_.VerifyAndClear();
}

TEST_F(NetworkDeviceTestFixture, QueueTx) {
  constexpr uint32_t kHeadLength = 42;
  constexpr uint32_t kTailLength = 12;
  constexpr uint32_t kFrameSize = 256;

  // Make sure test parameters are valid, extra head and tail space must fit inside frame size.
  ASSERT_GE(kFrameSize, kHeadLength + kTailLength);

  tx_buffer_t buffers[3];
  buffer_region_t buffer_regions[std::size(buffers)];
  FillTxBuffers(buffers, buffer_regions, std::size(buffers), kFrameSize, kHeadLength, kTailLength);

  device_.queue_tx_.ExpectCallWithMatcher([&](cpp20::span<Frame> frames) {
    ASSERT_EQ(frames.size(), std::size(buffers));
    auto verify = [&](const Frame& frame, const tx_buffer_t& buffer) {
      EXPECT_EQ(frame.VmoId(), buffer.data_list[0].vmo);
      EXPECT_EQ(frame.BufferId(), buffer.id);
      EXPECT_EQ(frame.PortId(), buffer.meta.port);
      // The queueing logic should shrink the head of the frame so that the head length is before
      // the data pointer. This way the frame will always point to the actual payload.
      EXPECT_EQ(frame.Size(), buffer.data_list[0].length - kHeadLength - kTailLength);
      EXPECT_EQ(frame.VmoOffset(), buffer.data_list[0].offset + buffer.head_length);
      EXPECT_EQ(frame.Data(), device_.vmo_addrs_[frame.VmoId()] + buffer.data_list[0].offset +
                                  buffer.head_length);
      EXPECT_EQ(frame.Headroom(), buffer.head_length);
    };
    verify(frames[0], buffers[0]);
    verify(frames[1], buffers[1]);
    verify(frames[2], buffers[2]);
  });
  device_.network_device_.NetworkDeviceImplQueueTx(buffers, std::size(buffers));
  device_.queue_tx_.VerifyAndClear();
}

TEST_F(NetworkDeviceTestFixture, QueueTxWhenStopped) {
  // A stopped device should not queue frames for TX, it should instead reported them as completed
  // to the network device with a status code of ZX_ERR_UNAVAILABLE

  device_.stop_.ExpectCallWithMatcher([](NetworkDevice::Callbacks::StopTxn) {});
  // Stop the device.
  device_.network_device_.NetworkDeviceImplStop([](void*) {}, nullptr);
  device_.stop_.VerifyAndClear();

  tx_buffer_t buffers[3];
  buffer_region_t buffer_regions[std::size(buffers)];
  FillTxBuffers(buffers, buffer_regions, std::size(buffers), 256, 0, 0);

  device_.queue_tx_.ExpectNoCall();
  netdev_ifc_.complete_tx_.ExpectCallWithMatcher([&](const tx_result_t* results, size_t count) {
    ASSERT_EQ(count, std::size(buffers));
    for (size_t i = 0; i < count; ++i) {
      EXPECT_EQ(results[i].id, buffers[i].id);
      EXPECT_EQ(results[i].status, ZX_ERR_UNAVAILABLE);
    }
  });
  device_.network_device_.NetworkDeviceImplQueueTx(buffers, std::size(buffers));
  device_.queue_tx_.VerifyAndClear();
  netdev_ifc_.complete_tx_.VerifyAndClear();
}

TEST_F(NetworkDeviceTestFixture, QueueRxSpace) {
  constexpr size_t kFrameSize = 256;

  uint32_t buffer_id = 0;
  size_t vmo_offset = 0;
  auto fill_buffer = [&](rx_space_buffer_t& buffer) {
    buffer.id = buffer_id++;
    buffer.region.vmo = kVmoId;
    buffer.region.length = kFrameSize;
    buffer.region.offset = vmo_offset;
    vmo_offset += kFrameSize;
  };

  rx_space_buffer_t buffers[3] = {};
  fill_buffer(buffers[0]);
  fill_buffer(buffers[1]);
  fill_buffer(buffers[2]);

  device_.queue_rx_space_.ExpectCall(buffers, std::size(buffers), device_.vmo_addrs_.data());

  device_.NetDevQueueRxSpace(buffers, std::size(buffers), device_.vmo_addrs_.data());
  device_.queue_rx_space_.VerifyAndClear();
}

TEST_F(NetworkDeviceTestFixture, Snoop) {
  constexpr bool kSnoopEnabled = true;

  device_.set_snoop_enabled_.ExpectCall(kSnoopEnabled);
  device_.NetDevSetSnoopEnabled(kSnoopEnabled);
  device_.set_snoop_enabled_.VerifyAndClear();
}

}  // namespace
