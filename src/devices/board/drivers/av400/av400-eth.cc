// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/ethernet/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <limits.h>

#include <fbl/algorithm.h>
#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-a5/a5-hw.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/dwmac-bind.h"
#include "src/devices/board/drivers/av400/eth-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace av400 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Irq> eth_mac_irqs{
    {{
        .irq = A5_ETH_GMAC_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Mmio> eth_board_mmios{
    {{
        .base = A5_CLK_BASE,
        .length = A5_CLK_LENGTH,
    }},
    {{
        .base = A5_CLK_BASE,
        .length = A5_CLK_LENGTH,
    }},
};

static const std::vector<fpbus::Mmio> eth_mac_mmios{
    {{
        .base = A5_ETH_MAC_BASE,
        .length = A5_ETH_MAC_LENGTH,
    }},
};

static const std::vector<fpbus::Bti> eth_mac_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_ETHERNET,
    }},
};

static const std::vector<fpbus::BootMetadata> eth_mac_metadata{
    {{
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = 0,
    }},
};

static constexpr eth_dev_metadata_t eth_phy_device = {
    .vid = PDEV_VID_REALTEK,
    .pid = PDEV_PID_RTL8211F,
    .did = PDEV_DID_REALTEK_ETH_PHY,
};

static const std::vector<fpbus::Metadata> eth_mac_device_metadata{
    {{
        .type = DEVICE_METADATA_ETH_PHY_DEVICE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&eth_phy_device),
            reinterpret_cast<const uint8_t*>(&eth_phy_device) + sizeof(eth_phy_device)),
    }},
};

static constexpr eth_dev_metadata_t eth_mac_device = {
    .vid = PDEV_VID_DESIGNWARE,
    .pid = 0,
    .did = PDEV_DID_DESIGNWARE_ETH_MAC,
};

static const std::vector<fpbus::Metadata> eth_board_metadata{
    {{
        .type = DEVICE_METADATA_ETH_MAC_DEVICE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&eth_mac_device),
            reinterpret_cast<const uint8_t*>(&eth_mac_device) + sizeof(eth_mac_device)),
    }},
};

static const fpbus::Node eth_board_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "ethernet_mac";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A5;
  dev.did() = PDEV_DID_AMLOGIC_ETH;
  dev.mmio() = eth_board_mmios;
  dev.metadata() = eth_board_metadata;
  return dev;
}();

static const fpbus::Node dwmac_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "dwmac";
  dev.vid() = PDEV_VID_DESIGNWARE;
  dev.did() = PDEV_DID_DESIGNWARE_ETH_MAC;
  dev.mmio() = eth_mac_mmios;
  dev.irq() = eth_mac_irqs;
  dev.bti() = eth_mac_btis;
  dev.metadata() = eth_mac_device_metadata;
  dev.boot_metadata() = eth_mac_metadata;
  return dev;
}();

zx_status_t Av400::EthInit() {
  // setup pinmux for RGMII connections
  gpio_impl_.SetAltFunction(A5_GPIOZ(0), A5_GPIOZ_0_ETH_MDIO_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(1), A5_GPIOZ_1_ETH_MDC_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(2), A5_GPIOZ_2_ETH_RX_CLK_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(3), A5_GPIOZ_3_ETH_RX_DV_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(4), A5_GPIOZ_4_ETH_RXD0_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(5), A5_GPIOZ_5_ETH_RXD1_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(6), A5_GPIOZ_6_ETH_RXD2_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(7), A5_GPIOZ_7_ETH_RXD3_FN);

  gpio_impl_.SetAltFunction(A5_GPIOZ(8), A5_GPIOZ_8_ETH_TX_CLK_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(9), A5_GPIOZ_9_ETH_TX_EN_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(10), A5_GPIOZ_10_ETH_TXD0_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(11), A5_GPIOZ_11_ETH_TXD1_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(12), A5_GPIOZ_12_ETH_TXD2_FN);
  gpio_impl_.SetAltFunction(A5_GPIOZ(13), A5_GPIOZ_13_ETH_TXD3_FN);

  gpio_impl_.SetDriveStrength(A5_GPIOZ(0), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(1), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(2), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(3), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(4), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(5), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(6), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(7), 4000, nullptr);

  gpio_impl_.SetDriveStrength(A5_GPIOZ(8), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(9), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(10), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(11), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(12), 4000, nullptr);
  gpio_impl_.SetDriveStrength(A5_GPIOZ(13), 4000, nullptr);

  // Add a composite device for ethernet board in a new devhost.
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('ETH_');
  auto result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, eth_board_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, eth_fragments, std::size(eth_fragments)),
      {});
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Eth(eth_board_dev) request failed: %s",
           __func__, result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Eth(eth_board_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  // Add a composite device for dwmac driver in the ethernet board driver's driver host.
  result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, dwmac_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, dwmac_fragments,
                                               std::size(dwmac_fragments)),
      "eth-board");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Eth(dwmac_dev) request failed: %s",
           __func__, result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Eth(dwmac_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}
}  // namespace av400
