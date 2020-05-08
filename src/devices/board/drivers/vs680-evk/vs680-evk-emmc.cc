// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpt.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <soc/vs680/vs680-gpio.h>
#include <soc/vs680/vs680-hw.h>

#include "vs680-evk.h"

namespace board_vs680_evk {

constexpr guid_map_t guid_map[] = {
    {"rootfs_a", GUID_ZIRCON_A_VALUE},
    {"rootfs_b", GUID_ZIRCON_B_VALUE},
    {"userdata", GUID_ZIRCON_R_VALUE},
    {"cache", GUID_FVM_VALUE},
};

zx_status_t Vs680Evk::EmmcInit() {
  zx_status_t status;

  constexpr pbus_mmio_t emmc_mmios[] = {
      {
          .base = vs680::kEmmc0Base,
          .length = vs680::kEmmc0Size,
      },
  };

  constexpr pbus_irq_t emmc_irqs[] = {
      {
          .irq = vs680::kEmmc0Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  constexpr pbus_bti_t emmc_btis[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_EMMC,
      },
  };

  constexpr pbus_metadata_t emmc_metadata[] = {
      {
          .type = DEVICE_METADATA_GUID_MAP,
          .data_buffer = guid_map,
          .data_size = sizeof(guid_map),
      },
  };

  pbus_dev_t emmc_dev = {};
  emmc_dev.name = "vs680-emmc";
  emmc_dev.vid = PDEV_VID_SYNAPTICS;
  emmc_dev.pid = PDEV_PID_SYNAPTICS_VS680;
  emmc_dev.did = PDEV_DID_VS680_SDHCI0;
  emmc_dev.irq_list = emmc_irqs;
  emmc_dev.irq_count = countof(emmc_irqs);
  emmc_dev.mmio_list = emmc_mmios;
  emmc_dev.mmio_count = countof(emmc_mmios);
  emmc_dev.bti_list = emmc_btis;
  emmc_dev.bti_count = countof(emmc_btis);
  emmc_dev.metadata_list = emmc_metadata;
  emmc_dev.metadata_count = countof(emmc_metadata);

  status = pbus_.DeviceAdd(&emmc_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd() error: %d", __func__, status);
  }

  return status;
}

}  // namespace board_vs680_evk
