// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if __cplusplus

#include <ddktl/device.h>
#include <ddktl/protocol/display.h>

namespace i915 {

class Device;
using DeviceType = ddk::Device<Device, ddk::Openable, ddk::Closable>;

class Device: public DeviceType, public ddk::DisplayProtocol<Device> {
public:
    Device(zx_device_t* parent);
    ~Device();

    zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
    zx_status_t DdkClose(uint32_t flags);
    void DdkRelease();
    zx_status_t Bind();

    zx_status_t SetMode(zx_display_info_t* info);
    zx_status_t GetMode(zx_display_info_t* info);
    zx_status_t GetFramebuffer(void** framebuffer);
    void Flush();
    void AcquireOrReleaseDisplay(bool acquire);
    void SetOwnershipChangeCallback(zx_display_cb_t callback, void* cookie);

private:
    void EnableBacklight(bool enable);

    uintptr_t regs_;
    uint64_t regs_size_;
    zx_handle_t regs_handle_;

    void* framebuffer_;
    uint64_t framebuffer_size_;
    zx_handle_t framebuffer_handle_;

    zx_display_info_t info_;
    uint32_t flags_;
};

}

#endif // __cplusplus

__BEGIN_CDECLS
zx_status_t intel_i915_bind(void* ctx, zx_device_t* parent);
__END_CDECLS
