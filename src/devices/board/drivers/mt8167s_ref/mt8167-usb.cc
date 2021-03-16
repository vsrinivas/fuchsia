// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/clockimpl/cpp/banjo.h>
#include <fuchsia/hardware/power/cpp/banjo.h>
#include <fuchsia/hardware/powerimpl/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ddk/hw/reg.h>
#include <lib/zircon-internal/align.h>
#include <string.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>

#include <lib/ddk/metadata.h>
#include <fbl/alloc_checker.h>
#include <soc/mt8167/mt8167-clk.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

namespace {

// USB peripheral device controller
constexpr pbus_mmio_t usb_dci_mmios[] = {
    {
        .base = MT8167_USB0_BASE,
        .length = MT8167_USB0_LENGTH,
    },
    {
        .base = MT8167_USBPHY_BASE,
        .length = MT8167_USBPHY_LENGTH,
    },
};

constexpr pbus_irq_t usb_dci_irqs[] = {
    {
        .irq = MT8167_IRQ_USB_MCU,
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

using FunctionDescriptor = fuchsia_hardware_usb_peripheral_FunctionDescriptor;

// Statically assigned dummy MAC address.
// TODO(fxbug.dev/34469): Provide real MAC address via bootloader or some other mechanism.
static const uint8_t eth_mac_address[] = {
    0x02, 0x98, 0x8f, 0x3c, 0xd2, 0xaa,
};

static pbus_metadata_t usb_metadata[] = {
    {
        .type = DEVICE_METADATA_USB_CONFIG,
        .data_buffer = nullptr,  // Filled in below.
        .data_size = 0,          // Filled in below.
    },
    {
        .type = DEVICE_METADATA_MAC_ADDRESS,
        .data_buffer = reinterpret_cast<const uint8_t*>(eth_mac_address),
        .data_size = sizeof(eth_mac_address),
    },
};

const pbus_dev_t usb_dci_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "mt-usb-dci";
  dev.vid = PDEV_VID_MEDIATEK;
  dev.did = PDEV_DID_MUSB_PERIPHERAL;
  dev.mmio_list = usb_dci_mmios;
  dev.mmio_count = countof(usb_dci_mmios);
  dev.irq_list = usb_dci_irqs;
  dev.irq_count = countof(usb_dci_irqs);
  dev.bti_list = usb_btis;
  dev.bti_count = countof(usb_btis);
  dev.metadata_list = usb_metadata;
  dev.metadata_count = countof(usb_metadata);
  return dev;
}();

// USB host controller
constexpr pbus_mmio_t usb_hci_mmios[] = {
    {
        .base = MT8167_USB1_BASE,
        .length = MT8167_USB0_LENGTH,
    },
    {
        .base = MT8167_USBPHY_BASE,
        .length = MT8167_USBPHY_LENGTH,
    },
};

constexpr pbus_irq_t usb_hci_irqs[] = {
    {
        .irq = MT8167_IRQ_USB_MCU_P1,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

const pbus_dev_t usb_hci_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "mt-usb-hci";
  dev.vid = PDEV_VID_MEDIATEK;
  dev.did = PDEV_DID_MUSB_HOST;
  dev.mmio_list = usb_hci_mmios;
  dev.mmio_count = countof(usb_hci_mmios);
  dev.irq_list = usb_hci_irqs;
  dev.irq_count = countof(usb_hci_irqs);
  dev.bti_list = usb_btis;
  dev.bti_count = countof(usb_btis);
  return dev;
}();

}  // namespace

zx_status_t Mt8167::UsbInit() {
  zx_status_t status;
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
  config->functions[0].interface_protocol = 0;
  config->functions[0].interface_subclass = USB_CDC_SUBCLASS_ETHERNET;
  config->functions[1].interface_class = USB_CLASS_VENDOR;
  config->functions[1].interface_protocol = 0;
  config->functions[1].interface_subclass = 0;
  usb_metadata[0].data_size = config_size;
  usb_metadata[0].data_buffer = reinterpret_cast<const uint8_t*>(config);
  usb_config_ = config;

  // Make sure the USB3v3 LDO voltage regulator is turned on.
  power_domain_status_t pwr_status;
  ddk::PowerImplProtocolClient power(parent());
  if (!power.is_valid()) {
    zxlogf(ERROR, "%s: could not get power protocol", __func__);
    return ZX_ERR_INTERNAL;
  }

  status = power.GetPowerDomainStatus(kVDLdoVUsb33, &pwr_status);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not read usb power domain: %d", __func__, status);
    return status;
  }

  if (pwr_status == POWER_DOMAIN_STATUS_DISABLED) {
    zxlogf(INFO, "%s: enabling usb power domain...", __func__);
    status = power.EnablePowerDomain(kVDLdoVUsb33);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not enable usb power domain: %d", __func__, status);
      return status;
    }

    status = power.GetPowerDomainStatus(kVDLdoVUsb33, &pwr_status);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not read usb power domain: %d", __func__, status);
      return status;
    }

    if (pwr_status != POWER_DOMAIN_STATUS_ENABLED) {
      zxlogf(ERROR, "%s: usb power domain could not be enabled", __func__);
      return ZX_ERR_INTERNAL;
    }
  }

  ddk::ClockImplProtocolClient clk(parent());
  if (!clk.is_valid()) {
    zxlogf(ERROR, "%s: could not get clock protocol", __func__);
    return ZX_ERR_INTERNAL;
  }

  status = clk.Enable(kClkUsb);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not enable USB-P0 clock: %d", __func__, status);
    return status;
  }

  status = clk.Enable(kClkUsb1p);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not enable USB-P1 clock: %d", __func__, status);
    return status;
  }

  status = pbus_.DeviceAdd(&usb_dci_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: (mt-usb-dci) DeviceAdd failed %d", __func__, status);
    return status;
  }

  status = pbus_.DeviceAdd(&usb_hci_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: (mt-usb-hci) DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_mt8167
