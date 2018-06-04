// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/tee.h>
#include <zircon/device/tee.h>

namespace optee {

class OpteeController;
using OpteeControllerBase = ddk::Device<OpteeController, ddk::Ioctlable, ddk::Unbindable>;
using OpteeControllerProtocol = ddk::TeeProtocol<OpteeController>;

class OpteeController : public OpteeControllerBase,
                        public OpteeControllerProtocol {
public:
    explicit OpteeController(zx_device_t* parent)
        : OpteeControllerBase(parent) {}

    zx_status_t Bind();

    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

private:
    platform_device_protocol_t pdev_proto_ = {};
    // TODO(rjascani): Eventually, the secure_monitor_ object should be an owned resource object
    // created and provided to us by our parent. For now, we're simply stashing a copy of the
    // root resource so that we can make zx_smc_calls. We can make that switch when we can properly
    // craft a resource object dedicated only to secure monitor calls targetting the Trusted OS.
    zx_handle_t secure_monitor_ = ZX_HANDLE_INVALID;
    uint32_t secure_world_capabilities_ = 0;
    tee_revision_t os_revision_ = {};

    zx_status_t ValidateApiUid() const;
    zx_status_t ValidateApiRevision() const;
    zx_status_t GetOsRevision();
    zx_status_t ExchangeCapabilities();

    // IOCTL commands
    zx_status_t GetDescription(tee_ioctl_description_t* out_description, size_t* out_size) const;
};

} // namespace optee
