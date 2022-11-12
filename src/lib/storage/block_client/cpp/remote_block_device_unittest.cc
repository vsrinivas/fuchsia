// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/block_client/cpp/remote_block_device.h"

#include <fidl/fuchsia.hardware.block/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fzl/fifo.h>
#include <lib/zx/vmo.h>

#include <thread>
#include <unordered_set>
#include <utility>

#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <gtest/gtest.h>
#include <storage/buffer/owned_vmoid.h>

namespace block_client {
namespace {

constexpr uint16_t kGoldenVmoid = 2;
constexpr uint32_t kBlockSize = 4096;
constexpr uint64_t kBlockCount = 10;

// Emulate the non-standard behavior of the block device which implements both the block device APIs
// and the Node API.
class MockBlockDevice : public fidl::testing::WireTestBase<fuchsia_hardware_block::BlockAndNode> {
 public:
  explicit MockBlockDevice(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {
    // Create buffer for read / write calls
    buffer_.resize(kBlockSize * kBlockCount);
  }

  void Bind(fidl::ServerEnd<fuchsia_hardware_block::Block> server_end) {
    fidl::BindServer(
        dispatcher_,
        fidl::ServerEnd<fuchsia_hardware_block::BlockAndNode>(server_end.TakeChannel()), this);
  }

  zx_status_t ReadFifoRequests(block_fifo_request_t* requests, size_t* count) const {
    zx_signals_t seen;
    zx_status_t status = fifo_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                        zx::deadline_after(zx::sec(5)), &seen);
    if (status != ZX_OK) {
      return status;
    }
    return fifo_.read(requests, BLOCK_FIFO_MAX_DEPTH, count);
  }

  zx_status_t WriteFifoResponse(const block_fifo_response_t& response) const {
    return fifo_.write_one(response);
  }

  bool FifoAttached() const { return fifo_.get().is_valid(); }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    FAIL() << "unexpected call to: " << name;
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) override {
    fidl::BindServer(
        dispatcher_,
        fidl::ServerEnd<fuchsia_hardware_block::BlockAndNode>(request->object.TakeChannel()), this);
  }

  void GetInfo(GetInfoCompleter::Sync& completer) override {
    fuchsia_hardware_block::wire::BlockInfo info = {
        .block_count = kBlockCount,
        .block_size = kBlockSize,
        .max_transfer_size = kBlockSize,
    };
    completer.Reply(ZX_OK, fidl::ObjectView<decltype(info)>::FromExternal(&info));
  }

  void GetStats(GetStatsRequestView request, GetStatsCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
  }

  void GetFifo(GetFifoCompleter::Sync& completer) override {
    fzl::fifo<block_fifo_request_t, block_fifo_response_t> client;
    EXPECT_EQ(fzl::create_fifo(BLOCK_FIFO_MAX_DEPTH, 0, &client, &fifo_), ZX_OK);
    completer.Reply(ZX_OK, std::move(client.get()));
  }

  void AttachVmo(AttachVmoRequestView request, AttachVmoCompleter::Sync& completer) override {
    fuchsia_hardware_block::wire::VmoId vmoid = {kGoldenVmoid};
    completer.Reply(ZX_OK, fidl::ObjectView<decltype(vmoid)>::FromExternal(&vmoid));
  }

  void CloseFifo(CloseFifoCompleter::Sync& completer) override {
    fifo_.reset();
    completer.Reply(ZX_OK);
  }

  void RebindDevice(RebindDeviceCompleter::Sync& completer) override {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }

  void ReadBlocks(ReadBlocksRequestView request, ReadBlocksCompleter::Sync& completer) override {
    zx_status_t status = request->vmo.write(buffer_.data() + request->dev_offset,
                                            request->vmo_offset, request->length);
    completer.Reply(status);
  }

  void WriteBlocks(WriteBlocksRequestView request, WriteBlocksCompleter::Sync& completer) override {
    zx_status_t status = request->vmo.read(buffer_.data() + request->dev_offset,
                                           request->vmo_offset, request->length);
    completer.Reply(status);
  }

  async_dispatcher_t* dispatcher_ = nullptr;
  fzl::fifo<block_fifo_response_t, block_fifo_request_t> fifo_;
  std::vector<uint8_t> buffer_;
};

// Tests that the RemoteBlockDevice can be created and immediately destroyed.
TEST(RemoteBlockDeviceTest, Constructor) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_block::Block>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();
  auto& [client, server] = endpoints.value();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  MockBlockDevice mock_device(loop.dispatcher());
  mock_device.Bind(std::move(server));

  std::unique_ptr<RemoteBlockDevice> device;
  ASSERT_EQ(RemoteBlockDevice::Create(std::move(client), &device), ZX_OK);
}

