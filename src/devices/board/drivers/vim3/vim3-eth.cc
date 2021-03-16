// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/ethernet/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <limits.h>

#include <lib/ddk/metadata.h>
#include <fbl/algorithm.h>
#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>

#include "vim3-gpios.h"
#include "vim3.h"

namespace vim3 {

static const pbus_irq_t eth_mac_irqs[] = {
    {
        .irq = A311D_ETH_GMAC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_mmio_t eth_board_mmios[] = {
    {
        .base = A311D_PERIPHERALS_BASE,
        .length = A311D_PERIPHERALS_LENGTH,
    },
    {
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    },
};

static const pbus_mmio_t eth_mac_mmios[] = {
    {
        .base = A311D_ETH_MAC_BASE,
        .length = A311D_ETH_MAC_LENGTH,
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
    .did = PDEV_DID_REALTEK_ETH_PHY,
};

static const pbus_metadata_t eth_mac_device_metadata[] = {
    {
        .type = DEVICE_METADATA_ETH_PHY_DEVICE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&eth_phy_device),
        .data_size = sizeof(eth_dev_metadata_t),
    },
};

static const eth_dev_metadata_t eth_mac_device = {
    .vid = PDEV_VID_DESIGNWARE,
    .pid = 0,
    .did = PDEV_DID_DESIGNWARE_ETH_MAC,
};

static const pbus_metadata_t eth_board_metadata[] = {
    {
        .type = DEVICE_METADATA_ETH_MAC_DEVICE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&eth_mac_device),
        .data_size = sizeof(eth_dev_metadata_t),
    },
};

static pbus_dev_t eth_board_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "ethernet_mac";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A311D;
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
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x18),
};

static const zx_bind_inst_t gpio_int_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, VIM3_ETH_MAC_INTR),
};
static const device_fragment_part_t i2c_fragment[] = {
    {countof(root_match), root_match},
    {countof(i2c_match), i2c_match},
};

static const device_fragment_part_t gpio_int_fragment[] = {
    {countof(root_match), root_match},
    {countof(gpio_int_match), gpio_int_match},
};
static const device_fragment_t eth_fragments[] = {
    {"i2c", countof(i2c_fragment), i2c_fragment},
    {"gpio-int", countof(gpio_int_fragment), gpio_int_fragment},
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

zx_status_t Vim3::EthInit() {
  // setup pinmux for RGMII connections
  gpio_impl_.SetAltFunction(A311D_GPIOZ(0), A311D_GPIOZ_0_ETH_MDIO_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(1), A311D_GPIOZ_1_ETH_MDC_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(2), A311D_GPIOZ_2_ETH_RX_CLK_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(3), A311D_GPIOZ_3_ETH_RX_DV_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(4), A311D_GPIOZ_4_ETH_RXD0_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(5), A311D_GPIOZ_5_ETH_RXD1_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(6), A311D_GPIOZ_6_ETH_RXD2_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(7), A311D_GPIOZ_7_ETH_RXD3_FN);

  gpio_impl_.SetAltFunction(A311D_GPIOZ(8), A311D_GPIOZ_8_ETH_TX_CLK_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(9), A311D_GPIOZ_9_ETH_TX_EN_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(10), A311D_GPIOZ_10_ETH_TXD0_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(11), A311D_GPIOZ_11_ETH_TXD1_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(12), A311D_GPIOZ_12_ETH_TXD2_FN);
  gpio_impl_.SetAltFunction(A311D_GPIOZ(13), A311D_GPIOZ_13_ETH_TXD3_FN);

  gpio_impl_.SetDriveStrength(A311D_GPIOZ(0), 2500, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(1), 2500, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(2), 3000, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(3), 3000, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(4), 3000, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(5), 3000, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(6), 3000, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(7), 3000, nullptr);

  gpio_impl_.SetDriveStrength(A311D_GPIOZ(8), 3000, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(9), 3000, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(10), 3000, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(11), 3000, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(12), 3000, nullptr);
  gpio_impl_.SetDriveStrength(A311D_GPIOZ(13), 3000, nullptr);

  // Add a composite device for ethernet board in a new devhost.
  auto status = pbus_.CompositeDeviceAdd(&eth_board_dev, reinterpret_cast<uint64_t>(eth_fragments),
                                         std::size(eth_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }

  // Add a composite device for dwmac driver in the ethernet board driver's devhost.
  status = pbus_.CompositeDeviceAdd(&dwmac_dev, reinterpret_cast<uint64_t>(dwmac_fragments),
                                    std::size(dwmac_fragments), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim3
