// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/pdev.h>
#include <ddktl/device.h>
#include <lib/mmio/mmio.h>

namespace aml_usb_phy {

class AmlUsbPhy;
using AmlUsbPhyType = ddk::Device<AmlUsbPhy>;

// This is the main class for the platform bus driver.
class AmlUsbPhy : public AmlUsbPhyType {
public:
    explicit AmlUsbPhy(zx_device_t* parent)
        : AmlUsbPhyType(parent), pdev_(parent) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    // Device protocol implementation.
    void DdkRelease();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(AmlUsbPhy);

    void SetupPLL(ddk::MmioBuffer* mmio);
    zx_status_t InitPhy();
    zx_status_t Init();

    ddk::PDev pdev_;
    std::optional<ddk::MmioBuffer> reset_mmio_;
    std::optional<ddk::MmioBuffer> usbctrl_mmio_;
    std::optional<ddk::MmioBuffer> usbphy20_mmio_;
    std::optional<ddk::MmioBuffer> usbphy30_mmio_;

    // Magic numbers for PLL from metadata
    uint32_t pll_settings_[8];
};

} // namespace aml_usb_phy
