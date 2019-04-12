// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <hw/reg.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/handle.h>
#include <lib/zx/resource.h>
#include <optional>
#include <soc/mt8167/mt8167-hw.h>
#include <string.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>

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

static pbus_metadata_t usb_metadata[] = {
    {.type = DEVICE_METADATA_USB_CONFIG, .data_buffer = nullptr, .data_size = 0},
};

const pbus_dev_t usb_dci_dev = [](){
    pbus_dev_t dev;
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

const pbus_dev_t usb_hci_dev = [](){
    pbus_dev_t dev;
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

} // namespace

#define CLK_GATING_CTRL1_CLR (0x084)
#define CLK_GATING_CTRL2_CLR (0x09c)
#define SET_USB_SW_CG (1U << 13)
#define SET_USB_1P_SW_CG (1U << 14)

zx_status_t Mt8167::UsbInit() {
    constexpr size_t alignment = alignof(UsbConfig) > __STDCPP_DEFAULT_NEW_ALIGNMENT__
                                     ? alignof(UsbConfig)
                                     : __STDCPP_DEFAULT_NEW_ALIGNMENT__;
    UsbConfig* config = reinterpret_cast<UsbConfig*>(
        aligned_alloc(alignment, ROUNDUP(sizeof(UsbConfig) + sizeof(FunctionDescriptor), alignment)));
    if (!config) {
        return ZX_ERR_NO_MEMORY;
    }
    config->vid = GOOGLE_USB_VID;
    config->pid = GOOGLE_USB_CDC_PID;
    strcpy(config->manufacturer, kManufacturer);
    strcpy(config->serial, kSerial);
    strcpy(config->product, kProduct);
    config->functions[0].interface_class = USB_CLASS_COMM;
    config->functions[0].interface_protocol = 0;
    config->functions[0].interface_subclass = USB_CDC_SUBCLASS_ETHERNET;
    usb_metadata[0].data_size = sizeof(UsbConfig) + sizeof(FunctionDescriptor);
    usb_metadata[0].data_buffer = config;
    usb_config_ = config;

    // TODO: move to clock driver when we have one
    zx::unowned_resource root_resource(get_root_resource());
    std::optional<ddk::MmioBuffer> xo_base;
    auto status = ddk::MmioBuffer::Create(MT8167_XO_BASE, MT8167_XO_SIZE, *root_resource,
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE, &xo_base);
    if (status != ZX_OK) {
        return status;
    }

    xo_base->Write32(SET_USB_SW_CG, CLK_GATING_CTRL1_CLR);
    xo_base->Write32(SET_USB_1P_SW_CG, CLK_GATING_CTRL2_CLR);
    xo_base.reset();

    status = pbus_.DeviceAdd(&usb_dci_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: (mt-usb-dci) DeviceAdd failed %d\n", __func__, status);
        return status;
    }

    status = pbus_.DeviceAdd(&usb_hci_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: (mt-usb-hci) DeviceAdd failed %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_mt8167
