// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/av400-tee-bind.h"

namespace av400 {
// The Av400 Secure OS memory region is defined within the bootloader image. The ZBI provided to
// the kernel must mark this memory space as reserved. The OP-TEE driver will query OP-TEE for the
// exact sub-range of this memory space to be used by the driver.
#define AV400_SECURE_OS_BASE 0x05000000
#define AV400_SECURE_OS_LENGTH 0x03400000

static constexpr pbus_mmio_t tee_mmios[] = {
    {
        .base = AV400_SECURE_OS_BASE,
        .length = AV400_SECURE_OS_LENGTH,
    },
};

static constexpr pbus_bti_t tee_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_TEE,
    },
};

static constexpr pbus_smc_t tee_smcs[] = {
    {
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_LENGTH,
        .exclusive = false,
    },
};

static constexpr pbus_dev_t tee_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "tee";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_OPTEE;
  dev.mmio_list = tee_mmios;
  dev.mmio_count = std::size(tee_mmios);
  dev.bti_list = tee_btis;
  dev.bti_count = std::size(tee_btis);
  dev.smc_list = tee_smcs;
  dev.smc_count = std::size(tee_smcs);
  return dev;
}();

zx_status_t Av400::TeeInit() {
  zx_status_t status = pbus_.AddComposite(&tee_dev, reinterpret_cast<uint64_t>(tee_fragments),
                                          std::size(tee_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "AddComposite failed: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace av400
