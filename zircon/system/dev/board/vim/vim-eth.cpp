// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/ethernet.h>
#include <fbl/algorithm.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include <limits.h>

#include "vim.h"
namespace vim {
static const pbus_gpio_t eth_board_gpios[] = {
    {
        // MAC_RST
        .gpio = S912_GPIOZ(14),
    },
    {
        // MAC_INTR (need to wire up interrupt?)
        .gpio = S912_GPIOZ(15),
    },
};

static const pbus_irq_t eth_mac_irqs[] = {
    {
        .irq = S912_ETH_GMAC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_mmio_t eth_board_mmios[] = {
    {
        .base = PERIPHS_REG_BASE,
        .length = PERIPHS_REG_SIZE,
    },
    {
        .base = HHI_REG_BASE,
        .length = HHI_REG_SIZE,
    },
};

static const pbus_mmio_t eth_mac_mmios[] = {
    {
        .base = ETH_MAC_REG_BASE,
        .length = ETH_MAC_REG_SIZE,
    },
};

static const pbus_bti_t eth_mac_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
};

static const pbus_boot_metadata_t eth_mac_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = 0,
    },
};

static const eth_dev_metadata_t eth_phy_device = {
    .vid = PDEV_VID_REALTEK,
    .pid = PDEV_PID_RTL8211F,
    .did = PDEV_DID_ETH_PHY,
};

static const pbus_metadata_t eth_mac_device_metadata[] = {
    {
        .type = DEVICE_METADATA_ETH_PHY_DEVICE,
        .data_buffer = &eth_phy_device,
        .data_size = sizeof(eth_dev_metadata_t),
    },
};

static const eth_dev_metadata_t eth_mac_device = {
    .vid = PDEV_VID_DESIGNWARE,
    //c++ init error
    .pid = 0,
    //c++ init error
    .did = PDEV_DID_ETH_MAC,
};

static const pbus_metadata_t eth_board_metadata[] = {
    {
        .type = DEVICE_METADATA_ETH_MAC_DEVICE,
        .data_buffer = &eth_mac_device,
        .data_size = sizeof(eth_dev_metadata_t),
    },
};

static const pbus_i2c_channel_t vim2_mcu_i2c[] = {
    {
        .bus_id = 1,
        .address = 0x18,
    },
};

static pbus_dev_t eth_board_dev = [](){
    pbus_dev_t dev;
    dev.name = "ethernet_mac";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_AMLOGIC_S912;
    dev.did = PDEV_DID_AMLOGIC_ETH;
    dev.mmio_list = eth_board_mmios;
    dev.mmio_count = countof(eth_board_mmios);
    dev.gpio_list = eth_board_gpios;
    dev.gpio_count = countof(eth_board_gpios);
    dev.i2c_channel_list = vim2_mcu_i2c;
    dev.i2c_channel_count = countof(vim2_mcu_i2c);
    dev.metadata_list = eth_board_metadata;
    dev.metadata_count = countof(eth_board_metadata);
    return dev;
}();

static pbus_dev_t dwmac_dev = [](){
    pbus_dev_t dev;
    dev.name = "dwmac";
    dev.vid = PDEV_VID_DESIGNWARE;
    dev.did = PDEV_DID_ETH_MAC;
    dev.mmio_list = eth_mac_mmios;
    dev.mmio_count = countof(eth_mac_mmios);
    dev.irq_list = eth_mac_irqs;
    dev.irq_count = countof(eth_mac_irqs);
    dev.bti_list = eth_mac_btis;
    dev.bti_count = countof(eth_mac_btis);
    dev.metadata_list = eth_mac_device_metadata;
    dev.metadata_count = countof(eth_mac_device_metadata);
    dev.boot_metadata_list = eth_mac_metadata;
    dev.boot_metadata_count = countof(eth_mac_metadata);
    return dev;
}();

// Composite binding rules for ethernet driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t eth_board_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_ETH_BOARD),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_DESIGNWARE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ETH_MAC),
};
static const device_component_part_t eth_board_component[] = {
    { fbl::count_of(root_match), root_match },
    { fbl::count_of(eth_board_match), eth_board_match },
};
static const device_component_t components[] = {
    { fbl::count_of(eth_board_component), eth_board_component },
};

zx_status_t Vim::EthInit() {
    // setup pinmux for RGMII connections
    gpio_impl_.SetAltFunction(S912_ETH_MDIO, S912_ETH_MDIO_FN);
    gpio_impl_.SetAltFunction(S912_ETH_MDC, S912_ETH_MDC_FN);
    gpio_impl_.SetAltFunction(S912_ETH_RGMII_RX_CLK,
                              S912_ETH_RGMII_RX_CLK_FN);
    gpio_impl_.SetAltFunction(S912_ETH_RX_DV, S912_ETH_RX_DV_FN);
    gpio_impl_.SetAltFunction(S912_ETH_RXD0, S912_ETH_RXD0_FN);
    gpio_impl_.SetAltFunction(S912_ETH_RXD1, S912_ETH_RXD1_FN);
    gpio_impl_.SetAltFunction(S912_ETH_RXD2, S912_ETH_RXD2_FN);
    gpio_impl_.SetAltFunction(S912_ETH_RXD3, S912_ETH_RXD3_FN);

    gpio_impl_.SetAltFunction(S912_ETH_RGMII_TX_CLK,
                              S912_ETH_RGMII_TX_CLK_FN);
    gpio_impl_.SetAltFunction(S912_ETH_TX_EN, S912_ETH_TX_EN_FN);
    gpio_impl_.SetAltFunction(S912_ETH_TXD0, S912_ETH_TXD0_FN);
    gpio_impl_.SetAltFunction(S912_ETH_TXD1, S912_ETH_TXD1_FN);
    gpio_impl_.SetAltFunction(S912_ETH_TXD2, S912_ETH_TXD2_FN);
    gpio_impl_.SetAltFunction(S912_ETH_TXD3, S912_ETH_TXD3_FN);

    auto status = pbus_.DeviceAdd(&eth_board_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_device_add failed: %d\n", __func__, status);
        return status;
    }

    // Add a composite device for dwmac driver.
    status = pbus_.CompositeDeviceAdd(&dwmac_dev, components, fbl::count_of(components));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}
} //namespace vim