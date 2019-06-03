// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/block-device.h>

#include <thread>

#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/fifo.h>
#include <lib/zx/vmo.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

constexpr uint16_t kGoldenVmoid = 2;

class MockBlockDevice {
public:
    using Binder = fidl::Binder<MockBlockDevice>;
    zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) {
        return Binder::BindOps<fuchsia_hardware_block_Block_dispatch>(
                dispatcher, std::move(channel), this, BlockOps());
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

    bool FifoAttached() const {
        return fifo_.get().is_valid();
    }

private:
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

    zx_status_t BlockGetInfo(fidl_txn_t* txn) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t BlockGetStats(bool clear, fidl_txn_t* txn) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t BlockGetFifo(fidl_txn_t* txn) {
        fzl::fifo<block_fifo_request_t, block_fifo_response_t> client;
        EXPECT_OK(fzl::create_fifo(BLOCK_FIFO_MAX_DEPTH, 0, &client, &fifo_));
        return fuchsia_hardware_block_BlockGetFifo_reply(txn, ZX_OK, client.release());
    }

    zx_status_t BlockAttachVmo(zx_handle_t vmo, fidl_txn_t* txn) {
        fuchsia_hardware_block_VmoID vmoid = { kGoldenVmoid } ;
        return fuchsia_hardware_block_BlockAttachVmo_reply(txn, ZX_OK, &vmoid);
    }

    zx_status_t BlockCloseFifo(fidl_txn_t* txn) {
        fifo_.reset();
        return fuchsia_hardware_block_BlockCloseFifo_reply(txn, ZX_OK);
    }

    zx_status_t BlockRebindDevice(fidl_txn_t* txn) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    fzl::fifo<block_fifo_response_t, block_fifo_request_t> fifo_;
};

// Tests that the RemoteBlockDevice can be created and immediately destroyed.
TEST(RemoteBlockDeviceTest, Constructor) {
    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
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

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
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

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_OK(loop.StartThread());

    MockBlockDevice mock_device;
    ASSERT_OK(mock_device.Bind(loop.dispatcher(), std::move(server)));

    std::unique_ptr<RemoteBlockDevice> device;
    ASSERT_OK(RemoteBlockDevice::Create(std::move(client), &device));

    zx::vmo vmo;
    zx::vmo dup;
    ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
    ASSERT_OK(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    fuchsia_hardware_block_VmoID vmoid;
    ASSERT_OK(device->BlockAttachVmo(std::move(dup), &vmoid));
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

} // namespace
} // namespace blobfs