// Tests that a fifo is attached to the block device for the duration of the
// RemoteBlockDevice lifetime.
TEST(RemoteBlockDeviceTest, FifoClosedOnDestruction) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_block::Block>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();
  auto& [client, server] = endpoints.value();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  MockBlockDevice mock_device(loop.dispatcher());
  mock_device.Bind(std::move(server));

  EXPECT_FALSE(mock_device.FifoAttached());
  {
    std::unique_ptr<RemoteBlockDevice> device;
    ASSERT_EQ(RemoteBlockDevice::Create(std::move(client), &device), ZX_OK);
    EXPECT_TRUE(mock_device.FifoAttached());
  }
  EXPECT_FALSE(mock_device.FifoAttached());
}

// Tests that the RemoteBlockDevice is capable of transmitting and receiving
// messages with the block device.
TEST(RemoteBlockDeviceTest, WriteTransactionReadResponse) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_block::Block>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();
  auto& [client, server] = endpoints.value();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  MockBlockDevice mock_device(loop.dispatcher());
  mock_device.Bind(std::move(server));

  std::unique_ptr<RemoteBlockDevice> device;
  ASSERT_EQ(RemoteBlockDevice::Create(std::move(client), &device), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo), ZX_OK);

  storage::OwnedVmoid vmoid;
  ASSERT_EQ(device->BlockAttachVmo(vmo, &vmoid.GetReference(device.get())), ZX_OK);
  ASSERT_EQ(kGoldenVmoid, vmoid.get());

  block_fifo_request_t request;
  request.opcode = BLOCKIO_READ;
  request.reqid = 1;
  request.group = 0;
  request.vmoid = vmoid.get();
  request.length = 1;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  std::thread server_thread([&mock_device, &request] {
    block_fifo_request_t server_request;
    size_t actual;
    EXPECT_EQ(mock_device.ReadFifoRequests(&server_request, &actual), ZX_OK);
    EXPECT_EQ(actual, 1u);
    EXPECT_EQ(0, memcmp(&server_request, &request, sizeof(request)));

    block_fifo_response_t response;
    response.status = ZX_OK;
    response.reqid = request.reqid;
    response.group = request.group;
    response.count = 1;
    EXPECT_EQ(mock_device.WriteFifoResponse(response), ZX_OK);
  });

  ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);
  vmoid.TakeId();
  server_thread.join();
}

// Tests that the RemoteBlockDevice is capable of transmitting and receiving
// messages with the block device.
TEST(RemoteBlockDeviceTest, WriteReadBlock) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_block::Block>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();
  auto& [client, server] = endpoints.value();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  MockBlockDevice mock_device(loop.dispatcher());
  mock_device.Bind(std::move(server));

  constexpr size_t max_count = 3;

  std::vector<uint8_t> write_buffer(kBlockSize * max_count + 5),
      read_buffer(kBlockSize * max_count);
  // write some pattern to the write buffer
  for (size_t i = 0; i < kBlockSize * max_count; ++i) {
    write_buffer[i] = static_cast<uint8_t>(i % 251);
  }
  // Test that unaligned counts and offsets result in failures:
  ASSERT_NE(SingleWriteBytes(client, write_buffer.data(), 5, 0), ZX_OK);
  ASSERT_NE(SingleWriteBytes(client, write_buffer.data(), kBlockSize, 5), ZX_OK);
  ASSERT_NE(SingleWriteBytes(client, nullptr, kBlockSize, 0), ZX_OK);
  ASSERT_NE(SingleReadBytes(client, read_buffer.data(), 5, 0), ZX_OK);
  ASSERT_NE(SingleReadBytes(client, read_buffer.data(), kBlockSize, 5), ZX_OK);
  ASSERT_NE(SingleReadBytes(client, nullptr, kBlockSize, 0), ZX_OK);

  // test multiple counts, multiple offsets
  for (uint64_t count = 1; count < max_count; ++count) {
    for (uint64_t offset = 0; offset < 2; ++offset) {
      size_t buffer_offset = count + 10 * offset;
      ASSERT_EQ(SingleWriteBytes(client, write_buffer.data() + buffer_offset, kBlockSize * count,
                                 kBlockSize * offset),
                ZX_OK);
      ASSERT_EQ(
          SingleReadBytes(client, read_buffer.data(), kBlockSize * count, kBlockSize * offset),
          ZX_OK);
      ASSERT_EQ(memcmp(write_buffer.data() + buffer_offset, read_buffer.data(), kBlockSize * count),
                0);
    }
  }
}

TEST(RemoteBlockDeviceTest, VolumeManagerOrdinals) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_block::Block>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();
  auto& [client, server] = endpoints.value();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  MockBlockDevice mock_device(loop.dispatcher());
  mock_device.Bind(std::move(server));

  std::unique_ptr<RemoteBlockDevice> device;
  ASSERT_EQ(RemoteBlockDevice::Create(std::move(client), &device), ZX_OK);

  // Querying the volume returns an error; the device doesn't implement
  // any FVM protocols. However, VolumeQuery utilizes a distinct
  // channel, so the connection should remain open.
  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info;
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info;
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, device->VolumeGetInfo(&manager_info, &volume_info));

  // Other block functions still function correctly.
  fuchsia_hardware_block::wire::BlockInfo block_info;
  EXPECT_EQ(device->BlockGetInfo(&block_info), ZX_OK);

  // Sending any FVM method other than "VolumeQuery" also returns an error.
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, device->VolumeExtend(0, 0));

  // But now, other (previously valid) block methods fail, because FIDL has
  // closed the channel.
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, device->BlockGetInfo(&block_info));
}

