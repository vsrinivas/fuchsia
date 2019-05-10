// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "aml-clk-blocks.h"
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <hwreg/mmio.h>
#include <lib/mmio/mmio.h>
#include <fuchsia/hardware/clock/c/fidl.h>
#include <zircon/thread_annotations.h>

#include <optional>

namespace amlogic_clock {

class AmlClock;
using DeviceType = ddk::Device<AmlClock,
                               ddk::Unbindable,
                               ddk::Messageable>;

class AmlClock : public DeviceType,
                 public ddk::ClockImplProtocol<AmlClock, ddk::base_protocol> {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlClock);
    AmlClock(zx_device_t* device,
             ddk::MmioBuffer hiu_mmio,
             std::optional<ddk::MmioBuffer> msr_mmio,
             uint32_t device_id);
    // Performs the object initialization.
    static zx_status_t Create(zx_device_t* device);

    // CLK protocol implementation.
    zx_status_t ClockImplEnable(uint32_t clk);
    zx_status_t ClockImplDisable(uint32_t clk);
    zx_status_t ClockImplRequestRate(uint32_t id, uint64_t hz);

    // CLK IOCTL implementation.
    zx_status_t ClkMeasure(uint32_t clk, fuchsia_hardware_clock_FrequencyInfo* info);
    uint32_t GetClkCount();

    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

    // Device protocol implementation.
    void DdkUnbind();
    void DdkRelease();

    void ShutDown();

private:
    // Toggle clocks enable bit.
    zx_status_t ClkToggle(uint32_t clk, const bool enable);
    // Initialize platform device.
    zx_status_t Init(uint32_t did);
    // Clock measure helper API.
    zx_status_t ClkMeasureUtil(uint32_t clk, uint64_t* clk_freq);

    // IO MMIO
    ddk::MmioBuffer hiu_mmio_;
    std::optional<ddk::MmioBuffer> msr_mmio_;
    // Protects clock gate registers.
    // Clock gates.
    fbl::Mutex lock_;
    const meson_clk_gate_t* gates_ = nullptr;
    size_t gate_count_ = 0;
    // Clock Table
    const char* const* clk_table_ = nullptr;
    size_t clk_table_count_ = 0;
    // MSR_CLK offsets/
    meson_clk_msr_t clk_msr_offsets_;
};

} // namespace amlogic_clock
