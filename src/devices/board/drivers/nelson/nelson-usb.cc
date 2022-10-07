// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/align.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb.h>

#include <ddk/usb-peripheral-config.h>
#include <soc/aml-s905d3/s905d3-hw.h>
#include <usb/cdc.h>
#include <usb/dwc2/metadata.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_dwc2_bind.h"
#include "src/devices/board/drivers/nelson/nelson_xhci_bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> dwc2_mmios{
    {{
        .base = S905D3_USB1_BASE,
        .length = S905D3_USB1_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> dwc2_irqs{
    {{
        .irq = S905D3_USB1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Bti> dwc2_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_USB,
    }},
};

static const char kManufacturer[] = "Zircon";
static const char kProduct[] = "CDC-Ethernet";
static const char kSerial[] = "0123456789ABCDEF";

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

using FunctionDescriptor = fuchsia_hardware_usb_peripheral_FunctionDescriptor;

static const std::vector<fpbus::BootMetadata> usb_boot_metadata{
    {{
        // Use Bluetooth MAC address for USB ethernet as well.
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_BLUETOOTH,
    }},
    {{
        // Advertise serial number over USB
        .zbi_type = DEVICE_METADATA_SERIAL_NUMBER,
        .zbi_extra = 0,
    }},
};

static const std::vector<fpbus::Mmio> xhci_mmios{
    {{
        .base = S905D3_USB0_BASE,
        .length = S905D3_USB0_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> xhci_irqs{
    {{
        .irq = S905D3_USB0_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
};

static const std::vector<fpbus::Bti> usb_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_USB,
    }},
};

static const fpbus::Node xhci_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "xhci";
  dev.vid() = PDEV_VID_GENERIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_USB_XHCI_COMPOSITE;
  dev.mmio() = xhci_mmios;
  dev.irq() = xhci_irqs;
  dev.bti() = usb_btis;
  return dev;
}();

static const std::vector<fpbus::Mmio> usb_phy_mmios{
    {{
        .base = S905D3_RESET1_BASE,
        .length = S905D3_RESET1_LENGTH,
    }},
    {{
        .base = S905D3_USBCTRL_BASE,
        .length = S905D3_USBCTRL_LENGTH,
    }},
    {{
        .base = S905D3_USBPHY20_BASE,
        .length = S905D3_USBPHY20_LENGTH,
    }},
    {{
        .base = S905D3_USBPHY21_BASE,
        .length = S905D3_USBPHY21_LENGTH,
    }},
    {{
        .base = S905D3_POWER_BASE,
        .length = S905D3_POWER_LENGTH,
    }},
    {{
        .base = S905D3_SLEEP_BASE,
        .length = S905D3_SLEEP_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> usb_phy_irqs{
    {{
        .irq = S905D3_USB_IDDIG_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

// values from mesong12b.dtsi usb2_phy_v2 pll-setting-#
static const uint32_t pll_settings[] = {
    0x09400414, 0x927E0000, 0xac5f49e5, 0xfe18, 0xfff, 0x78000, 0xe0004, 0xe000c,
};

static const std::vector<fpbus::Metadata> usb_phy_metadata{
    {{
        .type = DEVICE_METADATA_PRIVATE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&pll_settings),
            reinterpret_cast<const uint8_t*>(&pll_settings) + sizeof(pll_settings)),
    }},
};

static const fpbus::Node usb_phy_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-usb-phy-v2";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_NELSON;
  dev.did() = PDEV_DID_NELSON_USB_PHY;
  dev.mmio() = usb_phy_mmios;
  dev.irq() = usb_phy_irqs;
  dev.bti() = usb_btis;
  dev.metadata() = usb_phy_metadata;
  return dev;
}();

zx_status_t Nelson::UsbInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('USB_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, usb_phy_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Usb(usb_phy_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Usb(usb_phy_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  // Add XHCI and DWC2 to the same devhost as the aml-usb-phy.
  {
    auto result = pbus_.buffer(arena)->AddComposite(
        fidl::ToWire(fidl_arena, xhci_dev),
        platform_bus_composite::MakeFidlFragment(fidl_arena, xhci_fragments,
                                                 std::size(xhci_fragments)),
        "xhci-phy");
    if (!result.ok()) {
      zxlogf(ERROR, "%s: AddComposite Usb(xhci_dev) request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: AddComposite Usb(xhci_dev) failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
  }

  constexpr size_t alignment = alignof(UsbConfig) > __STDCPP_DEFAULT_NEW_ALIGNMENT__
                                   ? alignof(UsbConfig)
                                   : __STDCPP_DEFAULT_NEW_ALIGNMENT__;
  constexpr size_t config_size = sizeof(UsbConfig) + 1 * sizeof(FunctionDescriptor);
  UsbConfig* config =
      reinterpret_cast<UsbConfig*>(aligned_alloc(alignment, ZX_ROUNDUP(config_size, alignment)));
  if (!config) {
    return ZX_ERR_NO_MEMORY;
  }

  config->vid = GOOGLE_USB_VID;
  config->pid = GOOGLE_USB_CDC_AND_FUNCTION_TEST_PID;
  strcpy(config->manufacturer, kManufacturer);
  strcpy(config->serial, kSerial);
  strcpy(config->product, kProduct);
  config->functions[0].interface_class = USB_CLASS_COMM;
  config->functions[0].interface_subclass = USB_CDC_SUBCLASS_ETHERNET;
  config->functions[0].interface_protocol = 0;
  const std::vector<fpbus::Metadata> usb_metadata{
      {{
          .type = DEVICE_METADATA_USB_CONFIG,
          .data = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(config),
                                       reinterpret_cast<uint8_t*>(config) + config_size),
      }},
      {{
          .type = DEVICE_METADATA_PRIVATE,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&dwc2_metadata),
              reinterpret_cast<const uint8_t*>(&dwc2_metadata) + sizeof(dwc2_metadata)),
      }},
  };

  const fpbus::Node dwc2_dev = [&]() {
    fpbus::Node dev = {};
    dev.name() = "dwc2";
    dev.vid() = PDEV_VID_GENERIC;
    dev.pid() = PDEV_PID_GENERIC;
    dev.did() = PDEV_DID_USB_DWC2;
    dev.mmio() = dwc2_mmios;
    dev.irq() = dwc2_irqs;
    dev.bti() = dwc2_btis;
    dev.metadata() = std::move(usb_metadata);
    dev.boot_metadata() = usb_boot_metadata;
    return dev;
  }();

  {
    auto result = pbus_.buffer(arena)->AddComposite(
        fidl::ToWire(fidl_arena, dwc2_dev),
        platform_bus_composite::MakeFidlFragment(fidl_arena, dwc2_fragments,
                                                 std::size(dwc2_fragments)),
        "dwc2-phy");
    if (!result.ok()) {
      zxlogf(ERROR, "%s: AddComposite Usb(dwc2_dev) request failed: %s", __func__,
             result.FormatDescription().data());
      return result.status();
    }
    if (result->is_error()) {
      zxlogf(ERROR, "%s: AddComposite Usb(dwc2_dev) failed: %s", __func__,
             zx_status_get_string(result->error_value()));
      return result->error_value();
    }
  }
  free(config);

  return ZX_OK;
}

}  // namespace nelson
