// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl-proxy.h"

#include <lib/fidl-async/bind.h>

namespace devmgr {

zx_status_t FidlProxyHandler::Create(Coordinator* coordinator, async_dispatcher_t* dispatcher,
                                     zx::channel proxy_channel) {
    auto handler = std::make_unique<FidlProxyHandler>(coordinator);
    if (handler == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    handler->set_channel(std::move(proxy_channel));
    return BeginWait(std::move(handler), dispatcher);
}


void FidlProxyHandler::HandleRpc(fbl::unique_ptr<FidlProxyHandler> connection,
                                 async_dispatcher_t* dispatcher,
                                 async::WaitBase* wait, zx_status_t status,
                                 const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        return;
    }

    if (!(signal->observed & ZX_CHANNEL_READABLE)) {
        // Other side closed the connection, nothing to do.
        return;
    }

    connection->HandleClient(dispatcher, wait->object());

    BeginWait(std::move(connection), dispatcher);
}

void FidlProxyHandler::HandleClient(async_dispatcher_t* dispatcher, zx_handle_t channel) {
    constexpr size_t kInterfaceNameSize = 256;
    char interface_name[kInterfaceNameSize + 1] = {0};

    zx_handle_info_t client;
    uint32_t byte_count, handle_count;
    auto status = zx_channel_read_etc(channel,
                                      0, // Flags
                                      interface_name, &client, // bytes and handles
                                      kInterfaceNameSize, 1, // bytes and handles expected
                                      &byte_count, &handle_count); // received
    if (status != ZX_OK || handle_count != 1 || client.type != ZX_OBJ_TYPE_CHANNEL) {
        return;
    }
    zx::channel client_channel(client.handle);

    if (fbl::StringPiece(fuchsia_device_manager_DebugDumper_Name) ==
        fbl::StringPiece(interface_name, byte_count)) {
        static constexpr fuchsia_device_manager_DebugDumper_ops_t kOps = {
            .DumpTree = [](void* ctx, zx_handle_t vmo, fidl_txn_t* txn) {
                VmoWriter writer{zx::vmo(vmo)};
                static_cast<Coordinator*>(ctx)->DumpState(&writer);
                return fuchsia_device_manager_DebugDumperDumpTree_reply(
                        txn, writer.status(), writer.written(), writer.available());
            },
            .DumpDrivers = [](void* ctx, zx_handle_t vmo, fidl_txn_t* txn) {
                VmoWriter writer{zx::vmo(vmo)};
                static_cast<Coordinator*>(ctx)->DumpDrivers(&writer);
                return fuchsia_device_manager_DebugDumperDumpDrivers_reply(
                        txn, writer.status(), writer.written(), writer.available());
            },
            .DumpBindingProperties = [](void* ctx, zx_handle_t vmo, fidl_txn_t* txn) {
                VmoWriter writer{zx::vmo(vmo)};
                static_cast<Coordinator*>(ctx)->DumpGlobalDeviceProps(&writer);
                return fuchsia_device_manager_DebugDumperDumpBindingProperties_reply(
                        txn, writer.status(), writer.written(), writer.available());
            },
        };

        status = fidl_bind(dispatcher,
                           client_channel.release(),
                           reinterpret_cast<fidl_dispatch_t*>(
                               fuchsia_device_manager_DebugDumper_dispatch),
                           this->coordinator_,
                           &kOps);
        if (status != ZX_OK) {
            printf("Failed to bind to client channel: %d \n", status);
            return;
        }

    } else if (fbl::StringPiece(fuchsia_device_manager_Administrator_Name) ==
               fbl::StringPiece(interface_name, byte_count)) {
       static constexpr fuchsia_device_manager_Administrator_ops_t kOps = {
            .Suspend = [](void* ctx, uint32_t flags, fidl_txn_t* txn) {
                static_cast<Coordinator*>(ctx)->Suspend(flags);
                return fuchsia_device_manager_AdministratorSuspend_reply(txn, ZX_OK);
            },
        };

        status = fidl_bind(dispatcher,
                           client_channel.release(),
                           reinterpret_cast<fidl_dispatch_t*>(
                               fuchsia_device_manager_Administrator_dispatch),
                           this->coordinator_,
                           &kOps);
        if (status != ZX_OK) {
            printf("Failed to bind to client channel: %d \n", status);
            return;
        }

    } else {
        printf("Request for unknown interface %s\n", interface_name);
    }
}

}  // namespace devmgr

