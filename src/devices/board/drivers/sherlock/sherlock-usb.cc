// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/platform-defs.h>
#include <lib/zircon-internal/align.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/usb-peripheral-config.h>
#include <fbl/auto_call.h>
#include <soc/aml-common/aml-registers.h>
#include <usb/dwc2/metadata.h>

#include "sherlock.h"

namespace sherlock {

namespace {

constexpr pbus_mmio_t dwc2_mmios[] = {
    {
        .base = T931_USB1_BASE,
        .length = T931_USB1_LENGTH,
    },
};

constexpr pbus_irq_t dwc2_irqs[] = {
    {
        .irq = T931_USB1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_bti_t dwc2_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB,
    },
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

using FunctionDescriptor = fuchsia_hardware_usb_peripheral_FunctionDescriptor;

static pbus_metadata_t usb_metadata[] = {
    {
        .type = DEVICE_METADATA_USB_CONFIG,
        .data_buffer = NULL,
        .data_size = 0,
    },
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&dwc2_metadata),
        .data_size = sizeof(dwc2_metadata),
    },
};

constexpr pbus_boot_metadata_t usb_boot_metadata[] = {
    {
        // Use Bluetooth MAC address for USB ethernet as well.
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_BLUETOOTH,
    },
    {
        // Advertise serial number over USB
        .zbi_type = DEVICE_METADATA_SERIAL_NUMBER,
        .zbi_extra = 0,
    },
};

constexpr pbus_dev_t dwc2_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "dwc2";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_USB_DWC2;
  dev.mmio_list = dwc2_mmios;
  dev.mmio_count = countof(dwc2_mmios);
  dev.irq_list = dwc2_irqs;
  dev.irq_count = countof(dwc2_irqs);
  dev.bti_list = dwc2_btis;
  dev.bti_count = countof(dwc2_btis);
  dev.metadata_list = usb_metadata;
  dev.metadata_count = countof(usb_metadata);
  dev.boot_metadata_list = usb_boot_metadata;
  dev.boot_metadata_count = countof(usb_boot_metadata);
  return dev;
}();

constexpr pbus_mmio_t xhci_mmios[] = {
    {
        .base = T931_USB0_BASE,
        .length = T931_USB0_LENGTH,
    },
};

constexpr pbus_irq_t xhci_irqs[] = {
    {
        .irq = T931_USB0_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_mmio_t usb_phy_mmios[] = {
    {
        .base = T931_USBCTRL_BASE,
        .length = T931_USBCTRL_LENGTH,
    },
    {
        .base = T931_USBPHY20_BASE,
        .length = T931_USBPHY20_LENGTH,
    },
    {
        .base = T931_USBPHY21_BASE,
        .length = T931_USBPHY21_LENGTH,
    },
};

constexpr pbus_irq_t usb_phy_irqs[] = {
    {
        .irq = T931_USB_IDDIG_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr pbus_bti_t usb_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB,
    },
};

constexpr pbus_dev_t xhci_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "xhci";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_USB_XHCI_COMPOSITE;
  dev.mmio_list = xhci_mmios;
  dev.mmio_count = countof(xhci_mmios);
  dev.irq_list = xhci_irqs;
  dev.irq_count = countof(xhci_irqs);
  dev.bti_list = usb_btis;
  dev.bti_count = countof(usb_btis);
  return dev;
}();

// values from mesong12b.dtsi usb2_phy_v2 pll-setting-#
constexpr uint32_t pll_settings[] = {
    0x09400414, 0x927E0000, 0xac5f69e5, 0xfe18, 0x8000fff, 0x78000, 0xe0004, 0xe000c,
};

static const pbus_metadata_t usb_phy_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(pll_settings),
        .data_size = sizeof(pll_settings),
    },
};

