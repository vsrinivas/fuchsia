// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>
#include <zircon/status.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/usb/modeswitch.h>
#include <soc/aml-common/aml-registers.h>
#include <soc/aml-meson/g12b-clk.h>

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
        .data_buffer = pll_settings,
        .data_size = sizeof(pll_settings),
    },
    {
        .type = DEVICE_METADATA_USB_MODE,
        .data_buffer = &dr_mode,
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
  status = pbus_.CompositeDeviceAdd(&usb_phy_dev, usb_phy_fragments, countof(usb_phy_fragments),
                                    UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DeviceAdd(usb_phy) failed %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace vim3
