// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/tee.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <zircon/device/tee.h>
#include <zircon/thread_annotations.h>

namespace optee {

class OpteeClient;

class OpteeController;
using OpteeControllerBase = ddk::Device<OpteeController, ddk::Openable, ddk::Unbindable>;
using OpteeControllerProtocol = ddk::TeeProtocol<OpteeController>;

class OpteeController : public OpteeControllerBase,
                        public OpteeControllerProtocol {
public:
    explicit OpteeController(zx_device_t* parent)
        : OpteeControllerBase(parent) {}

    OpteeController(const OpteeController&) = delete;
    OpteeController& operator=(const OpteeController&) = delete;

    zx_status_t Bind();

    zx_status_t DdkOpen(zx_device_t** out_dev, uint32_t flags);
    void DdkUnbind();
    void DdkRelease();

    // Client IOCTL commands
    zx_status_t GetDescription(tee_ioctl_description_t* out_description, size_t* out_size) const;

    void RemoveClient(OpteeClient* client);

private:
    zx_status_t ValidateApiUid() const;
    zx_status_t ValidateApiRevision() const;
    zx_status_t GetOsRevision();
    zx_status_t ExchangeCapabilities();
    void AddClient(OpteeClient* client);
    void CloseClients();

    platform_device_protocol_t pdev_proto_ = {};
    // TODO(rjascani): Eventually, the secure_monitor_ object should be an owned resource object
    // created and provided to us by our parent. For now, we're simply stashing a copy of the
    // root resource so that we can make zx_smc_calls. We can make that switch when we can properly
    // craft a resource object dedicated only to secure monitor calls targetting the Trusted OS.
    zx_handle_t secure_monitor_ = ZX_HANDLE_INVALID;
    uint32_t secure_world_capabilities_ = 0;
    tee_revision_t os_revision_ = {};
    fbl::Mutex lock_;
    fbl::DoublyLinkedList<OpteeClient*> clients_ TA_GUARDED(lock_);
};

} // namespace optee
