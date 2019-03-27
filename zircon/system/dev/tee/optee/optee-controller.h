// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/tee/c/fidl.h>
#include <lib/zx/channel.h>
#include <zircon/thread_annotations.h>

#include "optee-message.h"
#include "optee-smc.h"
#include "shared-memory.h"

namespace optee {

class OpteeClient;

class OpteeController;
using OpteeControllerBase = ddk::Device<OpteeController, ddk::Messageable, ddk::Openable,
                                        ddk::Unbindable>;
using OpteeControllerProtocol = ddk::EmptyProtocol<ZX_PROTOCOL_TEE>;
class OpteeController : public OpteeControllerBase,
                        public OpteeControllerProtocol {
public:
    using RpcHandler = fbl::Function<zx_status_t(const RpcFunctionArgs&, RpcFunctionResult*)>;

    explicit OpteeController(zx_device_t* parent)
        : OpteeControllerBase(parent) {}

    OpteeController(const OpteeController&) = delete;
    OpteeController& operator=(const OpteeController&) = delete;

    zx_status_t Bind();

    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    zx_status_t DdkOpen(zx_device_t** out_dev, uint32_t flags);
    void DdkUnbind();
    void DdkRelease();

    // Connects a `fuchsia.tee.Device` protocol request.
    //
    // Parameters:
    //  * service_provider:  The (optional) client end of a channel to the
    //                       `fuchsia.tee.manager.ServiceProvider` protocol that provides service
    //                       support for the driver.
    //  * device_request:    The server end of a channel to the `fuchsia.tee.Device` protocol that
    //                       is requesting to be served.
    zx_status_t ConnectDevice(zx_handle_t service_provider, zx_handle_t device_request);

    // Client FIDL commands
    zx_status_t GetOsInfo(fidl_txn_t* txn) const;

    void RemoveClient(OpteeClient* client);

    uint32_t CallWithMessage(const optee::Message& message, RpcHandler rpc_handler);

    SharedMemoryManager::DriverMemoryPool* driver_pool() const {
        return shared_memory_manager_->driver_pool();
    }

    SharedMemoryManager::ClientMemoryPool* client_pool() const {
        return shared_memory_manager_->client_pool();
    }

private:
    zx_status_t ValidateApiUid() const;
    zx_status_t ValidateApiRevision() const;
    zx_status_t GetOsRevision();
    zx_status_t ExchangeCapabilities();
    void AddClient(OpteeClient* client);
    void CloseClients();
    zx_status_t InitializeSharedMemory();
    zx_status_t DiscoverSharedMemoryConfig(zx_paddr_t* out_start_addr, size_t* out_size);

    static fuchsia_hardware_tee_DeviceConnector_ops_t kFidlOps;

    pdev_protocol_t pdev_proto_ = {};
    zx_handle_t secure_monitor_ = ZX_HANDLE_INVALID;
    uint32_t secure_world_capabilities_ = 0;
    fuchsia_tee_OsRevision os_revision_ = {};
    fbl::Mutex clients_lock_;
    fbl::DoublyLinkedList<OpteeClient*> clients_ TA_GUARDED(clients_lock_);
    fbl::unique_ptr<SharedMemoryManager> shared_memory_manager_;
};

} // namespace optee
