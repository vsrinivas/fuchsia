// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device-controller-connection.h"

#include <fbl/auto_lock.h>
#include <lib/zx/vmo.h>
#include <zircon/status.h>
#include "../shared/fidl_txn.h"
#include "../shared/log.h"
#include "connection-destroyer.h"
#include "devhost.h"
#include "proxy-iostate.h"
#include "zx-device.h"

namespace devmgr {

namespace {

struct DevhostRpcReadContext {
    const char* path;
    DeviceControllerConnection* conn;
};

zx_status_t fidl_BindDriver(void* raw_ctx, const char* driver_path_data,
                            size_t driver_path_size, zx_handle_t raw_driver_vmo,
                            fidl_txn_t* txn) {
    auto ctx = static_cast<DevhostRpcReadContext*>(raw_ctx);
    zx::vmo driver_vmo(raw_driver_vmo);
    fbl::StringPiece driver_path(driver_path_data, driver_path_size);

    // TODO: api lock integration
    log(RPC_IN, "devhost[%s] bind driver '%.*s'\n", ctx->path, static_cast<int>(driver_path_size),
        driver_path_data);
    fbl::RefPtr<zx_driver_t> drv;
    if (ctx->conn->dev()->flags & DEV_FLAG_DEAD) {
        log(ERROR, "devhost[%s] bind to removed device disallowed\n", ctx->path);
        return fuchsia_device_manager_DeviceControllerBindDriver_reply(txn, ZX_ERR_IO_NOT_PRESENT);
    }

    zx_status_t r;
    if ((r = dh_find_driver(driver_path, std::move(driver_vmo), &drv)) < 0) {
        log(ERROR, "devhost[%s] driver load failed: %d\n", ctx->path, r);
        return fuchsia_device_manager_DeviceControllerBindDriver_reply(txn, r);
    }

    if (drv->has_bind_op()) {
        BindContext bind_ctx = {
            .parent = ctx->conn->dev(),
            .child = nullptr,
        };
        r = drv->BindOp(&bind_ctx, ctx->conn->dev());

        if ((r == ZX_OK) && (bind_ctx.child == nullptr)) {
            printf("devhost: WARNING: driver '%.*s' did not add device in bind()\n",
                   static_cast<int>(driver_path_size), driver_path_data);
        }
        if (r != ZX_OK) {
            log(ERROR, "devhost[%s] bind driver '%.*s' failed: %d\n", ctx->path,
                static_cast<int>(driver_path_size), driver_path_data, r);
        }
        return fuchsia_device_manager_DeviceControllerBindDriver_reply(txn, r);
    }

    if (!drv->has_create_op()) {
        log(ERROR, "devhost[%s] neither create nor bind are implemented: '%.*s'\n", ctx->path,
            static_cast<int>(driver_path_size), driver_path_data);
    }
    return fuchsia_device_manager_DeviceControllerBindDriver_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

zx_status_t fidl_ConnectProxy(void* raw_ctx, zx_handle_t raw_shadow) {
    auto ctx = static_cast<DevhostRpcReadContext*>(raw_ctx);
    zx::channel shadow(raw_shadow);

    log(RPC_SDW, "devhost[%s] connect proxy rpc\n", ctx->path);
    ctx->conn->dev()->ops->rxrpc(ctx->conn->dev()->ctx, ZX_HANDLE_INVALID);
    // Ignore any errors in the creation for now?
    // TODO(teisenbe/kulakowski): Investigate if this is the right thing
    ProxyIostate::Create(ctx->conn->dev(), std::move(shadow), DevhostAsyncLoop()->dispatcher());
    return ZX_OK;
}

zx_status_t fidl_Suspend(void* raw_ctx, uint32_t flags, fidl_txn_t* txn) {
    auto ctx = static_cast<DevhostRpcReadContext*>(raw_ctx);
    zx_status_t r;
    {
        ApiAutoLock lock;
        r = devhost_device_suspend(ctx->conn->dev(), flags);
    }
    return fuchsia_device_manager_DeviceControllerSuspend_reply(txn, r);
}

zx_status_t fidl_RemoveDevice(void* raw_ctx) {
    auto ctx = static_cast<DevhostRpcReadContext*>(raw_ctx);
    device_remove(ctx->conn->dev().get());
    return ZX_OK;
}

// Handler for when open() is called on a device
zx_status_t fidl_DirectoryOpen(void* ctx, uint32_t flags, uint32_t mode, const char* path_data,
                               size_t path_size, zx_handle_t object) {
    zx::channel c(object);
    if (path_size != 1 && path_data[0] != '.') {
        log(ERROR, "devhost: Tried to open path '%.*s'\n", static_cast<int>(path_size), path_data);
        return ZX_OK;
    }
    auto conn = static_cast<DeviceControllerConnection*>(ctx);
    devhost_device_connect(conn->dev(), flags, std::move(c));
    return ZX_OK;
}

const fuchsia_device_manager_DeviceController_ops_t kDefaultDeviceOps = {
    .BindDriver = fidl_BindDriver,
    .ConnectProxy = fidl_ConnectProxy,
    .RemoveDevice = fidl_RemoveDevice,
    .Suspend = fidl_Suspend,
};

const fuchsia_io_Directory_ops_t kDefaultDirectoryOps = []() {
    fuchsia_io_Directory_ops_t ops;
    ops.Open = fidl_DirectoryOpen;
    return ops;
}();

zx_status_t dh_null_reply(fidl_txn_t* reply, const fidl_msg_t* msg) {
    return ZX_OK;
}

} // namespace

DeviceControllerConnection::DeviceControllerConnection(
        fbl::RefPtr<zx_device> dev, zx::channel rpc,
        const fuchsia_device_manager_DeviceController_ops_t* device_fidl_ops,
        const fuchsia_io_Directory_ops_t* directory_fidl_ops)
    : dev_(std::move(dev)), device_fidl_ops_(device_fidl_ops),
      directory_fidl_ops_(directory_fidl_ops) {

    dev_->rpc = zx::unowned_channel(rpc);
    dev_->conn.store(this);
    set_channel(std::move(rpc));
}

DeviceControllerConnection::~DeviceControllerConnection() {
    // Ensure that the device has no dangling references to the resources we're
    // destroying.  This is safe because a device only ever has one associated
    // DeviceControllerConnection.
    dev_->conn.store(nullptr);
    dev_->rpc = zx::unowned_channel();
}

zx_status_t DeviceControllerConnection::Create(
        fbl::RefPtr<zx_device> dev, zx::channel rpc,
        std::unique_ptr<DeviceControllerConnection>* conn) {
    return Create(std::move(dev), std::move(rpc), &kDefaultDeviceOps, &kDefaultDirectoryOps, conn);
}

zx_status_t DeviceControllerConnection::Create(
        fbl::RefPtr<zx_device> dev, zx::channel rpc,
        const fuchsia_device_manager_DeviceController_ops_t* device_fidl_ops,
        const fuchsia_io_Directory_ops_t* directory_fidl_ops,
        std::unique_ptr<DeviceControllerConnection>* conn) {
    *conn = std::make_unique<DeviceControllerConnection>(std::move(dev), std::move(rpc),
                                                         device_fidl_ops, directory_fidl_ops);
    if (*conn == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    return ZX_OK;
}

void DeviceControllerConnection::HandleRpc(
        std::unique_ptr<DeviceControllerConnection> conn, async_dispatcher_t* dispatcher,
        async::WaitBase* wait, zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        log(ERROR, "devhost: devcoord conn wait error: %d\n", status);
        return;
    }
    if (signal->observed & ZX_CHANNEL_READABLE) {
        zx_status_t r = conn->HandleRead();
        if (r != ZX_OK) {
            if (conn->dev_->conn.load() == nullptr && r == ZX_ERR_INTERNAL) {
                // Treat this as a PEER_CLOSED below.  It can happen if the
                // devcoordinator sent us a request while we asked the
                // devcoordinator to remove us.  The coordinator then closes the
                // channel before we can reply, and the FIDL bindings convert
                // the PEER_CLOSED on zx_channel_write() to a ZX_ERR_INTERNAL.  See ZX-4114.
                __UNUSED auto r = conn.release();
                return;
            }
            log(ERROR, "devhost: devmgr rpc unhandleable ios=%p r=%d. fatal.\n", conn.get(), r);
            abort();
        }
        BeginWait(std::move(conn), dispatcher);
        return;
    }
    if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
        // Check if we were expecting this peer close.  If not, this could be a
        // serious bug.
        if (conn->dev_->conn.load() == nullptr) {
            // We're in the middle of shutting down, so just stop processing
            // signals and wait for the queued shutdown packet.  It has a
            // reference to the connection, which it will use to recover
            // ownership of it.
            __UNUSED auto r = conn.release();
            return;
        }

        log(ERROR, "devhost: devmgr disconnected! fatal. (conn=%p)\n", conn.get());
        abort();
    }
    log(ERROR, "devhost: no work? %08x\n", signal->observed);
    BeginWait(std::move(conn), dispatcher);
}

zx_status_t DeviceControllerConnection::HandleRead() {
    zx::unowned_channel conn = channel();
    uint8_t msg[8192];
    zx_handle_t hin[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t msize = sizeof(msg);
    uint32_t hcount = fbl::count_of(hin);
    zx_status_t status = conn->read(0, msg, hin, msize, hcount, &msize, &hcount);
    if (status != ZX_OK) {
        return status;
    }

    fidl_msg_t fidl_msg = {
        .bytes = msg,
        .handles = hin,
        .num_bytes = msize,
        .num_handles = hcount,
    };

    if (fidl_msg.num_bytes < sizeof(fidl_message_header_t)) {
        zx_handle_close_many(fidl_msg.handles, fidl_msg.num_handles);
        return ZX_ERR_IO;
    }

    char buffer[512];
    const char* path = mkdevpath(dev_, buffer, sizeof(buffer));

    // Double-check that Open (the only message we forward) cannot be mistaken for an
    // internal dev coordinator RPC message.
    static_assert(
        fuchsia_device_manager_DevhostControllerCreateDeviceStubOrdinal !=
            fuchsia_io_DirectoryOpenOrdinal &&
        fuchsia_device_manager_DevhostControllerCreateDeviceOrdinal !=
            fuchsia_io_DirectoryOpenOrdinal &&
        fuchsia_device_manager_DeviceControllerBindDriverOrdinal !=
            fuchsia_io_DirectoryOpenOrdinal &&
        fuchsia_device_manager_DeviceControllerConnectProxyOrdinal !=
            fuchsia_io_DirectoryOpenOrdinal &&
        fuchsia_device_manager_DeviceControllerSuspendOrdinal !=
            fuchsia_io_DirectoryOpenOrdinal &&
        fuchsia_device_manager_DeviceControllerRemoveDeviceOrdinal !=
            fuchsia_io_DirectoryOpenOrdinal);

    auto hdr = static_cast<fidl_message_header_t*>(fidl_msg.bytes);
    if (hdr->ordinal == fuchsia_io_DirectoryOpenOrdinal) {
        log(RPC_RIO, "devhost[%s] FIDL OPEN\n", path);

        fidl_txn_t dh_null_txn = {
            .reply = dh_null_reply,
        };
        status = fuchsia_io_Directory_dispatch(this, &dh_null_txn, &fidl_msg, directory_fidl_ops_);
        if (status != ZX_OK) {
            log(ERROR, "devhost: OPEN failed: %s\n", zx_status_get_string(status));
            return status;
        }
        return ZX_OK;
    }

    FidlTxn txn(std::move(conn), hdr->txid);
    DevhostRpcReadContext read_ctx = {path, this};
    return fuchsia_device_manager_DeviceController_dispatch(&read_ctx, txn.fidl_txn(), &fidl_msg,
                                                            device_fidl_ops_);
}

} // namespace devmgr
