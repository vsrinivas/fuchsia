// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-registers.h>
#include <soc/aml-s912/s912-hw.h>

#include "src/devices/board/drivers/vim2/mali_bind.h"
#include "vim.h"

namespace vim {
static const pbus_mmio_t mali_mmios[] = {
    {
        .base = S912_MALI_BASE,
        .length = S912_MALI_LENGTH,
    },
    {
        .base = S912_HIU_BASE,
        .length = S912_HIU_LENGTH,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = S912_MALI_IRQ_PP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S912_MALI_IRQ_GPMMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S912_MALI_IRQ_GP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static pbus_bti_t mali_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
};

zx_status_t Vim::MaliInit() {
  pbus_dev_t mali_dev = {};
  mali_dev.name = "mali";
  mali_dev.vid = PDEV_VID_AMLOGIC;
  mali_dev.pid = PDEV_PID_AMLOGIC_S912;
  mali_dev.did = PDEV_DID_AMLOGIC_MALI_INIT;
  mali_dev.mmio_list = mali_mmios;
  mali_dev.mmio_count = countof(mali_mmios);
  mali_dev.irq_list = mali_irqs;
  mali_dev.irq_count = countof(mali_irqs);
  mali_dev.bti_list = mali_btis;
  mali_dev.bti_count = countof(mali_btis);

  // Populate the BTI information
  mali_btis[0].iommu_index = 0;
  mali_btis[0].bti_id = BTI_MALI;

  zx_status_t status = pbus_.AddComposite(&mali_dev, reinterpret_cast<uint64_t>(mali_fragments),
                                          countof(mali_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "CompositeDeviceAdd failed: %d", status);
    return status;
  }
  return status;
}
}  // namespace vim
