// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/device.h>
#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include <lib/zx/interrupt.h>

namespace thermal {

class AmlTSensor {

public:
    uint32_t ReadTemperature();
    zx_status_t InitSensor(zx_device_t* parent);
    void ShutDown();

 private:
    zx_status_t InitPdev(zx_device_t* parent);
    uint32_t TempToCode(uint32_t temp, bool trend);
    uint32_t CodeToTemp(uint32_t temp_code);
    void SetRebootTemperature(uint32_t temp);

    uint32_t                                trim_info_;

    platform_device_protocol_t              pdev_;

    io_buffer_t                             pll_mmio_;
    io_buffer_t                             ao_mmio_;
    io_buffer_t                             hiu_mmio_;

    zx::interrupt                           tsensor_irq_;

    fbl::unique_ptr<hwreg::RegisterIo>      pll_regs_;
    fbl::unique_ptr<hwreg::RegisterIo>      ao_regs_;
    fbl::unique_ptr<hwreg::RegisterIo>      hiu_regs_;
};
}  // namespace thermal
