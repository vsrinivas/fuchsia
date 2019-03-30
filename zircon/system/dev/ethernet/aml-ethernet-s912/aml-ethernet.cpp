// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-ethernet.h"
#include "aml-regs.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c-lib.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <soc/aml-s912/s912-hw.h>
#include <stdio.h>
#include <string.h>
#include <zircon/compiler.h>

namespace eth {

#define MCU_I2C_REG_BOOT_EN_WOL 0x21
#define MCU_I2C_REG_BOOT_EN_WOL_RESET_ENABLE 0x03

void AmlEthernet::EthBoardResetPhy() {
    gpios_[PHY_RESET].Write(0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    gpios_[PHY_RESET].Write(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
}

zx_status_t AmlEthernet::InitPdev() {
    if (!pdev_.is_valid()) {
        return ZX_ERR_NO_RESOURCES;
    }

    zx_status_t status;
    for (uint32_t i = 0; i < countof(gpios_); i++) {
        gpios_[i] = pdev_.GetGpio(i);
        if (!gpios_[i].is_valid()) {
            return ZX_ERR_NO_RESOURCES;
        }
    }

    // I2c for MCU messages.
    i2c_ = pdev_.GetI2c(0);
    if (!i2c_.is_valid()) {
        return ZX_ERR_NO_RESOURCES;
    }

    // Map amlogic peripheral control registers.
    status = pdev_.MapMmio(MMIO_PERIPH, &periph_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not map periph mmio: %d\n", status);
        return status;
    }

    // Map HHI regs (clocks and power domains).
    status = pdev_.MapMmio(MMIO_HHI, &hhi_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not map hiu mmio: %d\n", status);
        return status;
    }

    return status;
}

zx_status_t AmlEthernet::Bind() {
    // Set reset line to output
    gpios_[PHY_RESET].ConfigOut(0);

    // Initialize AMLogic peripheral registers associated with dwmac.
    //Sorry about the magic...rtfm
    periph_mmio_->Write32(0x1621, PER_ETH_REG0);
    periph_mmio_->Write32(0x20000, PER_ETH_REG1);

    periph_mmio_->Write32(REG2_ETH_REG2_REVERSED | REG2_INTERNAL_PHY_ID, PER_ETH_REG2);

    periph_mmio_->Write32(REG3_CLK_IN_EN | REG3_ETH_REG3_19_RESVERD |
                              REG3_CFG_PHY_ADDR | REG3_CFG_MODE |
                              REG3_CFG_EN_HIGH | REG3_ETH_REG3_2_RESERVED,
                          PER_ETH_REG3);

    // Enable clocks and power domain for dwmac
    hhi_mmio_->SetBits32(1 << 3, HHI_GCLK_MPEG1);
    hhi_mmio_->ClearBits32((1 << 3) | (1 << 2), HHI_MEM_PD_REG0);

    // WOL reset enable to MCU
    uint8_t write_buf[2] = {MCU_I2C_REG_BOOT_EN_WOL, MCU_I2C_REG_BOOT_EN_WOL_RESET_ENABLE};
    zx_status_t status = i2c_.WriteSync(write_buf, sizeof(write_buf));
    if (status) {
        zxlogf(ERROR, "aml-ethernet: WOL reset enable to MCU failed: %d\n", status);
        return status;
    }

    // Populate board specific information
    eth_dev_metadata_t mac_info;
    size_t actual;
    status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &mac_info,
                                 sizeof(eth_dev_metadata_t), &actual);
    if (status != ZX_OK || actual != sizeof(eth_dev_metadata_t)) {
        zxlogf(ERROR, "aml-ethernet: Could not get MAC metadata %d\n", status);
        return status;
    }

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, mac_info.vid},
        {BIND_PLATFORM_DEV_DID, 0, mac_info.did},
    };

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "aml-ethernet";
    args.ctx = this;
    args.ops = &ddk_device_proto_;
    args.proto_id = ddk_proto_id_;
    args.proto_ops = ddk_proto_ops_;
    args.props = props;
    args.prop_count = countof(props);

    return pdev_.DeviceAdd(0, &args, &zxdev_);
}

void AmlEthernet::DdkUnbind() {
    DdkRemove();
}

void AmlEthernet::DdkRelease() {
    delete this;
}

zx_status_t AmlEthernet::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto eth_device = fbl::make_unique_checked<AmlEthernet>(&ac, parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = eth_device->InitPdev();
    if (status != ZX_OK) {
        return status;
    }

    status = eth_device->Bind();
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-ethernet driver failed to get added: %d\n", status);
        return status;
    } else {
        zxlogf(INFO, "aml-ethernet driver added\n");
    }

    // eth_device intentionally leaked as it is now held by DevMgr
    __UNUSED auto ptr = eth_device.release();

    return ZX_OK;
}

} // namespace eth

extern "C" zx_status_t aml_eth_bind(void* ctx, zx_device_t* parent) {
    return eth::AmlEthernet::Create(parent);
}
