// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/vmo.h>
#include <thread>
#include <zxtest/zxtest.h>
#include "connection-destroyer.h"
#include "device-controller-connection.h"
#include "zx-device.h"

namespace {

const fuchsia_device_manager_DeviceController_ops_t kNoDeviceOps = {};
const fuchsia_io_Directory_ops_t kNoDirectoryOps = {};

TEST(DeviceControllerConnectionTestCase, Creation) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

    fbl::RefPtr<zx_device> dev;
    ASSERT_OK(zx_device::Create(&dev));

    zx::channel device_local, device_remote;
    ASSERT_OK(zx::channel::create(0, &device_local, &device_remote));

    std::unique_ptr<devmgr::DeviceControllerConnection> conn;

    ASSERT_NULL(dev->conn.load());
    ASSERT_OK(devmgr::DeviceControllerConnection::Create(
            dev, std::move(device_remote), &kNoDeviceOps, &kNoDirectoryOps, &conn));
    ASSERT_NOT_NULL(dev->conn.load());

    ASSERT_OK(devmgr::DeviceControllerConnection::BeginWait(std::move(conn), loop.dispatcher()));
    ASSERT_OK(loop.RunUntilIdle());
}

TEST(DeviceControllerConnectionTestCase, PeerClosedDuringReply) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

    fbl::RefPtr<zx_device> dev;
    ASSERT_OK(zx_device::Create(&dev));

    zx::channel device_local, device_remote;
    ASSERT_OK(zx::channel::create(0, &device_local, &device_remote));

    // This is static so we can access it from bind_driver().  The
    // existing structure of the code makes it difficult to plumb access to it
    // through to the callback.
    static struct {
        fbl::RefPtr<zx_device> dev;
        zx::channel local;
        async_dispatcher_t* dispatcher;
    } bind_driver_closure;

    bind_driver_closure.dev = dev;
    bind_driver_closure.local = std::move(device_local);
    bind_driver_closure.dispatcher = loop.dispatcher();

    auto bind_driver = [](void* ctx, const char* driver_path_data,
                          size_t driver_path_size, zx_handle_t raw_driver_vmo,
                          fidl_txn_t* txn) {
        // Pretend that a device closure happened right before we began
        // processing BindDriver.  Close the other half of the channel, so the reply below will fail
        // from ZX_ERR_PEER_CLOSED.
        auto conn = bind_driver_closure.dev->conn.exchange(nullptr);
        devmgr::ConnectionDestroyer::Get()->QueueDeviceControllerConnection(
                bind_driver_closure.dispatcher, conn);
        bind_driver_closure.local.reset();

        return fuchsia_device_manager_DeviceControllerBindDriver_reply(txn, ZX_OK);
    };
    fuchsia_device_manager_DeviceController_ops_t device_ops = {};
    device_ops.BindDriver = bind_driver;

    std::unique_ptr<devmgr::DeviceControllerConnection> conn;
    ASSERT_OK(devmgr::DeviceControllerConnection::Create(
            dev, std::move(device_remote), &device_ops, &kNoDirectoryOps, &conn));

    ASSERT_OK(devmgr::DeviceControllerConnection::BeginWait(std::move(conn), loop.dispatcher()));
    ASSERT_OK(loop.RunUntilIdle());

    // Create a thread to send a BindDriver message.  The thread isn't strictly
    // necessary, but is done out of convenience since the FIDL C bindings don't
    // expose non-zx_channel_call client bindings.
    enum {
        INITIAL,
        VMO_CREATE_FAILED,
        WRONG_CALL_STATUS,
        SUCCESS,
    } thread_status = INITIAL;
    std::thread synchronous_call_thread([channel=bind_driver_closure.local.get(),
                                        &thread_status]() {
        zx::vmo vmo;
        zx_status_t status = zx::vmo::create(0, 0, &vmo);
        if (status != ZX_OK) {
            thread_status = VMO_CREATE_FAILED;
            return;
        }
        zx_status_t call_status;
        status = fuchsia_device_manager_DeviceControllerBindDriver(channel, "", 0, vmo.release(),
                                                                   &call_status);
        // zx_channel_call() returns this when the handle is closed during the
        // call.
        if (status != ZX_ERR_CANCELED) {
            thread_status = WRONG_CALL_STATUS;
            return;
        }
        thread_status = SUCCESS;
    });

    ASSERT_OK(loop.Run(zx::time::infinite(), true /* run_once */));

    synchronous_call_thread.join();
    ASSERT_EQ(SUCCESS, thread_status);
    ASSERT_FALSE(bind_driver_closure.local.is_valid());
}

// Verify we do not abort when an expected PEER_CLOSED comes in.
TEST(DeviceControllerConnectionTestCase, PeerClosed) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

    fbl::RefPtr<zx_device> dev;
    ASSERT_OK(zx_device::Create(&dev));

    zx::channel device_local, device_remote;
    ASSERT_OK(zx::channel::create(0, &device_local, &device_remote));

    std::unique_ptr<devmgr::DeviceControllerConnection> conn;
    ASSERT_OK(devmgr::DeviceControllerConnection::Create(
            dev, std::move(device_remote), &kNoDeviceOps, &kNoDirectoryOps, &conn));

    ASSERT_OK(devmgr::DeviceControllerConnection::BeginWait(std::move(conn), loop.dispatcher()));
    ASSERT_OK(loop.RunUntilIdle());

    // Perform the device shutdown protocol, since otherwise the devhost code
    // will assert, since it is unable to handle unexpected connection closures.
    auto dev_conn = dev->conn.exchange(nullptr);
    devmgr::ConnectionDestroyer::Get()->QueueDeviceControllerConnection(loop.dispatcher(),
                                                                        dev_conn);
    device_local.reset();

    ASSERT_OK(loop.RunUntilIdle());
}

} // namespace
