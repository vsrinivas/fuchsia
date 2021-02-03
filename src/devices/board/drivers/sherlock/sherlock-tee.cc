// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <zircon/syscalls/smc.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>

#include "sherlock.h"

namespace sherlock {
// The Sherlock Secure OS memory region is defined within the bootloader image. The ZBI provided to
// the kernel must mark this memory space as reserved. The OP-TEE driver will query OP-TEE for the
// exact sub-range of this memory space to be used by the driver.
#define SHERLOCK_SECURE_OS_BASE 0x05300000
#define SHERLOCK_SECURE_OS_LENGTH 0x02000000

static const pbus_mmio_t sherlock_tee_mmios[] = {
    {
        .base = SHERLOCK_SECURE_OS_BASE,
        .length = SHERLOCK_SECURE_OS_LENGTH,
    },
};

static const pbus_bti_t sherlock_tee_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_TEE,
    },
};

static const pbus_smc_t sherlock_tee_smcs[] = {
    {
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_LENGTH,
        .exclusive = false,
    },
};

static pbus_dev_t tee_dev = []() {
  pbus_dev_t tee_dev = {};
  tee_dev.name = "tee";
  tee_dev.vid = PDEV_VID_GENERIC;
  tee_dev.pid = PDEV_PID_GENERIC;
  tee_dev.did = PDEV_DID_OPTEE;
  tee_dev.mmio_list = sherlock_tee_mmios;
  tee_dev.mmio_count = std::size(sherlock_tee_mmios);
  tee_dev.bti_list = sherlock_tee_btis;
  tee_dev.bti_count = std::size(sherlock_tee_btis);
  tee_dev.smc_list = sherlock_tee_smcs;
  tee_dev.smc_count = std::size(sherlock_tee_smcs);
  return tee_dev;
}();

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
constexpr zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
constexpr zx_bind_inst_t rpmb_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_RPMB),
};
constexpr device_fragment_part_t sysmem_fragment[] = {
    {countof(root_match), root_match},
    {countof(sysmem_match), sysmem_match},
};
constexpr device_fragment_part_t rpmb_fragment[] = {
    {countof(root_match), root_match},
    {countof(rpmb_match), rpmb_match},
};
constexpr device_fragment_t fragments[] = {
    {"sysmem", countof(sysmem_fragment), sysmem_fragment},
    {"rpmb", countof(rpmb_fragment), rpmb_fragment},
};

zx_status_t Sherlock::TeeInit() {
  zx_status_t status = pbus_.CompositeDeviceAdd(&tee_dev, reinterpret_cast<uint64_t>(fragments),
                                                countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace sherlock
