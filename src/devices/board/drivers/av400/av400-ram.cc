// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include "av400.h"

namespace av400 {

static constexpr pbus_mmio_t av400_dmc_mmios[] = {
    {
        .base = A5_DMC_BASE,
        .length = A5_DMC_LENGTH,
    },
};

static constexpr pbus_irq_t av400_dmc_irqs[] = {
    {
        .irq = A5_DDR_BW_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static constexpr pbus_smc_t av400_dmc_smcs[] = {
    {
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_LENGTH,
        .exclusive = false,
    },
};

static constexpr pbus_dev_t dmc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-ram-ctl";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A5;
  dev.did = PDEV_DID_AMLOGIC_RAM_CTL;
  dev.mmio_list = av400_dmc_mmios;
  dev.mmio_count = std::size(av400_dmc_mmios);
  dev.irq_list = av400_dmc_irqs;
  dev.irq_count = std::size(av400_dmc_irqs);
  dev.smc_list = av400_dmc_smcs;
  dev.smc_count = std::size(av400_dmc_smcs);
  return dev;
}();

zx_status_t Av400::DmcInit() {
  zx_status_t status = pbus_.DeviceAdd(&dmc_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DeviceAdd failed: %s", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

}  // namespace av400
