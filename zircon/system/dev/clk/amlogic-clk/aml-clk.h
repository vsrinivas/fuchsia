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
#include <lib/mmio/mmio.h>
#include <ddktl/protocol/clk.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <hwreg/mmio.h>
#include <zircon/device/clk.h>
#include <zircon/thread_annotations.h>

#include <optional>

namespace amlogic_clock {

class AmlClock;
using DeviceType = ddk::Device<AmlClock,
                               ddk::Unbindable,
                               ddk::Ioctlable>;

class AmlClock : public DeviceType,
                 public ddk::ClkProtocol<AmlClock, ddk::base_protocol> {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlClock);
    explicit AmlClock(zx_device_t* device)
        : DeviceType(device){};
    // Performs the object initialization.
    static zx_status_t Create(zx_device_t* device);

    // CLK protocol implementation.
    zx_status_t ClkEnable(uint32_t clk);
    zx_status_t ClkDisable(uint32_t clk);

    // CLK IOCTL implementation.
    zx_status_t ClkMeasure(uint32_t clk, clk_freq_info_t* info);

    // Device protocol implementation.
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf,
                         size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);

    void ShutDown();

private:
    // Toggle clocks enable bit.
    zx_status_t ClkToggle(uint32_t clk, const bool enable);
    // Initialize platform device.
    zx_status_t InitPdev(zx_device_t* parent);
    // Clock measure helper API.
    zx_status_t ClkMeasureUtil(uint32_t clk, uint32_t* clk_freq);
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
