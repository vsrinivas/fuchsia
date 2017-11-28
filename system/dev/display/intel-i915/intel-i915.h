// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if __cplusplus

#include <fbl/unique_ptr.h>

#include <zx/vmo.h>

#include "display-device.h"
#include "gtt.h"
#include "mmio-space.h"

namespace i915 {

class Controller;
using DeviceType = ddk::Device<Controller, ddk::Unbindable>;

class Controller : public DeviceType {
public:
    Controller(zx_device_t* parent);
    ~Controller();

    void DdkUnbind();
    void DdkRelease();
    zx_status_t Bind();

    MmioSpace* mmio_space() { return mmio_space_.get(); }
    Gtt* gtt() { return &gtt_; }

private:
    void EnableBacklight(bool enable);
    zx_status_t InitDisplays();

    Gtt gtt_;

    fbl::unique_ptr<MmioSpace> mmio_space_;
    zx_handle_t regs_handle_;

    // Reference to display, owned by devmgr - will always be valid when non-null.
    DisplayDevice* display_device_;

    uint32_t flags_;
};

} // namespace i915

#endif // __cplusplus

__BEGIN_CDECLS
zx_status_t intel_i915_bind(void* ctx, zx_device_t* parent);
__END_CDECLS
