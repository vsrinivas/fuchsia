// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <fbl/mutex.h>
#include <fuchsia/hardware/clock/c/fidl.h>
#include <lib/mmio/mmio.h>

namespace clk {

class Msm8x53Clk;
using DeviceType = ddk::Device<Msm8x53Clk, ddk::Unbindable>;

class Msm8x53Clk : public DeviceType,
                   public ddk::ClockImplProtocol<Msm8x53Clk, ddk::base_protocol> {
public:
    static zx_status_t Create(void* ctx, zx_device_t* parent);

    zx_status_t Init();

    // Clock Protocol Implementation
    zx_status_t ClockImplEnable(uint32_t index);
    zx_status_t ClockImplDisable(uint32_t index);

    // Device Protocol Implementation.
    zx_status_t Bind();
    void DdkUnbind();
    void DdkRelease();

private:
    Msm8x53Clk(zx_device_t* parent)
        : DeviceType(parent) {}

    zx_status_t RegisterClockProtocol();

    std::optional<ddk::MmioBuffer> mmio_ __TA_GUARDED(local_clock_mutex_);

    // Protects access to all clock gates
    // NOTE(gkalsi): We want finer grain locking once we add more lock classes.
    fbl::Mutex local_clock_mutex_;
};

} // namespace clk
