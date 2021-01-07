// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/ethernet/c/banjo.h>
#include <limits.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim-gpios.h"
#include "vim.h"

namespace vim {

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
        .base = S912_HIU_BASE,
        .length = S912_HIU_LENGTH,
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
        .bti_id = BTI_ETHERNET,
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
    // c++ init error
    .pid = 0,
    // c++ init error
    .did = PDEV_DID_DESIGNWARE_ETH_MAC,
};

static const pbus_metadata_t eth_board_metadata[] = {
    {
        .type = DEVICE_METADATA_ETH_MAC_DEVICE,
        .data_buffer = &eth_mac_device,
        .data_size = sizeof(eth_dev_metadata_t),
    },
};

static pbus_dev_t eth_board_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "ethernet_mac";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S912;
  dev.did = PDEV_DID_AMLOGIC_ETH;
  dev.mmio_list = eth_board_mmios;
  dev.mmio_count = countof(eth_board_mmios);
  dev.metadata_list = eth_board_metadata;
  dev.metadata_count = countof(eth_board_metadata);
  return dev;
}();

static pbus_dev_t dwmac_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "dwmac";
  dev.vid = PDEV_VID_DESIGNWARE;
  dev.did = PDEV_DID_DESIGNWARE_ETH_MAC;
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

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

// Composite binding rules for ethernet board driver.
const zx_bind_inst_t i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 1),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x18),
};
static const zx_bind_inst_t gpio_reset_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_ETH_MAC_RST),
};
static const zx_bind_inst_t gpio_int_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_ETH_MAC_INTR),
};
static const device_fragment_part_t i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(i2c_match), i2c_match},
};
static const device_fragment_part_t gpio_reset_fragment[] = {
    {countof(root_match), root_match},
    {countof(gpio_reset_match), gpio_reset_match},
};
static const device_fragment_part_t gpio_int_fragment[] = {
    {countof(root_match), root_match},
    {countof(gpio_int_match), gpio_int_match},
};
static const device_fragment_t eth_fragments[] = {
    {"i2c", countof(i2c_fragment), i2c_fragment},
    {"gpio-int", countof(gpio_int_fragment), gpio_int_fragment},
    {"gpio-reset", countof(gpio_reset_fragment), gpio_reset_fragment},
};

// Composite binding rules for dwmac.
static const zx_bind_inst_t eth_board_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_ETH_BOARD),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_DESIGNWARE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_DESIGNWARE_ETH_MAC),
};
static const device_fragment_part_t eth_board_fragment[] = {
    {std::size(root_match), root_match},
    {std::size(eth_board_match), eth_board_match},
};
static const device_fragment_t dwmac_fragments[] = {
    {"eth-board", std::size(eth_board_fragment), eth_board_fragment},
};

zx_status_t Vim::EthInit() {
  // setup pinmux for RGMII connections
  gpio_impl_.SetAltFunction(S912_ETH_MDIO, S912_ETH_MDIO_FN);
  gpio_impl_.SetAltFunction(S912_ETH_MDC, S912_ETH_MDC_FN);
  gpio_impl_.SetAltFunction(S912_ETH_RGMII_RX_CLK, S912_ETH_RGMII_RX_CLK_FN);
  gpio_impl_.SetAltFunction(S912_ETH_RX_DV, S912_ETH_RX_DV_FN);
  gpio_impl_.SetAltFunction(S912_ETH_RXD0, S912_ETH_RXD0_FN);
  gpio_impl_.SetAltFunction(S912_ETH_RXD1, S912_ETH_RXD1_FN);
  gpio_impl_.SetAltFunction(S912_ETH_RXD2, S912_ETH_RXD2_FN);
  gpio_impl_.SetAltFunction(S912_ETH_RXD3, S912_ETH_RXD3_FN);

  gpio_impl_.SetAltFunction(S912_ETH_RGMII_TX_CLK, S912_ETH_RGMII_TX_CLK_FN);
  gpio_impl_.SetAltFunction(S912_ETH_TX_EN, S912_ETH_TX_EN_FN);
  gpio_impl_.SetAltFunction(S912_ETH_TXD0, S912_ETH_TXD0_FN);
  gpio_impl_.SetAltFunction(S912_ETH_TXD1, S912_ETH_TXD1_FN);
  gpio_impl_.SetAltFunction(S912_ETH_TXD2, S912_ETH_TXD2_FN);
  gpio_impl_.SetAltFunction(S912_ETH_TXD3, S912_ETH_TXD3_FN);

  // Add a composite device for ethernet board in a new devhost.
  auto status =
      pbus_.CompositeDeviceAdd(&eth_board_dev, eth_fragments, std::size(eth_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }

  // Add a composite device for dwmac driver in the ethernet board driver's devhost.
  status = pbus_.CompositeDeviceAdd(&dwmac_dev, dwmac_fragments, std::size(dwmac_fragments), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim
