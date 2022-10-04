// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>

#include <soc/aml-a5/a5-hw.h>

#include "av400.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace av400 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> mailbox_mmios{
    {{
        .base = A5_MAILBOX_WR_BASE,
        .length = A5_MAILBOX_WR_LENGTH,
    }},
    {{
        .base = A5_MAILBOX_RD_BASE,
        .length = A5_MAILBOX_RD_LENGTH,
    }},
    {{
        .base = A5_MAILBOX_SET_BASE,
        .length = A5_MAILBOX_SET_LENGTH,
    }},
    {{
        .base = A5_MAILBOX_CLR_BASE,
        .length = A5_MAILBOX_CLR_LENGTH,
    }},
    {{
        .base = A5_MAILBOX_STS_BASE,
        .length = A5_MAILBOX_STS_LENGTH,
    }},
    {{
        .base = A5_MAILBOX_IRQCTRL_BASE,
        .length = A5_MAILBOX_IRQCTRL_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> mailbox_irqs{
    {{
        .irq = A5_MAILBOX_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const fpbus::Node mailbox_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "mailbox";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A5;
  dev.did() = PDEV_DID_AMLOGIC_MAILBOX;
  dev.mmio() = mailbox_mmios;
  dev.irq() = mailbox_irqs;
  return dev;
}();

zx_status_t Av400::MailboxInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('MAIL');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, mailbox_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Mailbox(mailbox_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Mailbox(mailbox_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace av400
