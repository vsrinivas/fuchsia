// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/usb/peripheral/c/fidl.h>
#include <stdio.h>
#include <string.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/usb/modeswitch.h>
#include <ddk/usb-peripheral-config.h>
#include <hw/reg.h>
#include <soc/hi3660/hi3660-hw.h>
#include <soc/hi3660/hi3660-regs.h>

#include "hikey960-hw.h"
#include "hikey960.h"

static const char* kManufacturer = "Zircon";
static const char* kProduct = "CDC-Ethernet";
static const char* kSerial = "0123456789ABCDEF";

typedef fuchsia_hardware_usb_peripheral_FunctionDescriptor FunctionDescriptor;

zx_status_t hikey960_usb_phy_init(hikey960_t* hikey) {
  MMIO_PTR volatile void* usb3otg_bc = hikey->usb3otg_bc.vaddr;
  MMIO_PTR volatile void* peri_crg = hikey->peri_crg.vaddr;
  MMIO_PTR volatile void* pctrl = hikey->pctrl.vaddr;
  uint32_t temp;

  MmioWrite32(PERI_CRG_ISODIS_REFCLK_ISO_EN, peri_crg + PERI_CRG_ISODIS);
  MmioWrite32(PCTRL_CTRL3_USB_TCXO_EN | (PCTRL_CTRL3_USB_TCXO_EN << PCTRL_CTRL3_MSK_START),
              pctrl + PCTRL_CTRL3);

  temp = MmioRead32(pctrl + PCTRL_CTRL24);
  temp &= ~PCTRL_CTRL24_SC_CLK_USB3PHY_3MUX1_SEL;
  MmioWrite32(temp, pctrl + PCTRL_CTRL24);

  MmioWrite32(PERI_CRG_GT_CLK_USB3OTG_REF | PERI_CRG_GT_ACLK_USB3OTG, peri_crg + PERI_CRG_CLK_EN4);
  MmioWrite32(
      PERI_CRG_IP_RST_USB3OTG_MUX | PERI_CRG_IP_RST_USB3OTG_AHBIF | PERI_CRG_IP_RST_USB3OTG_32K,
      peri_crg + PERI_CRG_RSTDIS4);

  MmioWrite32(PERI_CRG_IP_RST_USB3OTGPHY_POR | PERI_CRG_IP_RST_USB3OTG, peri_crg + PERI_CRG_RSTEN4);

  // enable PHY REF CLK
  temp = MmioRead32(usb3otg_bc + USB3OTG_CTRL0);
  temp |= USB3OTG_CTRL0_ABB_GT_EN;
  MmioWrite32(temp, usb3otg_bc + USB3OTG_CTRL0);

  temp = MmioRead32(usb3otg_bc + USB3OTG_CTRL7);
  temp |= USB3OTG_CTRL7_REF_SSP_EN;
  MmioWrite32(temp, usb3otg_bc + USB3OTG_CTRL7);

  // exit from IDDQ mode
  temp = MmioRead32(usb3otg_bc + USB3OTG_CTRL2);
  temp &= ~(USB3OTG_CTRL2_POWERDOWN_HSP | USB3OTG_CTRL2_POWERDOWN_SSP);
  MmioWrite32(temp, usb3otg_bc + USB3OTG_CTRL2);
  zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

  MmioWrite32(PERI_CRG_IP_RST_USB3OTGPHY_POR, peri_crg + PERI_CRG_RSTDIS4);
  MmioWrite32(PERI_CRG_IP_RST_USB3OTG, peri_crg + PERI_CRG_RSTDIS4);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));

  temp = MmioRead32(usb3otg_bc + USB3OTG_CTRL3);
  temp |= (USB3OTG_CTRL3_VBUSVLDEXT | USB3OTG_CTRL3_VBUSVLDEXTSEL);
  MmioWrite32(temp, usb3otg_bc + USB3OTG_CTRL3);
  zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

  return ZX_OK;
}

static const pbus_mmio_t dwc3_mmios[] = {
    {
        .base = MMIO_USB3OTG_BASE,
        .length = MMIO_USB3OTG_LENGTH,
    },
};

static const pbus_irq_t dwc3_irqs[] = {
    {
        .irq = IRQ_USB3,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t dwc3_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB_DWC3,
    },
};

