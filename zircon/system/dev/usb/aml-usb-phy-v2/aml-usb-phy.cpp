// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-usb-phy.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-g12-reset.h>

#include "usb-phy-regs.h"

namespace aml_usb_phy {

// Based on set_usb_pll() in phy-aml-new-usb2-v2.c
void AmlUsbPhy::SetupPLL(ddk::MmioBuffer* mmio) {
    PLL_REGISTER::Get(0x40)
        .FromValue(0x30000000 | pll_settings_[0])
        .WriteTo(mmio);

    PLL_REGISTER::Get(0x44)
        .FromValue(pll_settings_[1])
        .WriteTo(mmio);

    PLL_REGISTER::Get(0x48)
        .FromValue(pll_settings_[2])
        .WriteTo(mmio);

    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

    PLL_REGISTER::Get(0x40)
        .FromValue(0x10000000 | pll_settings_[0])
        .WriteTo(mmio);

    // PLL

    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

    PLL_REGISTER::Get(0x50)
        .FromValue(pll_settings_[3])
        .WriteTo(mmio);

    PLL_REGISTER::Get(0x10)
        .FromValue(pll_settings_[4])
        .WriteTo(mmio);

    // Recovery state
    PLL_REGISTER::Get(0x38)
        .FromValue(0)
        .WriteTo(mmio);

    PLL_REGISTER::Get(0x34)
        .FromValue(pll_settings_[5])
        .WriteTo(mmio);

    // Disconnect threshold
    PLL_REGISTER::Get(0xc)
        .FromValue(0x3c)
        .WriteTo(mmio);

    // Tuning

    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

    PLL_REGISTER::Get(0x38)
        .FromValue(pll_settings_[6])
        .WriteTo(mmio);

    PLL_REGISTER::Get(0x34)
        .FromValue(pll_settings_[5])
        .WriteTo(mmio);

    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
}

zx_status_t AmlUsbPhy::InitPhy() {
    // first reset USB
    auto reset_1_level = aml_reset::RESET_1::GetLevel().ReadFrom(&*reset_mmio_);
    // The bits being manipulated here are not documented.
    reset_1_level.set_reg_value(reset_1_level.reg_value() | (0x3 << 16));
    reset_1_level.WriteTo(&*reset_mmio_);

    // amlogic_new_usbphy_reset_v2()
    auto reset_1 = aml_reset::RESET_1::Get().ReadFrom(&*reset_mmio_);
    reset_1.set_usb(1);
    reset_1.WriteTo(&*reset_mmio_);
    // FIXME(voydanoff) this delay is very long, but it is what the Amlogic Linux kernel is doing.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(500)));

    // amlogic_new_usb2_init()
    for (int i = 0; i < 2; i++) {
        auto u2p_r0 = U2P_R0_V2::Get(i).ReadFrom(&*usbctrl_mmio_);
        u2p_r0.set_por(1);
        u2p_r0.set_host_device(1);
        if (i == 1) {
            u2p_r0.set_idpullup0(1);
            u2p_r0.set_drvvbus0(1);
        }
        u2p_r0.WriteTo(&*usbctrl_mmio_);

        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

        // amlogic_new_usbphy_reset_phycfg_v2()
        reset_1.ReadFrom(&*reset_mmio_);
        // The bit being manipulated here is not documented.
        reset_1.set_reg_value(reset_1.reg_value() | (1 << 16));
        reset_1.WriteTo(&*reset_mmio_);

        zx_nanosleep(zx_deadline_after(ZX_USEC(50)));

        auto u2p_r1 = U2P_R1_V2::Get(i);

        int count = 0;
        while (!u2p_r1.ReadFrom(&*usbctrl_mmio_).phy_rdy()) {
            // wait phy ready max 1ms, common is 100us
            if (count > 200) {
                zxlogf(ERROR, "AmlUsbPhy::InitPhy U2P_R1_PHY_RDY wait failed\n");
                break;
            }

            count++;
            zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
        }
    }

    // set up PLLs
    SetupPLL(&*usbphy20_mmio_);
    SetupPLL(&*usbphy30_mmio_);

    return ZX_OK;
}

zx_status_t AmlUsbPhy::Create(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<AmlUsbPhy>(&ac, parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = dev->Init();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = dev.release();
    return ZX_OK;
}

zx_status_t AmlUsbPhy::Init() {
    if (!pdev_.is_valid()) {
        zxlogf(ERROR, "AmlUsbPhy::Init: could not get platform device protocol\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    size_t actual;
    auto status = DdkGetMetadata(DEVICE_METADATA_PRIVATE, pll_settings_, sizeof(pll_settings_),
                                 &actual);
    if (status != ZX_OK || actual != sizeof(pll_settings_)) {
        zxlogf(ERROR, "AmlUsbPhy::Init could not get metadata for PLL settings\n");
        return ZX_ERR_INTERNAL;
    }

    status = pdev_.MapMmio(0, &reset_mmio_);
    if (status != ZX_OK) {
        return status;
    }
    status = pdev_.MapMmio(1, &usbctrl_mmio_);
    if (status != ZX_OK) {
        return status;
    }
    status = pdev_.MapMmio(2, &usbphy20_mmio_);
    if (status != ZX_OK) {
        return status;
    }
    status = pdev_.MapMmio(3, &usbphy30_mmio_);
    if (status != ZX_OK) {
        return status;
    }

    status = InitPhy();
    if (status != ZX_OK) {
        return status;
    }

    return DdkAdd("aml-usb-phy-v2", 0, nullptr, 0, ZX_PROTOCOL_USB_PHY);
}

void AmlUsbPhy::DdkRelease() {
    delete this;
}

static constexpr zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = AmlUsbPhy::Create;
    return ops;
}();

} // namespace aml_usb_phy

ZIRCON_DRIVER_BEGIN(aml_usb_phy, aml_usb_phy::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AML_USB_PHY_V2),
ZIRCON_DRIVER_END(aml_usb_phy)
