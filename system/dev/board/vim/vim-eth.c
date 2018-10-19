// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/ethernet.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include <limits.h>

#include "vim.h"

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
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &eth_phy_device,
        .data_size = sizeof(eth_dev_metadata_t),
    },
};

static const eth_dev_metadata_t eth_mac_device = {
    .vid = PDEV_VID_DESIGNWARE,
    .did = PDEV_DID_ETH_MAC,
};

static const pbus_metadata_t eth_board_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
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

static const pbus_dev_t eth_board_children[] = {
    // Designware MAC.
    {
        .name = "dwmac",
        .mmio_list = eth_mac_mmios,
        .mmio_count = countof(eth_mac_mmios),
        .bti_list = eth_mac_btis,
        .bti_count = countof(eth_mac_btis),
        .irq_list = eth_mac_irqs,
        .irq_count = countof(eth_mac_irqs),
        .metadata_list = eth_mac_device_metadata,
        .metadata_count = countof(eth_mac_device_metadata),
        .boot_metadata_list = eth_mac_metadata,
        .boot_metadata_count = countof(eth_mac_metadata),
    },
};

static pbus_dev_t eth_board_dev = {
    .name = "ethernet_mac",
    .vid = PDEV_VID_KHADAS,
    .pid = PDEV_PID_VIM2,
    .did = PDEV_DID_AMLOGIC_ETH,
    .mmio_list = eth_board_mmios,
    .mmio_count = countof(eth_board_mmios),
    .gpio_list = eth_board_gpios,
    .gpio_count = countof(eth_board_gpios),
    .i2c_channel_list = vim2_mcu_i2c,
    .i2c_channel_count = countof(vim2_mcu_i2c),
    .metadata_list = eth_board_metadata,
    .metadata_count = countof(eth_board_metadata),
    .child_list = eth_board_children,
    .child_count = countof(eth_board_children),
};

zx_status_t vim_eth_init(vim_bus_t* bus) {

    // setup pinmux for RGMII connections
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_MDIO, S912_ETH_MDIO_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_MDC, S912_ETH_MDC_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_RGMII_RX_CLK,
                               S912_ETH_RGMII_RX_CLK_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_RX_DV, S912_ETH_RX_DV_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_RXD0, S912_ETH_RXD0_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_RXD1, S912_ETH_RXD1_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_RXD2, S912_ETH_RXD2_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_RXD3, S912_ETH_RXD3_FN);

    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_RGMII_TX_CLK,
                               S912_ETH_RGMII_TX_CLK_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_TX_EN, S912_ETH_TX_EN_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_TXD0, S912_ETH_TXD0_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_TXD1, S912_ETH_TXD1_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_TXD2, S912_ETH_TXD2_FN);
    gpio_impl_set_alt_function(&bus->gpio, S912_ETH_TXD3, S912_ETH_TXD3_FN);

    zx_status_t status = pbus_device_add(&bus->pbus, &eth_board_dev);

    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_eth_init: pbus_device_add failed: %d\n", status);
    }
    return status;
}
