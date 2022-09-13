// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>

#include <soc/aml-a5/a5-hw.h>

#include "av400.h"

namespace av400 {

static constexpr pbus_mmio_t mailbox_mmios[] = {
    {
        .base = A5_MAILBOX_WR_BASE,
        .length = A5_MAILBOX_WR_LENGTH,
    },
    {
        .base = A5_MAILBOX_RD_BASE,
        .length = A5_MAILBOX_RD_LENGTH,
    },
    {
        .base = A5_MAILBOX_SET_BASE,
        .length = A5_MAILBOX_SET_LENGTH,
    },
    {
        .base = A5_MAILBOX_CLR_BASE,
        .length = A5_MAILBOX_CLR_LENGTH,
    },
    {
        .base = A5_MAILBOX_STS_BASE,
        .length = A5_MAILBOX_STS_LENGTH,
    },
    {
        .base = A5_MAILBOX_IRQCTRL_BASE,
        .length = A5_MAILBOX_IRQCTRL_LENGTH,
    },
};

static constexpr pbus_irq_t mailbox_irqs[] = {
    {
        .irq = A5_MAILBOX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t mailbox_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "mailbox";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A5;
  dev.did = PDEV_DID_AMLOGIC_MAILBOX;
  dev.mmio_list = mailbox_mmios;
  dev.mmio_count = std::size(mailbox_mmios);
  dev.irq_list = mailbox_irqs;
  dev.irq_count = std::size(mailbox_irqs);
  return dev;
}();

zx_status_t Av400::MailboxInit() {
  zx_status_t status = pbus_.DeviceAdd(&mailbox_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DeviceAdd failed %s", zx_status_get_string(status));
  }
  return status;
}

}  // namespace av400