TEST(RemoteBlockDeviceTest, LargeThreadCountSuceeds) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_block::Block>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();
  auto& [client, server] = endpoints.value();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  MockBlockDevice mock_device(loop.dispatcher());
  mock_device.Bind(std::move(server));

  std::unique_ptr<RemoteBlockDevice> device;
  ASSERT_EQ(RemoteBlockDevice::Create(std::move(client), &device), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo), ZX_OK);

  storage::OwnedVmoid vmoid;
  ASSERT_EQ(device->BlockAttachVmo(vmo, &vmoid.GetReference(device.get())), ZX_OK);
  ASSERT_EQ(kGoldenVmoid, vmoid.get());

  constexpr int kThreadCount = 2 * MAX_TXN_GROUP_COUNT;
  std::thread threads[kThreadCount];
  fbl::Mutex mutex;
  fbl::ConditionVariable condition;
  int done = 0;
  for (auto& thread : threads) {
    thread = std::thread([device = device.get(), &mutex, &done, &condition, vmoid = vmoid.get()]() {
      block_fifo_request_t request = {};
      request.opcode = BLOCKIO_READ;
      request.vmoid = vmoid;
      request.length = 1;
      ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);
      fbl::AutoLock lock(&mutex);
      ++done;
      condition.Signal();
    });
  }
  vmoid.TakeId();  // We don't need the vmoid any more.
  block_fifo_request_t requests[kThreadCount + BLOCK_FIFO_MAX_DEPTH];
  size_t request_count = 0;
  do {
    if (request_count < kThreadCount) {
      // Read some more requests.
      size_t count = 0;
      ASSERT_EQ(mock_device.ReadFifoRequests(&requests[request_count], &count), ZX_OK);
      ASSERT_GT(count, 0u);
      request_count += count;
    }
    // Check that all the outstanding requests we have use different group IDs.
    std::unordered_set<groupid_t> groups;
    for (size_t i = done; i < request_count; ++i) {
      ASSERT_TRUE(groups.insert(requests[i].group).second);
    }
    // Finish one request.
    block_fifo_response_t response;
    response.status = ZX_OK;
    response.reqid = requests[done].reqid;
    response.group = requests[done].group;
    response.count = 1;
    int last_done = done;
    EXPECT_EQ(mock_device.WriteFifoResponse(response), ZX_OK);
    // Wait for it to be done.
    fbl::AutoLock lock(&mutex);
    while (done != last_done + 1) {
      condition.Wait(&mutex);
    }
  } while (done < kThreadCount);
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(RemoteBlockDeviceTest, NoHangForErrorsWithMultipleThreads) {
  zx::result endpoints = fidl::CreateEndpoints<fuchsia_hardware_block::Block>();
  ASSERT_TRUE(endpoints.is_ok()) << endpoints.status_string();
  auto& [client, server] = endpoints.value();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  std::unique_ptr<RemoteBlockDevice> device;
  constexpr int kThreadCount = 4 * MAX_TXN_GROUP_COUNT;
  std::thread threads[kThreadCount];

  {
    MockBlockDevice mock_device(loop.dispatcher());
    mock_device.Bind(std::move(server));

    ASSERT_EQ(RemoteBlockDevice::Create(std::move(client), &device), ZX_OK);

    zx::vmo vmo;
    ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo), ZX_OK);

    storage::OwnedVmoid vmoid;
    ASSERT_EQ(device->BlockAttachVmo(vmo, &vmoid.GetReference(device.get())), ZX_OK);
    ASSERT_EQ(kGoldenVmoid, vmoid.get());

    for (auto& thread : threads) {
      thread = std::thread([device = device.get(), vmoid = vmoid.get()]() {
        block_fifo_request_t request = {};
        request.opcode = BLOCKIO_READ;
        request.vmoid = vmoid;
        request.length = 1;
        ASSERT_EQ(ZX_ERR_PEER_CLOSED, device->FifoTransaction(&request, 1));
      });
    }
    vmoid.TakeId();  // We don't need the vmoid any more.

    // Wait for at least 2 requests to be received.
    block_fifo_request_t requests[BLOCK_FIFO_MAX_DEPTH];
    size_t request_count = 0;
    while (request_count < 2) {
      size_t count = 0;
      ASSERT_EQ(mock_device.ReadFifoRequests(requests, &count), ZX_OK);
      request_count += count;
    }

    // Allow MockBlockDevice to go out of scope which should close the fifo.
    loop.Shutdown();
  }

  // We should be able to join all the threads.
  for (auto& thread : threads) {
    thread.join();
  }
}

}  // namespace
}  // namespace block_client
