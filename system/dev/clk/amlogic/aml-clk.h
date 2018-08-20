// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "aml-clk-blocks.h"
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/clk.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include <zircon/thread_annotations.h>

namespace amlogic_clock {

class AmlClock;
using DeviceType = ddk::Device<AmlClock,
                               ddk::Unbindable>;

class AmlClock : public DeviceType,
                 public ddk::ClkProtocol<AmlClock> {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlClock);
    AmlClock(zx_device_t* device)
        : DeviceType(device){};
    static zx_status_t Create(zx_device_t* device);

    // Protocol Ops.
    zx_status_t ClkEnable(uint32_t clk);
    zx_status_t ClkDisable(uint32_t clk);

    // Ddk Hooks.
    void DdkUnbind();
    void DdkRelease();

    void ShutDown();

private:
    // Toggle clocks enable bit.
    zx_status_t ClkToggle(uint32_t clk, const bool enable);
    // Initialize platform device.
    zx_status_t InitPdev(zx_device_t* parent);
    // Platform device protocol.
    platform_device_protocol_t pdev_;
    // Clock protocol.
    clk_protocol_t clk_;
    io_buffer_t hiu_mmio_;
    fbl::unique_ptr<hwreg::RegisterIo> hiu_regs_;
    // Protects clock gate registers.
    fbl::Mutex lock_;
    // Clock gates.
    fbl::Array<meson_clk_gate_t> gates_;
};

} // namespace amlogic_clock
