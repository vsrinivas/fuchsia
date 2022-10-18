// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fit/defer.h>
#include <lib/zircon-internal/align.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/usb-peripheral-config.h>
#include <soc/aml-common/aml-registers.h>
#include <usb/cdc.h>
#include <usb/dwc2/metadata.h>
#include <usb/peripheral.h>
#include <usb/usb.h>

#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-aml-usb-phy-v2-bind.h"
#include "src/devices/board/drivers/sherlock/sherlock-dwc2-phy-bind.h"
#include "src/devices/board/drivers/sherlock/sherlock-xhci-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

namespace {

static const std::vector<fpbus::Mmio> dwc2_mmios{
    {{
        .base = T931_USB1_BASE,
        .length = T931_USB1_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> dwc2_irqs{
    {{
        .irq = T931_USB1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Bti> dwc2_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_USB,
    }},
};

constexpr char kManufacturer[] = "Zircon";
constexpr char kSerial[] = "0123456789ABCDEF";
#if (ENABLE_RNDIS)
constexpr char kProductRndis[] = "RNDIS-Ethernet";
#else
constexpr char kProduct[] = "CDC-Ethernet";
#endif

// Metadata for DWC2 driver.
constexpr dwc2_metadata_t dwc2_metadata = {
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

using FunctionDescriptor = fuchsia_hardware_usb_peripheral::wire::FunctionDescriptor;

static std::vector<fpbus::Metadata> usb_metadata{
    {{
        .type = DEVICE_METADATA_USB_CONFIG,
        // No metadata for this item.
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
        .base = T931_USB0_BASE,
        .length = T931_USB0_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> xhci_irqs{
    {{
        .irq = T931_USB0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Mmio> usb_phy_mmios{
    {{
        .base = T931_USBCTRL_BASE,
        .length = T931_USBCTRL_LENGTH,
    }},
    {{
        .base = T931_USBPHY20_BASE,
        .length = T931_USBPHY20_LENGTH,
    }},
    {{
        .base = T931_USBPHY21_BASE,
        .length = T931_USBPHY21_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> usb_phy_irqs{
    {{
        .irq = T931_USB_IDDIG_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
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

// values from mesong12b.dtsi usb2_phy_v2 pll-setting-#
constexpr uint32_t pll_settings[] = {
    0x09400414, 0x927E0000, 0xac5f69e5, 0xfe18, 0x8000fff, 0x78000, 0xe0004, 0xe000c,
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
  dev.did() = PDEV_DID_AML_USB_PHY_V2;
  dev.mmio() = usb_phy_mmios;
  dev.irq() = usb_phy_irqs;
  dev.bti() = usb_btis;
  dev.metadata() = usb_phy_metadata;
  return dev;
}();

}  // namespace

zx_status_t Sherlock::UsbInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('USB_');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, usb_phy_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, aml_usb_phy_v2_fragments,
                                               std::size(aml_usb_phy_v2_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Usb(usb_phy_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Usb(usb_phy_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  // Add XHCI and DWC2 to the same driver_host as the aml-usb-phy.
  result =
      pbus_.buffer(arena)->AddComposite(fidl::ToWire(fidl_arena, xhci_dev),
                                        platform_bus_composite::MakeFidlFragment(
                                            fidl_arena, xhci_fragments, std::size(xhci_fragments)),
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

  constexpr size_t alignment = alignof(UsbConfig) > __STDCPP_DEFAULT_NEW_ALIGNMENT__
                                   ? alignof(UsbConfig)
                                   : __STDCPP_DEFAULT_NEW_ALIGNMENT__;

#if (ENABLE_RNDIS)
  {
    constexpr size_t config_size = sizeof(UsbConfig) + 1 * sizeof(FunctionDescriptor);
    UsbConfig* config =
        reinterpret_cast<UsbConfig*>(aligned_alloc(alignment, ZX_ROUNDUP(config_size, alignment)));
    if (!config) {
      return ZX_ERR_NO_MEMORY;
    }
    auto call = fit::defer([=]() { free(config); });
    config->vid = GOOGLE_USB_VID;
    config->pid = GOOGLE_USB_CDC_AND_FUNCTION_TEST_PID;
    strncpy(config->manufacturer, kManufacturer, sizeof(config->manufacturer));
    strncpy(config->serial, kSerial, sizeof(config->serial));
    strncpy(config->product, kProductRndis, sizeof(config->product));
    config->functions[0].interface_class = USB_CLASS_MISC;
    config->functions[0].interface_subclass = USB_SUBCLASS_MSC_RNDIS;
    config->functions[0].interface_protocol = USB_PROTOCOL_MSC_RNDIS_ETHERNET;
    usb_metadata[0].data() = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(config),
                                                  reinterpret_cast<uint8_t*>(config) + config_size);

    fpbus::Node dwc2_dev;
    dwc2_dev.name() = "dwc2";
    dwc2_dev.vid() = PDEV_VID_GENERIC;
    dwc2_dev.pid() = PDEV_PID_GENERIC;
    dwc2_dev.did() = PDEV_DID_USB_DWC2;
    dwc2_dev.mmio() = dwc2_mmios;
    dwc2_dev.irq() = dwc2_irqs;
    dwc2_dev.bti() = dwc2_btis;
    dwc2_dev.metadata() = usb_metadata;
    dwc2_dev.boot_metadata() = usb_boot_metadata;

    result = pbus_.buffer(arena)->AddComposite(
        fidl::ToWire(fidl_arena, dwc2_dev),
        platform_bus_composite::MakeFidlFragment(fidl_arena, dwc2_phy_fragments,
                                                 std::size(dwc2_phy_fragments)),
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
#else
  {
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
    usb_metadata[0].data() = std::vector<uint8_t>(reinterpret_cast<uint8_t*>(config),
                                                  reinterpret_cast<uint8_t*>(config) + config_size);
    fpbus::Node dwc2_dev = {};
    dwc2_dev.name() = "dwc2";
    dwc2_dev.vid() = PDEV_VID_GENERIC;
    dwc2_dev.pid() = PDEV_PID_GENERIC;
    dwc2_dev.did() = PDEV_DID_USB_DWC2;
    dwc2_dev.mmio() = dwc2_mmios;
    dwc2_dev.irq() = dwc2_irqs;
    dwc2_dev.bti() = dwc2_btis;
    dwc2_dev.metadata() = usb_metadata;
    dwc2_dev.boot_metadata() = usb_boot_metadata;

    result = pbus_.buffer(arena)->AddComposite(
        fidl::ToWire(fidl_arena, dwc2_dev),
        platform_bus_composite::MakeFidlFragment(fidl_arena, dwc2_phy_fragments,
                                                 std::size(dwc2_phy_fragments)),
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
    free(config);
  }
#endif  //(ENABLE_RNDIS)

  return ZX_OK;
}

}  // namespace sherlock
