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
#include <ddk/protocol/platform-device.h>
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

void AmlEthernet::ResetPhy(void* ctx) {
    auto& self = *static_cast<AmlEthernet*>(ctx);
    gpio_write(&self.gpios_[PHY_RESET], 0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    gpio_write(&self.gpios_[PHY_RESET], 1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
}

zx_status_t AmlEthernet::InitPdev(zx_device_t* parent) {

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev_);
    if (status != ZX_OK) {
        return status;
    }

    for (uint32_t i = 0; i < countof(gpios_); i++) {
        status = pdev_get_protocol(&pdev_, ZX_PROTOCOL_GPIO, i, &gpios_[i]);
        if (status != ZX_OK) {
            return status;
        }
    }

    // I2c for MCU messages.
    status = device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c_);
    if (status != ZX_OK) {
        return status;
    }

    // Map amlogic peripheral control registers.
    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer2(&pdev_, MMIO_PERIPH, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not map periph mmio: %d\n", status);
        return status;
    }
    periph_mmio_ = ddk::MmioBuffer(mmio);


    // Map HHI regs (clocks and power domains).
    status = pdev_map_mmio_buffer2(&pdev_, MMIO_HHI, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-dwmac: could not map hiu mmio: %d\n", status);
        return status;
    }
    hhi_mmio_ = ddk::MmioBuffer(mmio);

    return status;
}

void AmlEthernet::ReleaseBuffers() {
    periph_mmio_.reset();
    hhi_mmio_.reset();
}

static void DdkUnbind(void* ctx) {
    auto& self = *static_cast<AmlEthernet*>(ctx);
    device_remove(self.device_);
}

static void DdkRelease(void* ctx) {
    auto& self = *static_cast<AmlEthernet*>(ctx);
    self.ReleaseBuffers();
    delete &self;
}

static eth_board_protocol_ops_t proto_ops = {
    .reset_phy = AmlEthernet::ResetPhy,
};

static zx_protocol_device_t eth_device_ops = []() {
    zx_protocol_device_t result;

    result.version = DEVICE_OPS_VERSION;
    result.unbind = &DdkUnbind;
    result.release = &DdkRelease;
    return result;
}();

static device_add_args_t eth_mac_dev_args = []() {
    device_add_args_t result;

    result.version = DEVICE_ADD_ARGS_VERSION;
    result.name = "aml-ethernet";
    result.ops = &eth_device_ops;
    result.proto_id = ZX_PROTOCOL_ETH_BOARD;
    result.proto_ops = &proto_ops;
    return result;
}();

zx_status_t AmlEthernet::Create(zx_device_t* device) {
    fbl::AllocChecker ac;
    auto eth_device = fbl::make_unique_checked<AmlEthernet>(&ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = eth_device->InitPdev(device);
    if (status != ZX_OK) {
        return status;
    }

    // Set reset line to output
    gpio_config_out(&eth_device->gpios_[PHY_RESET], 0);

    // Initialize AMLogic peripheral registers associated with dwmac.
    auto& pregs = eth_device->periph_mmio_;
    //Sorry about the magic...rtfm
    pregs->Write32(0x1621, PER_ETH_REG0);
    pregs->Write32(0x20000, PER_ETH_REG1);

    pregs->Write32(REG2_ETH_REG2_REVERSED | REG2_INTERNAL_PHY_ID, PER_ETH_REG2);

    pregs->Write32(REG3_CLK_IN_EN | REG3_ETH_REG3_19_RESVERD |
                       REG3_CFG_PHY_ADDR | REG3_CFG_MODE |
                       REG3_CFG_EN_HIGH | REG3_ETH_REG3_2_RESERVED,
                   PER_ETH_REG3);

    // Enable clocks and power domain for dwmac
    auto& hregs = eth_device->hhi_mmio_;
    hregs->SetBits32(1 << 3, HHI_GCLK_MPEG1);
    hregs->ClearBits32((1 << 3) | (1 << 2), HHI_MEM_PD_REG0);

    // WOL reset enable to MCU
    uint8_t write_buf[2] = {MCU_I2C_REG_BOOT_EN_WOL, MCU_I2C_REG_BOOT_EN_WOL_RESET_ENABLE};
    status = i2c_write_sync(&eth_device->i2c_, write_buf, sizeof(write_buf));
    if (status) {
        zxlogf(ERROR, "aml-ethernet: WOL reset enable to MCU failed: %d\n", status);
        return status;
    }

    // Populate board specific information
    eth_dev_metadata_t mac_info;
    size_t actual;
    status = device_get_metadata(device, DEVICE_METADATA_PRIVATE, &mac_info,
                                 sizeof(eth_dev_metadata_t), &actual);
    if (status != ZX_OK || actual != sizeof(eth_dev_metadata_t)) {
        zxlogf(ERROR, "aml-ethernet: Could not get MAC metadata %d\n", status);
        return status;
    }

    static zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, mac_info.vid},
        {BIND_PLATFORM_DEV_DID, 0, mac_info.did},
    };

    eth_mac_dev_args.props = props;
    eth_mac_dev_args.prop_count = countof(props);
    eth_mac_dev_args.ctx = eth_device.get();

    status = pdev_device_add(&eth_device->pdev_, 0, &eth_mac_dev_args, &eth_device->device_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-ethernet driver failed to get added\n");
        return status;
    } else {
        zxlogf(INFO, "aml-ethernet driver added\n");
    }

    // eth_device intentionally leaked as it is now held by DevMgr
    __UNUSED auto ptr = eth_device.release();

    return ZX_OK;
}

} // namespace eth

extern "C" zx_status_t aml_eth_bind(void* ctx, zx_device_t* device) {
    return eth::AmlEthernet::Create(device);
}
