// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <fbl/mutex.h>
#include <fuchsia/hardware/clock/c/fidl.h>
#include <lib/mmio/mmio.h>
#include <zircon/thread_annotations.h>

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
    zx_status_t ClockImplRequestRate(uint32_t id, uint64_t hz);

    // Device Protocol Implementation.
    zx_status_t Bind();
    void DdkUnbind();
    void DdkRelease();

private:
    Msm8x53Clk(zx_device_t* parent)
        : DeviceType(parent) {}

    zx_status_t RegisterClockProtocol();

    // Gate Clocks
    zx_status_t GateClockEnable(uint32_t index);
    zx_status_t GateClockDisable(uint32_t index);

    // Branch Clocks
    zx_status_t BranchClockEnable(uint32_t index);
    zx_status_t BranchClockDisable(uint32_t index);
    enum class AwaitBranchClockStatus {
        Enabled,
        Disabled
    };
    // Wait for a change to a particular branch clock to take effect.
    zx_status_t AwaitBranchClock(AwaitBranchClockStatus s,
                                 const uint32_t cbcr_reg);

    // Voter Clocks
    zx_status_t VoterClockEnable(uint32_t index);
    zx_status_t VoterClockDisable(uint32_t index);

    fbl::Mutex lock_;       // Lock guards mmio_.
    std::optional<ddk::MmioBuffer> mmio_;
};

} // namespace clk