static pbus_metadata_t dwc3_metadata[] = {
    {
        .type = DEVICE_METADATA_USB_CONFIG,
        // data_buffer and data_size filled in below
    },
};

static const pbus_dev_t dwc3_dev = {
    .name = "dwc3",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_USB_DWC3,
    .mmio_list = dwc3_mmios,
    .mmio_count = countof(dwc3_mmios),
    .irq_list = dwc3_irqs,
    .irq_count = countof(dwc3_irqs),
    .bti_list = dwc3_btis,
    .bti_count = countof(dwc3_btis),
    .metadata_list = dwc3_metadata,
    .metadata_count = countof(dwc3_metadata),
};

static usb_mode_t hikey_usb_mode = USB_MODE_HOST;

static pbus_metadata_t hikey_usb_metadata[] = {
    {
        .type = DEVICE_METADATA_USB_MODE,
        .data_buffer = &hikey_usb_mode,
        .data_size = sizeof(hikey_usb_mode),
    },
};

const pbus_dev_t hikey_usb_dev = {
    .name = "hikey-usb",
    .vid = PDEV_VID_96BOARDS,
    .pid = PDEV_PID_HIKEY960,
    .did = PDEV_DID_HIKEY960_USB,
    .metadata_list = hikey_usb_metadata,
    .metadata_count = countof(hikey_usb_metadata),
};

// Composite binding rules for the USB drivers.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

static const zx_bind_inst_t gpio1_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_HUB_VDD33_EN),
};
static const device_fragment_part_t gpio1_fragment[] = {
    {countof(root_match), root_match},
    {countof(gpio1_match), gpio1_match},
};
static const zx_bind_inst_t gpio2_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_VBUS_TYPEC),
};
static const device_fragment_part_t gpio2_fragment[] = {
    {countof(root_match), root_match},
    {countof(gpio2_match), gpio2_match},
};
static const zx_bind_inst_t gpio3_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_USBSW_SW_SEL),
};
static const device_fragment_part_t gpio3_fragment[] = {
    {countof(root_match), root_match},
    {countof(gpio3_match), gpio3_match},
};
static const device_fragment_t hikey_usb_fragments[] = {
    {countof(gpio1_fragment), gpio1_fragment},
    {countof(gpio2_fragment), gpio2_fragment},
    {countof(gpio3_fragment), gpio3_fragment},
};

static const zx_bind_inst_t ums_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_MODE_SWITCH),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC3),
};
static const device_fragment_part_t ums_fragment[] = {
    {countof(root_match), root_match},
    {countof(ums_match), ums_match},
};
static const device_fragment_t dwc3_fragments[] = {
    {countof(ums_fragment), ums_fragment},
};

zx_status_t hikey960_usb_init(hikey960_t* hikey) {
  zx_status_t status = hikey960_usb_phy_init(hikey);
  if (status != ZX_OK) {
    return status;
  }

  status = pbus_composite_device_add(&hikey->pbus, &hikey_usb_dev, hikey_usb_fragments,
                                     countof(hikey_usb_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hikey960_add_devices could not add hikey_usb_dev: %d", status);
    return status;
  }

  // construct USB config metadata
  uint8_t buffer[sizeof(struct UsbConfig) + sizeof(FunctionDescriptor)];
  struct UsbConfig* config = (struct UsbConfig*)buffer;
  config->vid = GOOGLE_USB_VID;
  config->pid = GOOGLE_USB_CDC_PID;
  strcpy(config->manufacturer, kManufacturer);
  strcpy(config->serial, kSerial);
  strcpy(config->product, kProduct);
  config->functions[0].interface_class = USB_CLASS_COMM;
  config->functions[0].interface_protocol = 0;
  config->functions[0].interface_subclass = USB_CDC_SUBCLASS_ETHERNET;
  dwc3_metadata[0].data_size = sizeof(struct UsbConfig) + sizeof(FunctionDescriptor);
  dwc3_metadata[0].data_buffer = config;

  status = pbus_composite_device_add(&hikey->pbus, &dwc3_dev, dwc3_fragments,
                                     countof(dwc3_fragments), 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pbus_composite_device_add failed: %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}
