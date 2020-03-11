// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/smc.h>

#include <cstdint>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "astro.h"

namespace astro {

// The Astro Secure OS memory region is defined within the bootloader image.
// The ZBI provided to the kernel must mark this memory space as reserved.
// The OP-TEE driver will query OP-TEE for the exact sub-range of this memory
// space to be used by the driver.
#define ASTRO_SECURE_OS_BASE 0x05300000
#define ASTRO_SECURE_OS_LENGTH 0x02000000

static const pbus_mmio_t astro_tee_mmios[] = {
    {
        .base = ASTRO_SECURE_OS_BASE,
        .length = ASTRO_SECURE_OS_LENGTH,
    },
};

static const pbus_bti_t astro_tee_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_TEE,
    },
};

static const pbus_smc_t astro_tee_smcs[] = {
    {
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_LENGTH,
        .exclusive = false,
    },
};

static const pbus_dev_t tee_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "tee";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_OPTEE;
  dev.mmio_list = astro_tee_mmios;
  dev.mmio_count = countof(astro_tee_mmios);
  dev.bti_list = astro_tee_btis;
  dev.bti_count = countof(astro_tee_btis);
  dev.smc_list = astro_tee_smcs;
  dev.smc_count = countof(astro_tee_smcs);
  return dev;
}();

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
constexpr zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
constexpr device_fragment_part_t sysmem_fragment[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
constexpr device_fragment_t fragments[] = {
    {countof(sysmem_fragment), sysmem_fragment},
};

zx_status_t Astro::TeeInit() {
  zx_status_t status =
      pbus_.CompositeDeviceAdd(&tee_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d\n", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace astro
