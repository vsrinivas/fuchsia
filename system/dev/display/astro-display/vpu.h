// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <ddk/protocol/platform-device.h>
#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include "vpu-regs.h"
#include "common.h"


namespace astro_display {

class Vpu {
public:
    Vpu() {}
    ~Vpu() {
        io_buffer_release(&mmio_vpu_);
        io_buffer_release(&mmio_hhi_);
    }
    zx_status_t Init(zx_device_t* parent);
    // This function powers on VPU related blocks. The function contains undocumented
    // register and/or power-on sequences.
    void PowerOn();
    // This function powers off VPU related blocks. The function contains undocumented
    // register and/or power-off sequences.
    void PowerOff();
    // This function sets up default video post processing unit. It contains undocumented
    // registers and/or initialization sequences
    void VppInit();
private:
    // This function configures the VPU-related clocks. It contains undocumented registers
    // and/or clock initialization sequences
    void ConfigureClock();

    io_buffer_t                         mmio_vpu_;
    io_buffer_t                         mmio_hhi_;
    io_buffer_t                         mmio_aobus_;
    io_buffer_t                         mmio_cbus_;
    platform_device_protocol_t          pdev_ = {};
    fbl::unique_ptr<hwreg::RegisterIo>  vpu_regs_;
    fbl::unique_ptr<hwreg::RegisterIo>  hhi_regs_;
    fbl::unique_ptr<hwreg::RegisterIo>  aobus_regs_;
    fbl::unique_ptr<hwreg::RegisterIo>  cbus_regs_;

    bool                                initialized_ = false;

};
} // namespace astro_display
