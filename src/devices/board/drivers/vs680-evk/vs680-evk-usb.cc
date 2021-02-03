// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/align.h>
#include <unistd.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/usb-peripheral-config.h>
#include <soc/vs680/vs680-reset.h>
#include <soc/vs680/vs680-usb.h>
#include <usb/dwc2/metadata.h>

#include "src/devices/board/drivers/vs680-evk/vs680-evk-bind.h"
#include "vs680-evk.h"

namespace board_vs680_evk {

constexpr pbus_mmio_t dwc2_mmios[] = {
    {
        .base = vs680::kUsb0Base,
        .length = vs680::kUsb0Size,
    },
};

constexpr pbus_irq_t dwc2_irqs[] = {
    {
        .irq = vs680::kUsb0Irq,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

constexpr pbus_bti_t usb_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB,
    },
};

constexpr char kManufacturer[] = "Zircon";
constexpr char kProduct[] = "CDC-Ethernet";
constexpr char kSerial[] = "0123456789ABCDEF";

// Metadata for DWC2 driver.
constexpr dwc2_metadata_t dwc2_metadata = {
    .dma_burst_len = DWC2_DMA_BURST_INCR8,
    .usb_turnaround_time = 5,

    // Total fifo size is 2648 words, so we can afford to make our FIFO sizes
    // larger than the minimum requirements.
    .rx_fifo_size = 1024,   // for all OUT endpoints.
    .nptx_fifo_size = 256,  // for endpoint zero IN direction.
    .tx_fifo_sizes =
        {
            512,  // for CDC ethernet bulk IN.
            4,    // for CDC ethernet interrupt IN.
            512,  // for test function bulk IN.
            16,   // for test function interrupt IN.
        },
};

// Statically assigned dummy MAC address.
// TODO: Provide real MAC address via bootloader or some other mechanism.
constexpr uint8_t eth_mac_address[] = {
    0x02, 0x98, 0x8f, 0x3c, 0xd2, 0xaa,
};

using FunctionDescriptor = fuchsia_hardware_usb_peripheral_FunctionDescriptor;

static pbus_metadata_t usb_metadata[] = {
    {
        .type = DEVICE_METADATA_USB_CONFIG,
        .data_buffer = nullptr,  // filled in later
        .data_size = 0,
    },
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &dwc2_metadata,
        .data_size = sizeof(dwc2_metadata),
    },
    {
        .type = DEVICE_METADATA_MAC_ADDRESS,
        .data_buffer = eth_mac_address,
        .data_size = sizeof(eth_mac_address),
    },
};

static const pbus_dev_t dwc2_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "dwc2-usb";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_USB_DWC2;
  dev.mmio_list = dwc2_mmios;
  dev.mmio_count = countof(dwc2_mmios);
  dev.irq_list = dwc2_irqs;
  dev.irq_count = countof(dwc2_irqs);
  dev.bti_list = usb_btis;
  dev.bti_count = countof(usb_btis);
  dev.metadata_list = usb_metadata;
  dev.metadata_count = countof(usb_metadata);
  return dev;
}();

constexpr pbus_mmio_t usb_phy_mmios[] = {
    {
        .base = vs680::kUsbPhy0Base,
        .length = vs680::kUsbPhy0Size,
    },
    {
        .base = vs680::kResetBase,
        .length = vs680::kResetSize,
    },
};

static const pbus_dev_t usb_phy_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-usb-phy-v2";
  dev.vid = PDEV_VID_SYNAPTICS;
  dev.pid = PDEV_PID_SYNAPTICS_VS680;
  dev.did = PDEV_DID_VS680_USB_PHY;
  dev.mmio_list = usb_phy_mmios;
  dev.mmio_count = countof(usb_phy_mmios);
  dev.bti_list = usb_btis;
  dev.bti_count = countof(usb_btis);
  return dev;
}();

static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
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

zx_status_t Vs680Evk::UsbInit() {
  auto status = pbus_.DeviceAdd(&usb_phy_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  constexpr size_t alignment = alignof(UsbConfig) > __STDCPP_DEFAULT_NEW_ALIGNMENT__
                                   ? alignof(UsbConfig)
                                   : __STDCPP_DEFAULT_NEW_ALIGNMENT__;
  constexpr size_t config_size = sizeof(UsbConfig) + 2 * sizeof(FunctionDescriptor);
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
  config->functions[1].interface_class = USB_CLASS_VENDOR;
  config->functions[1].interface_subclass = 0;
  config->functions[1].interface_protocol = 0;
  usb_metadata[0].data_size = config_size;
  usb_metadata[0].data_buffer = config;

  status = pbus_.CompositeDeviceAdd(&dwc2_dev, reinterpret_cast<uint64_t>(dwc2_fragments),
                                    countof(dwc2_fragments), 1);
  free(config);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_vs680_evk
