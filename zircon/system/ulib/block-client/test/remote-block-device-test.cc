// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/fifo.h>
#include <lib/zx/vmo.h>

#include <thread>

#include <block-client/cpp/remote-block-device.h>
#include <zxtest/zxtest.h>

namespace block_client {
namespace {

constexpr uint16_t kGoldenVmoid = 2;

class MockBlockDevice {
 public:
  using Binder = fidl::Binder<MockBlockDevice>;
  zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) {
    dispatcher_ = dispatcher;
    return fidl_bind(dispatcher_, channel.release(), FidlDispatch, this, nullptr);
  }

  zx_status_t ReadFifoRequests(block_fifo_request_t* requests, size_t* count) {
    zx_signals_t seen;
    zx_status_t status = fifo_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                        zx::deadline_after(zx::sec(5)), &seen);
    if (status != ZX_OK) {
      return status;
    }
    return fifo_.read(requests, BLOCK_FIFO_MAX_DEPTH, count);
  }

  zx_status_t WriteFifoResponse(const block_fifo_response_t& response) {
    return fifo_.write_one(response);
  }

  bool FifoAttached() const { return fifo_.get().is_valid(); }

 private:
  // Manually dispatch to emulate the non-standard behavior of the block device,
  // which implements both the block device APIs, the Node API, and (optionally)
  // the FVM API.
  static zx_status_t FidlDispatch(void* context, fidl_txn_t* txn, fidl_msg_t* msg, const void*) {
    return reinterpret_cast<MockBlockDevice*>(context)->HandleMessage(txn, msg);
  }

  zx_status_t HandleMessage(fidl_txn_t* txn, fidl_msg_t* msg) {
    zx_status_t status = fuchsia_hardware_block_Block_try_dispatch(this, txn, msg, BlockOps());
    if (status != ZX_ERR_NOT_SUPPORTED) {
      return status;
    }
    return fuchsia_io_Node_dispatch(this, txn, msg, NodeOps());
  }

  static const fuchsia_hardware_block_Block_ops* BlockOps() {
    static const fuchsia_hardware_block_Block_ops kOps = {
        .GetInfo = Binder::BindMember<&MockBlockDevice::BlockGetInfo>,
        .GetStats = Binder::BindMember<&MockBlockDevice::BlockGetStats>,
        .GetFifo = Binder::BindMember<&MockBlockDevice::BlockGetFifo>,
        .AttachVmo = Binder::BindMember<&MockBlockDevice::BlockAttachVmo>,
        .CloseFifo = Binder::BindMember<&MockBlockDevice::BlockCloseFifo>,
        .RebindDevice = Binder::BindMember<&MockBlockDevice::BlockRebindDevice>,
    };
    return &kOps;
  }

  // This implementation of Node is decidedly non-standard and incomplete, but it is
  // sufficient to test the cloning behavior used below.
  static const fuchsia_io_Node_ops* NodeOps() {
    static const fuchsia_io_Node_ops kOps = {
        .Clone = Binder::BindMember<&MockBlockDevice::NodeClone>,
        .Close = nullptr,
        .Describe = nullptr,
        .Sync = nullptr,
        .GetAttr = nullptr,
        .SetAttr = nullptr,
        .NodeGetFlags = nullptr,
        .NodeSetFlags = nullptr,
    };
    return &kOps;
  }

  zx_status_t NodeClone(uint32_t flags, zx_handle_t object) {
    Bind(dispatcher_, zx::channel(object));
    return ZX_OK;
  }

  zx_status_t BlockGetInfo(fidl_txn_t* txn) {
    fuchsia_hardware_block_BlockInfo info = {};
    return fuchsia_hardware_block_BlockGetInfo_reply(txn, ZX_OK, &info);
  }

  zx_status_t BlockGetStats(bool clear, fidl_txn_t* txn) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t BlockGetFifo(fidl_txn_t* txn) {
    fzl::fifo<block_fifo_request_t, block_fifo_response_t> client;
    EXPECT_OK(fzl::create_fifo(BLOCK_FIFO_MAX_DEPTH, 0, &client, &fifo_));
    return fuchsia_hardware_block_BlockGetFifo_reply(txn, ZX_OK, client.release());
  }

  zx_status_t BlockAttachVmo(zx_handle_t vmo, fidl_txn_t* txn) {
    fuchsia_hardware_block_VmoId vmoid = {kGoldenVmoid};
    return fuchsia_hardware_block_BlockAttachVmo_reply(txn, ZX_OK, &vmoid);
  }

