// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/mmio-buffer.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zircon-internal/align.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>

#include <ddk/usb-peripheral-config.h>
#include <soc/aml-s905d3/s905d3-hw.h>
#include <usb/dwc2/metadata.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/dwc2_bind.h"
#include "src/devices/board/drivers/nelson/xhci_bind.h"

namespace nelson {

static const pbus_mmio_t dwc2_mmios[] = {
    {
        .base = S905D3_USB1_BASE,
        .length = S905D3_USB1_LENGTH,
    },
};

static const pbus_irq_t dwc2_irqs[] = {
    {
        .irq = S905D3_USB1_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t dwc2_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB,
    },
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

static const pbus_boot_metadata_t usb_boot_metadata[] = {
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

static const pbus_dev_t dwc2_dev = []() {
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

static const pbus_mmio_t xhci_mmios[] = {
    {
        .base = S905D3_USB0_BASE,
        .length = S905D3_USB0_LENGTH,
    },
};

static const pbus_irq_t xhci_irqs[] = {
    {
        .irq = S905D3_USB0_IRQ,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static const pbus_bti_t usb_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB,
    },
};

static const pbus_dev_t xhci_dev = []() {
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

static const pbus_mmio_t usb_phy_mmios[] = {
    {
        .base = S905D3_RESET1_BASE,
        .length = S905D3_RESET1_LENGTH,
    },
    {
        .base = S905D3_USBCTRL_BASE,
        .length = S905D3_USBCTRL_LENGTH,
    },
    {
        .base = S905D3_USBPHY20_BASE,
        .length = S905D3_USBPHY20_LENGTH,
    },
    {
        .base = S905D3_USBPHY21_BASE,
        .length = S905D3_USBPHY21_LENGTH,
    },
    {
        .base = S905D3_POWER_BASE,
        .length = S905D3_POWER_LENGTH,
    },
    {
        .base = S905D3_SLEEP_BASE,
        .length = S905D3_SLEEP_LENGTH,
    },
};

static const pbus_irq_t usb_phy_irqs[] = {
    {
        .irq = S905D3_USB_IDDIG_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

// values from mesong12b.dtsi usb2_phy_v2 pll-setting-#
static const uint32_t pll_settings[] = {
    0x09400414, 0x927E0000, 0xac5f49e5, 0xfe18, 0xfff, 0x78000, 0xe0004, 0xe000c,
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
  dev.pid = PDEV_PID_NELSON;
  dev.did = PDEV_DID_NELSON_USB_PHY;
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

zx_status_t Nelson::UsbInit() {
  zx_status_t status = pbus_.DeviceAdd(&usb_phy_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd(usb_phy) failed %d", __func__, status);
    return status;
  }

  // Add XHCI and DWC2 to the same devhost as the aml-usb-phy.
  status = pbus_.AddComposite(&xhci_dev, reinterpret_cast<uint64_t>(xhci_fragments),
                              countof(xhci_fragments), "xhci-phy");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd(xhci) failed %d", __func__, status);
    return status;
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
  usb_metadata[0].data_size = config_size;
  usb_metadata[0].data_buffer = reinterpret_cast<uint8_t*>(config);

  status = pbus_.AddComposite(&dwc2_dev, reinterpret_cast<uint64_t>(dwc2_fragments),
                              countof(dwc2_fragments), "dwc2-phy");
  free(config);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd(dwc2) failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace nelson
