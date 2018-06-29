// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include <limits.h>

#include "vim.h"

static const pbus_gpio_t eth_gpios[] = {
    {
        // MAC_RST
        .gpio = S912_GPIOZ(14),
    },
    {
        // MAC_INTR (need to wire up interrupt?)
        .gpio = S912_GPIOZ(15),
    },
};

static const pbus_irq_t eth_irqs[] = {
    {
        .irq = 40,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};


static const pbus_mmio_t vim2_eth_mmios[] = {
    {
        .base   = PERIPHS_REG_BASE,
        .length = PERIPHS_REG_SIZE,
    },
    {
        .base   = ETH_MAC_REG_BASE,
        .length = ETH_MAC_REG_SIZE,
    },
    {
        .base   = HHI_REG_BASE,
        .length = HHI_REG_SIZE,
    },
};

static const pbus_bti_t eth_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
};

static const pbus_metadata_t eth_metadata[] = {
    {
        .type = DEVICE_METADATA_MAC_ADDRESS,
        .extra = 0,
    },
};

static pbus_dev_t eth_dev = {
    .name = "ethernet",
    .vid = PDEV_VID_KHADAS,
    .pid = PDEV_PID_VIM2,
    .did = PDEV_DID_AMLOGIC_ETH,
    .mmios = vim2_eth_mmios,
    .mmio_count = countof(vim2_eth_mmios),
    .gpios = eth_gpios,
    .gpio_count = countof(eth_gpios),
    .btis = eth_btis,
    .bti_count = countof(eth_btis),
    .irqs = eth_irqs,
    .irq_count = countof(eth_irqs),
    .metadata = eth_metadata,
    .metadata_count = countof(eth_metadata),
};

zx_status_t vim_eth_init(vim_bus_t* bus) {

    // setup pinmux for RGMII connections
    gpio_set_alt_function(&bus->gpio, S912_ETH_MDIO,     S912_ETH_MDIO_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_MDC,      S912_ETH_MDC_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_RGMII_RX_CLK,
                                      S912_ETH_RGMII_RX_CLK_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_RX_DV,    S912_ETH_RX_DV_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_RXD0,     S912_ETH_RXD0_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_RXD1,     S912_ETH_RXD1_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_RXD2,     S912_ETH_RXD2_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_RXD3,     S912_ETH_RXD3_FN);

    gpio_set_alt_function(&bus->gpio, S912_ETH_RGMII_TX_CLK,
                                      S912_ETH_RGMII_TX_CLK_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_TX_EN,    S912_ETH_TX_EN_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_TXD0,     S912_ETH_TXD0_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_TXD1,     S912_ETH_TXD1_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_TXD2,     S912_ETH_TXD2_FN);
    gpio_set_alt_function(&bus->gpio, S912_ETH_TXD3,     S912_ETH_TXD3_FN);

    //Set reset line to output
    gpio_config(&bus->gpio, S912_GPIOZ(14), GPIO_DIR_OUT);

    zx_status_t status = pbus_device_add(&bus->pbus, &eth_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_eth_init: pbus_device_add failed: %d\n", status);
    }
    return status;
}
