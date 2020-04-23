// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>
#include <lib/zx/resource.h>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-usb-phy.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

namespace vim {
#define BIT_MASK(start, count) (((1 << (count)) - 1) << (start))
#define SET_BITS(dest, start, count, value) \
  ((dest & ~BIT_MASK(start, count)) | (((value) << (start)) & BIT_MASK(start, count)))

static const pbus_mmio_t xhci_mmios[] = {
    {
        .base = S912_USB0_BASE,
        .length = S912_USB0_LENGTH,
    },
};

static const pbus_irq_t xhci_irqs[] = {
    {
        .irq = S912_USBH_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t xhci_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_USB_XHCI,
    },
};

zx_status_t Vim::UsbInit() {
  zx_status_t status;
  pbus_dev_t xhci_dev = {};

  xhci_dev.name = "xhci";
  xhci_dev.vid = PDEV_VID_GENERIC;
  xhci_dev.pid = PDEV_PID_GENERIC;
  xhci_dev.did = PDEV_DID_USB_XHCI;
  xhci_dev.mmio_list = xhci_mmios;
  xhci_dev.mmio_count = countof(xhci_mmios);
  xhci_dev.irq_list = xhci_irqs;
  xhci_dev.irq_count = countof(xhci_irqs);
  xhci_dev.bti_list = xhci_btis;
  xhci_dev.bti_count = countof(xhci_btis);

  zx::bti bti;

  std::optional<ddk::MmioBuffer> usb_phy;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource resource(get_root_resource());
  status = ddk::MmioBuffer::Create(S912_USB_PHY_BASE, S912_USB_PHY_LENGTH, *resource,
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE, &usb_phy);
  if (status != ZX_OK) {
    zxlogf(ERROR, "UsbInit io_buffer_init_physical failed %d", status);
    return status;
  }

  volatile uint8_t* regs = static_cast<uint8_t*>(usb_phy->get());

  // amlogic_new_usb2_init
  for (int i = 0; i < 4; i++) {
    volatile uint8_t* addr = regs + (i * PHY_REGISTER_SIZE) + U2P_R0_OFFSET;

    uint32_t temp = readl(addr);
    temp |= U2P_R0_POR;
    temp |= U2P_R0_DMPULLDOWN;
    temp |= U2P_R0_DPPULLDOWN;
    if (i == 1) {
      temp |= U2P_R0_IDPULLUP;
    }
    writel(temp, addr);
    zx_nanosleep(zx_deadline_after(ZX_USEC(500)));
    temp = readl(addr);
    temp &= ~U2P_R0_POR;
    writel(temp, addr);
  }

  // amlogic_new_usb3_init
  volatile uint8_t* addr = regs + (4 * PHY_REGISTER_SIZE);

  uint32_t temp = readl(addr + USB_R1_OFFSET);
  temp = SET_BITS(temp, USB_R1_U3H_FLADJ_30MHZ_REG_START, USB_R1_U3H_FLADJ_30MHZ_REG_BITS, 0x20);
  writel(temp, addr + USB_R1_OFFSET);

  temp = readl(addr + USB_R5_OFFSET);
  temp |= USB_R5_IDDIG_EN0;
  temp |= USB_R5_IDDIG_EN1;
  temp = SET_BITS(temp, USB_R5_IDDIG_TH_START, USB_R5_IDDIG_TH_BITS, 255);
  writel(temp, addr + USB_R5_OFFSET);

  if ((status = pbus_.DeviceAdd(&xhci_dev)) != ZX_OK) {
    zxlogf(ERROR, "UsbInit could not add xhci_dev: %d", status);
    return status;
  }

  return ZX_OK;
}
}  // namespace vim
