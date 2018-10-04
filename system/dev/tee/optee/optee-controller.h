// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddktl/device.h>
#include <ddktl/protocol/tee.h>
#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <zircon/thread_annotations.h>
#include <zircon/tee/c/fidl.h>

#include "optee-message.h"
#include "optee-smc.h"
#include "shared-memory.h"

namespace optee {

class OpteeClient;

class OpteeController;
using OpteeControllerBase = ddk::Device<OpteeController, ddk::Openable, ddk::Unbindable>;
using OpteeControllerProtocol = ddk::TeeProtocol<OpteeController>;
class OpteeController : public OpteeControllerBase,
                        public OpteeControllerProtocol {
public:
    using RpcHandler = fbl::Function<zx_status_t(const RpcFunctionArgs&, RpcFunctionResult*)>;

    explicit OpteeController(zx_device_t* parent)
        : OpteeControllerBase(parent) {}

    OpteeController(const OpteeController&) = delete;
    OpteeController& operator=(const OpteeController&) = delete;

    zx_status_t Bind();

    zx_status_t DdkOpen(zx_device_t** out_dev, uint32_t flags);
    void DdkUnbind();
    void DdkRelease();

    // Client FIDL commands
    zx_status_t GetOsInfo(fidl_txn_t* txn) const;

    void RemoveClient(OpteeClient* client);

    uint32_t CallWithMessage(const Message& message, RpcHandler rpc_handler);

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

    pdev_protocol_t pdev_proto_ = {};
    // TODO(rjascani): Eventually, the secure_monitor_ object should be an owned resource object
    // created and provided to us by our parent. For now, we're simply stashing a copy of the
    // root resource so that we can make zx_smc_calls. We can make that switch when we can properly
    // craft a resource object dedicated only to secure monitor calls targetting the Trusted OS.
    zx_handle_t secure_monitor_ = ZX_HANDLE_INVALID;
    uint32_t secure_world_capabilities_ = 0;
    zircon_tee_OsRevision os_revision_ = {};
    fbl::Mutex clients_lock_;
    fbl::DoublyLinkedList<OpteeClient*> clients_ TA_GUARDED(clients_lock_);
    fbl::unique_ptr<SharedMemoryManager> shared_memory_manager_;
};

} // namespace optee
