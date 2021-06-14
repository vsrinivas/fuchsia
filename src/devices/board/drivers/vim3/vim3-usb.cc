// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/usb/modeswitch/cpp/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/align.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/status.h>

#include <cstring>

#include <ddk/usb-peripheral-config.h>
#include <soc/aml-common/aml-registers.h>
#include <soc/aml-meson/g12b-clk.h>
#include <usb/dwc2/metadata.h>

#include "vim3.h"

namespace vim3 {

static const pbus_mmio_t usb_phy_mmios[] = {
    {
        .base = A311D_USBCTRL_BASE,
        .length = A311D_USBCTRL_LENGTH,
    },
    {
        .base = A311D_USBPHY20_BASE,
        .length = A311D_USBPHY20_LENGTH,
    },
    {
        .base = A311D_USBPHY21_BASE,
        .length = A311D_USBPHY21_LENGTH,
    },
};

static const pbus_irq_t usb_phy_irqs[] = {
    {
        .irq = A311D_USB_IDDIG_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t usb_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB,
    },
};

// Static PLL configuration parameters.
static const uint32_t pll_settings[] = {
    0x09400414, 0x927e0000, 0xac5f49e5, 0xfe18, 0xfff, 0x78000, 0xe0004, 0xe000c,
};

static const usb_mode_t dr_mode = USB_MODE_PERIPHERAL;

static const pbus_metadata_t usb_phy_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = reinterpret_cast<const uint8_t*>(pll_settings),
        .data_size = sizeof(pll_settings),
    },
    {
        .type = DEVICE_METADATA_USB_MODE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&dr_mode),
        .data_size = sizeof(dr_mode),
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

static const pbus_mmio_t dwc2_mmios[] = {
    {
        .base = A311D_USB1_BASE,
        .length = A311D_USB1_LENGTH,
    },
};

static const pbus_irq_t dwc2_irqs[] = {
    {
        .irq = A311D_USB1_IRQ,
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

static const zx_bind_inst_t reset_register_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_REGISTERS),
    BI_MATCH_IF(EQ, BIND_REGISTER_ID, aml_registers::REGISTER_USB_PHY_V2_RESET),
};
static const device_fragment_part_t reset_register_fragment[] = {
    {countof(reset_register_match), reset_register_match},
};
static const device_fragment_t usb_phy_fragments[] = {
    {"register-reset", countof(reset_register_fragment), reset_register_fragment},
};
static const zx_bind_inst_t dwc2_phy_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_PHY),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC2),
};
static const device_fragment_part_t dwc2_phy_fragment[] = {
    {countof(dwc2_phy_match), dwc2_phy_match},
};
static const device_fragment_t dwc2_fragments[] = {
    {"dwc2-phy", countof(dwc2_phy_fragment), dwc2_phy_fragment},
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

  // Create USB Phy Device
  status = pbus_.CompositeDeviceAdd(&usb_phy_dev, reinterpret_cast<uint64_t>(usb_phy_fragments),
                                    countof(usb_phy_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DeviceAdd(usb_phy) failed %s", zx_status_get_string(status));
    return status;
  }

  // Create DWC2 Device
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
  std::strcpy(config->manufacturer, kManufacturer);
  std::strcpy(config->serial, kSerial);
  std::strcpy(config->product, kProduct);
  config->functions[0].interface_class = USB_CLASS_COMM;
  config->functions[0].interface_subclass = USB_CDC_SUBCLASS_ETHERNET;
  config->functions[0].interface_protocol = 0;
  usb_metadata[0].data_size = config_size;
  usb_metadata[0].data_buffer = reinterpret_cast<uint8_t*>(config);

  status = pbus_.CompositeDeviceAdd(&dwc2_dev, reinterpret_cast<uint64_t>(dwc2_fragments),
                                    countof(dwc2_fragments), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd(dwc2) failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace vim3
