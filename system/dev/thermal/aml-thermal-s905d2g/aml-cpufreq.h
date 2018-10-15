// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddktl/mmio.h>
#include <ddktl/protocol/clk.h>
#include <fbl/optional.h>
#include <hwreg/mmio.h>
#include <soc/aml-s905d2/s905d2-hiu.h>
#include <lib/zx/bti.h>

namespace thermal {
// This class handles the dynamic changing of
// CPU frequency.
class AmlCpuFrequency {

public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlCpuFrequency);
    AmlCpuFrequency(){};
    ~AmlCpuFrequency() = default;
    zx_status_t SetFrequency(uint32_t rate);
    zx_status_t Init(zx_device_t* parent);
    uint32_t GetFrequency();

private:
    zx_status_t WaitForBusy();
    zx_status_t ConfigureSysPLL(uint32_t new_rate);
    zx_status_t ConfigureFixedPLL(uint32_t new_rate);

    platform_device_protocol_t pdev_;
    // Initialize platform stuff.
    zx_status_t InitPdev(zx_device_t* parent);
    // Protocols.
    clk_protocol_t clk_protocol_;
    fbl::optional<ddk::ClkProtocolProxy> clk_;
    // MMIOS.
    fbl::optional<ddk::MmioBuffer> hiu_mmio_;
    // BTI handle.
    zx::bti bti_;
    // HIU Handle.
    aml_hiu_dev_t hiu_;
    // Sys PLL.
    aml_pll_dev_t sys_pll_;
    // Current Frequency, default is 1.2GHz,
    // which is set by u-boot while booting up.
    uint32_t current_rate_ = 1200000000;
};
} // namespace thermal
