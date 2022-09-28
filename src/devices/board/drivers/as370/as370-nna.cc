// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/as370/as370-nna.h>

#include "as370.h"
#include "src/devices/board/drivers/as370/as370_nna_bind.h"

namespace board_as370 {

static const pbus_mmio_t nna_mmios[] = {
    {
        .base = as370::kNnaBase,
        .length = as370::kNnaSize,
    },
};

static pbus_bti_t nna_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_NNA,
    },
};

static pbus_irq_t nna_irqs[] = {
    {
        .irq = as370::kNnaIrq,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static pbus_dev_t nna_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "as370-nna";
  dev.vid = PDEV_VID_SYNAPTICS;
  dev.pid = PDEV_PID_SYNAPTICS_AS370;
  dev.did = PDEV_DID_AS370_NNA;
  dev.mmio_list = nna_mmios;
  dev.mmio_count = std::size(nna_mmios);
  dev.bti_list = nna_btis;
  dev.bti_count = std::size(nna_btis);
  dev.irq_list = nna_irqs;
  dev.irq_count = std::size(nna_irqs);
  return dev;
}();

zx_status_t As370::NnaInit() {
  zx_status_t status = pbus_.AddComposite(&nna_dev, reinterpret_cast<uint64_t>(as370_nna_fragments),
                                          std::size(as370_nna_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "AddComposite() failed for nna: %s", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

}  // namespace board_as370
