// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>

#include <ddktl/device.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/clockimpl.h>

namespace hisi_clock {

// Forward-declared from dev/clk/hisi-lib/hisi-gate.h
class Gate;

class HisiClock;
using DeviceType = ddk::Device<HisiClock,
                               ddk::Unbindable>;

class HisiClock : public DeviceType,
                  public ddk::ClockImplProtocol<HisiClock, ddk::base_protocol> {
public:
    HisiClock(const HisiClock&) = delete;            // No Copies.
    HisiClock& operator=(const HisiClock&) = delete; // No Moves.

    static zx_status_t Create(const char* name, const Gate gates[],
                              const size_t gate_count, zx_device_t* parent);

    // Implement the Clock Protocol
    zx_status_t ClockImplEnable(uint32_t clock);
    zx_status_t ClockImplDisable(uint32_t clock);
    zx_status_t ClockImplRequestRate(uint32_t id, uint64_t hz);

    // Device Protocol Implementation.
    void DdkUnbind();
    void DdkRelease();

private:
    // Create instances via "Create"
    explicit HisiClock(zx_device_t* device, const Gate gates[],
                       const size_t gate_count)
        : DeviceType(device), gates_(gates), gate_count_(gate_count) {}

    // Init after construction, DeInit before destruction.
    zx_status_t Init();
    void DeInit();


    zx_status_t Toggle(uint32_t clock, bool enable);
    zx_status_t ToggleSepClkLocked(const Gate& gate, bool enable) __TA_REQUIRES(lock_);
    zx_status_t ToggleGateClkLocked(const Gate& gate, bool enable) __TA_REQUIRES(lock_);

    // Publish the clock protocol to the platform bus.
    zx_status_t RegisterClockProtocol();

    fbl::Mutex lock_;
    std::optional<ddk::MmioBuffer> peri_crg_mmio_ __TA_GUARDED(lock_);
    std::optional<ddk::MmioBuffer> sctrl_mmio_ __TA_GUARDED(lock_);

    const Gate* const gates_;
    const size_t gate_count_;
};

} // namespace hisi_clock
