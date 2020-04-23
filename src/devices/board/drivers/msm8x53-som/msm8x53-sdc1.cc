// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <soc/msm8x53/msm8x53-hw.h>
#include <soc/msm8x53/msm8x53-sdhci.h>

#include "msm8x53.h"

namespace board_msm8x53 {

zx_status_t Msm8x53::Sdc1Init() {
  static const pbus_bti_t sdc1_btis[] = {
      {.iommu_index = 0, .bti_id = BTI_SDC1},
  };

  const pbus_irq_t sdc1_irqs[] = {
      {.irq = msm8x53::kIrqSdc1, .mode = ZX_INTERRUPT_MODE_EDGE_HIGH},
  };

  const pbus_mmio_t sdc1_mmios[] = {
      {.base = msm8x53::kSdc1CoreBase, .length = msm8x53::kSdc1CoreSize},
      {.base = msm8x53::kSdc1HcBase, .length = msm8x53::kSdc1HcSize},
  };

  pbus_dev_t sdc1_dev = {};
  sdc1_dev.name = "emmc";
  sdc1_dev.vid = PDEV_VID_QUALCOMM;
  sdc1_dev.did = PDEV_DID_QUALCOMM_SDC1;
  sdc1_dev.bti_list = sdc1_btis;
  sdc1_dev.bti_count = countof(sdc1_btis);
  sdc1_dev.irq_list = sdc1_irqs;
  sdc1_dev.irq_count = countof(sdc1_irqs);
  sdc1_dev.mmio_list = sdc1_mmios;
  sdc1_dev.mmio_count = countof(sdc1_mmios);

  zx_status_t status = pbus_.DeviceAdd(&sdc1_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
  }

  return status;
}

}  // namespace board_msm8x53
