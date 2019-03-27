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
    explicit AmlClock(zx_device_t* device)
        : DeviceType(device){}
    // Performs the object initialization.
    static zx_status_t Create(zx_device_t* device);

    // CLK protocol implementation.
    zx_status_t ClockImplEnable(uint32_t clk);
    zx_status_t ClockImplDisable(uint32_t clk);

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
    zx_status_t InitPdev(zx_device_t* parent);
    // Clock measure helper API.
    zx_status_t ClkMeasureUtil(uint32_t clk, uint64_t* clk_freq);
    // MMIO helper APIs.
    zx_status_t InitHiuRegs(pdev_device_info_t* info);
    zx_status_t InitMsrRegs(pdev_device_info_t* info);
    // Platform device protocol.
    pdev_protocol_t pdev_;
    // IO MMIO
    std::optional<ddk::MmioBuffer> hiu_mmio_;
    std::optional<ddk::MmioBuffer> msr_mmio_;
    // Protects clock gate registers.
    fbl::Mutex lock_;
    // Clock gates.
    fbl::Array<meson_clk_gate_t> gates_;
    // Clock Table
    fbl::Array<const char* const> clk_table_;
    // MSR_CLK offsets/
    meson_clk_msr_t clk_msr_offsets_;
    // Booleans for feature support
    bool clk_gates_ = true;
    bool clk_msr_ = true;
};

} // namespace amlogic_clock
