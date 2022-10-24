// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/usb/modeswitch/cpp/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <zircon/status.h>

#include <cstring>

#include <ddk/usb-peripheral-config.h>
#include <soc/aml-common/aml-registers.h>
#include <soc/aml-meson/g12b-clk.h>
#include <usb/cdc.h>
#include <usb/dwc2/metadata.h>
#include <usb/peripheral-config.h>
#include <usb/peripheral.h>
#include <usb/usb.h>

#include "src/devices/board/drivers/vim3/vim3-gpios.h"
#include "src/devices/board/drivers/vim3/vim3.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> usb_phy_mmios{
    {{
        .base = A311D_USBCTRL_BASE,
        .length = A311D_USBCTRL_LENGTH,
    }},
    {{
        .base = A311D_USBPHY20_BASE,
        .length = A311D_USBPHY20_LENGTH,
    }},
    {{
        .base = A311D_USBPHY21_BASE,
        .length = A311D_USBPHY21_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> usb_phy_irqs{
    {{
        .irq = A311D_USB_IDDIG_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Bti> usb_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_USB,
    }},
};

// Static PLL configuration parameters.
static const uint32_t pll_settings[] = {
    0x09400414, 0x927e0000, 0xac5f49e5, 0xfe18, 0xfff, 0x78000, 0xe0004, 0xe000c,
};

static const usb_mode_t dr_mode = USB_MODE_PERIPHERAL;

static const std::vector<fpbus::Metadata> usb_phy_metadata{
    {{
        .type = DEVICE_METADATA_PRIVATE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&pll_settings),
            reinterpret_cast<const uint8_t*>(&pll_settings) + sizeof(pll_settings)),
    }},
    {{
        .type = DEVICE_METADATA_USB_MODE,
        .data = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&dr_mode),
                                     reinterpret_cast<const uint8_t*>(&dr_mode) + sizeof(dr_mode)),
    }},
};

static const fpbus::Node usb_phy_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "vim3-usb-phy";
  dev.pid() = PDEV_PID_VIM3;
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.did() = PDEV_DID_VIM3_USB_PHY;
  dev.mmio() = usb_phy_mmios;
  dev.irq() = usb_phy_irqs;
  dev.bti() = usb_btis;
  dev.metadata() = usb_phy_metadata;
  return dev;
}();

static const std::vector<fpbus::Mmio> dwc2_mmios{
    {{
        .base = A311D_USB1_BASE,
        .length = A311D_USB1_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> dwc2_irqs{
    {{
        .irq = A311D_USB1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Bti> dwc2_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_USB,
    }},
};

// Metadata for DWC2 driver.
static const dwc2_metadata_t dwc2_metadata = {
    .dma_burst_len = DWC2_DMA_BURST_INCR8,
    .usb_turnaround_time = 9,
    .rx_fifo_size = 256,   // for all OUT endpoints.
    .nptx_fifo_size = 32,  // for endpoint zero IN direction.
    .tx_fifo_sizes =
        {
            128,  // for CDC ethernet bulk IN.
            4,    // for CDC ethernet interrupt IN.
            128,  // for test function bulk IN.
            16,   // for test function interrupt IN.
        },
};

static const std::vector<fpbus::Mmio> xhci_mmios{
    {{
        .base = A311D_USB0_BASE,
        .length = A311D_USB0_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> xhci_irqs{
    {{
        .irq = A311D_USB0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const fpbus::Node xhci_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "xhci";
  dev.vid() = PDEV_VID_GENERIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_USB_XHCI;
  dev.mmio() = xhci_mmios;
  dev.irq() = xhci_irqs;
  dev.bti() = usb_btis;
  return dev;
}();

static const zx_bind_inst_t xhci_phy_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_PHY),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_XHCI_COMPOSITE),
};
static const device_fragment_part_t xhci_phy_fragment[] = {
    {std::size(xhci_phy_match), xhci_phy_match},
};
static const device_fragment_t xhci_fragments[] = {
    {"xhci-phy", std::size(xhci_phy_fragment), xhci_phy_fragment},
};

using FunctionDescriptor = fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor;

static const std::vector<fpbus::Metadata> usb_metadata{
    {{
        .type = DEVICE_METADATA_USB_CONFIG,
    }},
    {{
        .type = DEVICE_METADATA_PRIVATE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&dwc2_metadata),
            reinterpret_cast<const uint8_t*>(&dwc2_metadata) + sizeof(dwc2_metadata)),
    }},
};

static const std::vector<fpbus::BootMetadata> usb_boot_metadata{
    {{
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_WIFI,
    }},
};

static fpbus::Node dwc2_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "dwc2";
  dev.vid() = PDEV_VID_GENERIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_USB_DWC2;
  dev.mmio() = dwc2_mmios;
  dev.irq() = dwc2_irqs;
  dev.bti() = dwc2_btis;
  dev.metadata() = usb_metadata;
  dev.boot_metadata() = usb_boot_metadata;
  return dev;
}();

static const zx_bind_inst_t reset_register_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_REGISTERS),
    BI_MATCH_IF(EQ, BIND_REGISTER_ID, aml_registers::REGISTER_USB_PHY_V2_RESET),
};
static const device_fragment_part_t reset_register_fragment[] = {
    {std::size(reset_register_match), reset_register_match},
};
static const device_fragment_t usb_phy_fragments[] = {
    {"register-reset", std::size(reset_register_fragment), reset_register_fragment},
};
static const zx_bind_inst_t dwc2_phy_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_PHY),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC2),
};
static const device_fragment_part_t dwc2_phy_fragment[] = {
    {std::size(dwc2_phy_match), dwc2_phy_match},
};
static const device_fragment_t dwc2_fragments[] = {
    {"dwc2-phy", std::size(dwc2_phy_fragment), dwc2_phy_fragment},
};

zx_status_t Vim3::UsbInit() {
  // Turn on clocks.
  auto status = clk_impl_.Enable(g12b_clk::G12B_CLK_USB);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Unable to enable G12B_CLK_USB");
    return status;
  }
  status = clk_impl_.Enable(g12b_clk::G12B_CLK_USB1_TO_DDR);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Unable to enable G12B_CLK_USB1_TO_DDR");
    return status;
  }

  // Power on USB.
  gpio_impl_.ConfigOut(VIM3_USB_PWR, 1);

  // Create USB Phy Device
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('USB_');
  auto result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, usb_phy_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, usb_phy_fragments,
                                               std::size(usb_phy_fragments)),
      {});
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Usb(usb_phy_dev) request failed: %s",
           __func__, result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Usb(usb_phy_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  // Create DWC2 Device
  UsbConfig* config = nullptr;
  size_t config_size = 0;
  usb::UsbPeripheralConfig peripheral_config(parent_);
  status = peripheral_config.GetUsbConfigFromBootArgs(&config, &config_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get usb config from boot args - %d", status);
    return status;
  }

  dwc2_dev.metadata().value()[0].data().emplace(std::vector<uint8_t>(
      reinterpret_cast<uint8_t*>(config), reinterpret_cast<uint8_t*>(config) + config_size));

  result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, dwc2_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, dwc2_fragments,
                                               std::size(dwc2_fragments)),
      "dwc2-phy");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Usb(dwc2_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Usb(dwc2_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  // Create XHCI device.
  result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, xhci_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, xhci_fragments,
                                               std::size(xhci_fragments)),
      "xhci-phy");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Usb(xhci_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Usb(xhci_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace vim3
