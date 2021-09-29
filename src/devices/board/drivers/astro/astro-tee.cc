// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/stdcompat/span.h>
#include <zircon/syscalls/smc.h>

#include <cstdint>

#include "astro.h"
#include "src/devices/board/drivers/astro/astro-tee-bind.h"
#include "src/devices/lib/fidl-metadata/tee.h"

namespace astro {

// The Astro Secure OS memory region is defined within the bootloader image.
// The ZBI provided to the kernel must mark this memory space as reserved.
// The OP-TEE driver will query OP-TEE for the exact sub-range of this memory
// space to be used by the driver.
#define ASTRO_SECURE_OS_BASE 0x05300000
#define ASTRO_SECURE_OS_LENGTH 0x02000000

#define ASTRO_OPTEE_DEFAULT_THREAD_COUNT 2

using tee_thread_config_t = fidl_metadata::tee::CustomThreadConfig;

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

static tee_thread_config_t tee_thread_cfg[] = {
    {.role = "fuchsia.tee.media",
     .count = 1,
     .trusted_apps = {
         {0x9a04f079,
          0x9840,
          0x4286,
          {0xab, 0x92, 0xe6, 0x5b, 0xe0, 0x88, 0x5f, 0x95}},  // playready
         {0xe043cde0, 0x61d0, 0x11e5, {0x9c, 0x26, 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}  // widevine
     }}};

zx_status_t Astro::TeeInit() {
  std::vector<pbus_metadata_t> metadata;

  pbus_dev_t tee_dev = {};
  tee_dev.name = "tee";
  tee_dev.vid = PDEV_VID_GENERIC;
  tee_dev.pid = PDEV_PID_GENERIC;
  tee_dev.did = PDEV_DID_OPTEE;
  tee_dev.mmio_list = astro_tee_mmios;
  tee_dev.mmio_count = countof(astro_tee_mmios);
  tee_dev.bti_list = astro_tee_btis;
  tee_dev.bti_count = countof(astro_tee_btis);
  tee_dev.smc_list = astro_tee_smcs;
  tee_dev.smc_count = countof(astro_tee_smcs);

  auto optee_status = fidl_metadata::tee::TeeMetadataToFidl(
      ASTRO_OPTEE_DEFAULT_THREAD_COUNT,
      cpp20::span<const tee_thread_config_t>(tee_thread_cfg, countof(tee_thread_cfg)));
  if (optee_status.is_error()) {
    zxlogf(ERROR, "%s: failed to fidl encode optee thread config: %d", __func__,
           optee_status.error_value());
    return optee_status.error_value();
  }

  auto& data = optee_status.value();

  metadata.emplace_back(pbus_metadata_t{
      .type = DEVICE_METADATA_TEE_THREAD_CONFIG,
      .data_buffer = data.data(),
      .data_size = data.size(),
  });

  tee_dev.metadata_count = metadata.size();
  tee_dev.metadata_list = metadata.data();

  zx_status_t status = pbus_.AddComposite(&tee_dev, reinterpret_cast<uint64_t>(tee_fragments),
                                          countof(tee_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace astro
