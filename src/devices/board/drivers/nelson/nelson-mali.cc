// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.gpu.amlogic/cpp/wire.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include <soc/aml-common/aml-registers.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/mali_bind.h"

namespace nelson {

static const pbus_mmio_t mali_mmios[] = {
    {
        .base = S905D3_MALI_BASE,
        .length = S905D3_MALI_LENGTH,
    },
    {
        .base = S905D3_HIU_BASE,
        .length = S905D3_HIU_LENGTH,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = S905D3_MALI_IRQ_PP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S905D3_MALI_IRQ_GPMMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S905D3_MALI_IRQ_GP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static pbus_bti_t mali_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
};

// SMC is used to switch GPU into protected mode.
constexpr pbus_smc_t nelson_mali_smcs[] = {
    {
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_BASE,
        .count = 1,
        // The video decoder and TEE driver also use this SMC range. The aml-gpu driver only uses
        // the kFuncIdConfigDeviceSecure function with DMC_DEV_ID_GPU, and the other users don't
        // touch device ID.
        .exclusive = false,
    },
};

zx_status_t Nelson::MaliInit() {
  pbus_dev_t mali_dev = {};
  mali_dev.name = "mali";
  mali_dev.vid = PDEV_VID_AMLOGIC;
  mali_dev.pid = PDEV_PID_AMLOGIC_S905D3;
  mali_dev.did = PDEV_DID_AMLOGIC_MALI_INIT;
  mali_dev.mmio_list = mali_mmios;
  mali_dev.mmio_count = countof(mali_mmios);
  mali_dev.irq_list = mali_irqs;
  mali_dev.irq_count = countof(mali_irqs);
  mali_dev.bti_list = mali_btis;
  mali_dev.bti_count = countof(mali_btis);
  using fuchsia_hardware_gpu_amlogic::wire::Metadata;
  fidl::Arena allocator;
  Metadata metadata(allocator);
  metadata.set_supports_protected_mode(allocator, true);
  fidl::OwnedEncodedMessage<Metadata> encoded_metadata(&metadata);
  if (!encoded_metadata.ok()) {
    zxlogf(ERROR, "%s: Could not build metadata %s\n", __func__,
           encoded_metadata.FormatDescription().c_str());
    return encoded_metadata.status();
  }
  auto encoded_metadata_bytes = encoded_metadata.GetOutgoingMessage().CopyBytes();
  const pbus_metadata_t mali_metadata_list[] = {
      {
          .type = fuchsia_hardware_gpu_amlogic::wire::kMaliMetadata,
          .data_buffer = encoded_metadata_bytes.data(),
          .data_size = encoded_metadata_bytes.size(),
      },
  };
  mali_dev.metadata_list = mali_metadata_list;
  mali_dev.metadata_count = countof(mali_metadata_list);
  mali_dev.smc_list = nelson_mali_smcs;
  mali_dev.smc_count = countof(nelson_mali_smcs);

  // Populate the BTI information
  mali_btis[0].iommu_index = 0;
  mali_btis[0].bti_id = BTI_MALI;

  zx_status_t status = pbus_.AddComposite(&mali_dev, reinterpret_cast<uint64_t>(mali_fragments),
                                          countof(mali_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "CompositeDeviceAdd failed: %d", status);
    return status;
  }
  return status;
}

}  // namespace nelson
