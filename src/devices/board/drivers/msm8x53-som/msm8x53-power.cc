// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/msm8x53/msm8x53-gpio.h>
#include <soc/msm8x53/msm8x53-hw.h>
#include <soc/msm8x53/msm8x53-power.h>

#include "msm8x53.h"

namespace board_msm8x53 {

zx_status_t Msm8x53::PowerInit() {
  const pbus_mmio_t pmic_arb_mmios[] = {
      {
          .base = kPmicArbCoreMmio,
          .length = kPmicArbCoreMmioSize,
      },
      {
          .base = kPmicArbChnlsMmio,
          .length = kPmicArbChanlsMmioSize,
      },
      {
          .base = kPmicArbObsvrMmio,
          .length = kPmicArbObsvrMmioSize,
      },
      {
          .base = kPmicArbIntrMmio,
          .length = kPmicArbIntrMmioSize,
      },
      {
          .base = kPmicArbCnfgMmio,
          .length = kPmicArbCnfgMmioSize,
      },
  };

  const pbus_irq_t pmic_arb_irqs[] = {
      {
          .irq = 190,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  pbus_dev_t power_dev = {};
  power_dev.name = "power";
  power_dev.vid = PDEV_VID_QUALCOMM;
  power_dev.pid = PDEV_PID_QUALCOMM_MSM8X53;
  power_dev.did = PDEV_DID_QUALCOMM_POWER;
  power_dev.mmio_list = pmic_arb_mmios;
  power_dev.mmio_count = countof(pmic_arb_mmios);
  power_dev.irq_list = pmic_arb_irqs;
  power_dev.irq_count = countof(pmic_arb_irqs);

  zx_status_t status = pbus_.DeviceAdd(&power_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_msm8x53
