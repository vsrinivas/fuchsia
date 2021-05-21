// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include <cstdint>

#include "nelson.h"

namespace nelson {

// The Nelson Secure OS memory region is defined within the bootloader image.
// The ZBI provided to the kernel must mark this memory space as reserved.
// The OP-TEE driver will query OP-TEE for the exact sub-range of this memory
// space to be used by the driver.
#define NELSON_SECURE_OS_BASE 0x05300000
#define NELSON_SECURE_OS_LENGTH 0x02000000

static const pbus_mmio_t nelson_tee_mmios[] = {
    {
        .base = NELSON_SECURE_OS_BASE,
        .length = NELSON_SECURE_OS_LENGTH,
    },
};

static const pbus_bti_t nelson_tee_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_TEE,
    },
};

static const pbus_smc_t nelson_tee_smcs[] = {
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
  dev.mmio_list = nelson_tee_mmios;
  dev.mmio_count = countof(nelson_tee_mmios);
  dev.bti_list = nelson_tee_btis;
  dev.bti_count = countof(nelson_tee_btis);
  dev.smc_list = nelson_tee_smcs;
  dev.smc_count = countof(nelson_tee_smcs);
  return dev;
}();

constexpr zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};
constexpr zx_bind_inst_t rpmb_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_RPMB),
};
constexpr device_fragment_part_t sysmem_fragment[] = {
    {countof(sysmem_match), sysmem_match},
};
constexpr device_fragment_part_t rpmb_fragment[] = {
    {countof(rpmb_match), rpmb_match},
};
constexpr device_fragment_t fragments[] = {
    {"sysmem", countof(sysmem_fragment), sysmem_fragment},
    {"rpmb", countof(rpmb_fragment), rpmb_fragment},
};

zx_status_t Nelson::TeeInit() {
  zx_status_t status = pbus_.CompositeDeviceAdd(&tee_dev, reinterpret_cast<uint64_t>(fragments),
                                                countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace nelson