  zx_status_t BlockCloseFifo(fidl_txn_t* txn) {
    fifo_.reset();
    return fuchsia_hardware_block_BlockCloseFifo_reply(txn, ZX_OK);
  }

  zx_status_t BlockRebindDevice(fidl_txn_t* txn) { return ZX_ERR_NOT_SUPPORTED; }

  async_dispatcher_t* dispatcher_ = nullptr;
  fzl::fifo<block_fifo_response_t, block_fifo_request_t> fifo_;
};

// Tests that the RemoteBlockDevice can be created and immediately destroyed.
TEST(RemoteBlockDeviceTest, Constructor) {
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  MockBlockDevice mock_device;
  ASSERT_OK(mock_device.Bind(loop.dispatcher(), std::move(server)));

  std::unique_ptr<RemoteBlockDevice> device;
  ASSERT_OK(RemoteBlockDevice::Create(std::move(client), &device));
}

// Tests that a fifo is attached to the block device for the duration of the
// RemoteBlockDevice lifetime.
TEST(RemoteBlockDeviceTest, FifoClosedOnDestruction) {
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  MockBlockDevice mock_device;
  ASSERT_OK(mock_device.Bind(loop.dispatcher(), std::move(server)));

  EXPECT_FALSE(mock_device.FifoAttached());
  {
    std::unique_ptr<RemoteBlockDevice> device;
    ASSERT_OK(RemoteBlockDevice::Create(std::move(client), &device));
    EXPECT_TRUE(mock_device.FifoAttached());
  }
  EXPECT_FALSE(mock_device.FifoAttached());
}

// Tests that the RemoteBlockDevice is capable of transmitting and receiving
// messages with the block device.
TEST(RemoteBlockDeviceTest, WriteTransactionReadResponse) {
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  MockBlockDevice mock_device;
  ASSERT_OK(mock_device.Bind(loop.dispatcher(), std::move(server)));

  std::unique_ptr<RemoteBlockDevice> device;
  ASSERT_OK(RemoteBlockDevice::Create(std::move(client), &device));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));

  fuchsia_hardware_block_VmoId vmoid;
  ASSERT_OK(device->BlockAttachVmo(vmo, &vmoid));
  ASSERT_EQ(kGoldenVmoid, vmoid.id);

  block_fifo_request_t request;
  request.opcode = BLOCKIO_READ;
  request.reqid = 1;
  request.group = 0;
  request.vmoid = vmoid.id;
  request.length = 1;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  std::thread server_thread([&mock_device, &request] {
    block_fifo_request_t server_request;
    size_t actual;
    EXPECT_OK(mock_device.ReadFifoRequests(&server_request, &actual));
    EXPECT_EQ(1, actual);
    EXPECT_EQ(0, memcmp(&server_request, &request, sizeof(request)));

    block_fifo_response_t response;
    response.status = ZX_OK;
    response.reqid = request.reqid;
    response.group = request.group;
    response.count = 1;
    EXPECT_OK(mock_device.WriteFifoResponse(response));
  });

  ASSERT_OK(device->FifoTransaction(&request, 1));
  server_thread.join();
}

TEST(RemoteBlockDeviceTest, VolumeManagerOrdinals) {
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  MockBlockDevice mock_device;
  ASSERT_OK(mock_device.Bind(loop.dispatcher(), std::move(server)));

  std::unique_ptr<RemoteBlockDevice> device;
  ASSERT_OK(RemoteBlockDevice::Create(std::move(client), &device));

  // Querying the volume returns an error; the device doesn't implement
  // any FVM protocols. However, VolumeQuery utilizes a distinct
  // channel, so the connection should remain open.
  fuchsia_hardware_block_volume_VolumeInfo volume_info;
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, device->VolumeQuery(&volume_info));

  // Other block functions still function correctly.
  fuchsia_hardware_block_BlockInfo block_info;
  EXPECT_OK(device->BlockGetInfo(&block_info));

  // Sending any FVM method other than "VolumeQuery" also returns an error.
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, device->VolumeExtend(0, 0));

  // But now, other (previously valid) block methods fail, because FIDL has
  // closed the channel.
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, device->BlockGetInfo(&block_info));
}

}  // namespace
}  // namespace block_client