static const pbus_dev_t usb_phy_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-usb-phy-v2";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.did = PDEV_DID_AML_USB_PHY_V2;
  dev.mmio_list = usb_phy_mmios;
  dev.mmio_count = countof(usb_phy_mmios);
  dev.irq_list = usb_phy_irqs;
  dev.irq_count = countof(usb_phy_irqs);
  dev.bti_list = usb_btis;
  dev.bti_count = countof(usb_btis);
  dev.metadata_list = usb_phy_metadata;
  dev.metadata_count = countof(usb_phy_metadata);
  return dev;
}();

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t reset_register_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_REGISTERS),
    BI_MATCH_IF(EQ, BIND_REGISTER_ID, aml_registers::REGISTER_USB_PHY_V2_RESET),
};
static const device_fragment_part_t reset_register_fragment[] = {
    {countof(root_match), root_match},
    {countof(reset_register_match), reset_register_match},
};
static const device_fragment_t usb_phy_fragments[] = {
    {"register-reset", countof(reset_register_fragment), reset_register_fragment},
};
static const zx_bind_inst_t xhci_phy_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_PHY),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_XHCI_COMPOSITE),
};
static const device_fragment_part_t xhci_phy_fragment[] = {
    {countof(root_match), root_match},
    {countof(xhci_phy_match), xhci_phy_match},
};
static const device_fragment_t xhci_fragments[] = {
    {"xhci-phy", countof(xhci_phy_fragment), xhci_phy_fragment},
};
static const zx_bind_inst_t dwc2_phy_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_PHY),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC2),
};
static const device_fragment_part_t dwc2_phy_fragment[] = {
    {countof(root_match), root_match},
    {countof(dwc2_phy_match), dwc2_phy_match},
};
static const device_fragment_t dwc2_fragments[] = {
    {"dwc2-phy", countof(dwc2_phy_fragment), dwc2_phy_fragment},
};

}  // namespace

zx_status_t Sherlock::UsbInit() {
  auto status =
      pbus_.CompositeDeviceAdd(&usb_phy_dev, reinterpret_cast<uint64_t>(usb_phy_fragments),
                               countof(usb_phy_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  // Add XHCI and DWC2 to the same devhost as the aml-usb-phy.
  status = pbus_.CompositeDeviceAdd(&xhci_dev, reinterpret_cast<uint64_t>(xhci_fragments),
                                    countof(xhci_fragments), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d", __func__, status);
    return status;
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
    fbl::AutoCall call([=]() { free(config); });
    config->vid = GOOGLE_USB_VID;
    config->pid = GOOGLE_USB_CDC_AND_FUNCTION_TEST_PID;
    strncpy(config->manufacturer, kManufacturer, sizeof(config->manufacturer));
    strncpy(config->serial, kSerial, sizeof(config->serial));
    strncpy(config->product, kProductRndis, sizeof(config->product));
    config->functions[0].interface_class = USB_CLASS_MISC;
    config->functions[0].interface_subclass = USB_SUBCLASS_MSC_RNDIS;
    config->functions[0].interface_protocol = USB_PROTOCOL_MSC_RNDIS_ETHERNET;
    usb_metadata[0].data_size = config_size;
    usb_metadata[0].data_buffer = reinterpret_cast<uint8_t*>(config);

    status = pbus_.CompositeDeviceAdd(&dwc2_dev, reinterpret_cast<uint64_t>(dwc2_fragments),
                                      countof(dwc2_fragments), 1);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d", __func__, status);
      return status;
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
    usb_metadata[0].data_size = config_size;
    usb_metadata[0].data_buffer = reinterpret_cast<uint8_t*>(config);

    status = pbus_.CompositeDeviceAdd(&dwc2_dev, reinterpret_cast<uint64_t>(dwc2_fragments),
                                      countof(dwc2_fragments), 1);
    free(config);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d", __func__, status);
      return status;
    }
  }
#endif  //(ENABLE_RNDIS)

  return ZX_OK;
}

}  // namespace sherlock
