// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpu/amlogic/llcpp/fidl.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <zircon/syscalls/smc.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-registers.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson.h"

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
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t reset_register_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_REGISTERS),
    BI_MATCH_IF(EQ, BIND_REGISTER_ID, aml_registers::REGISTER_MALI_RESET),
};
static const device_fragment_part_t reset_register_fragment[] = {
    {countof(root_match), root_match},
    {countof(reset_register_match), reset_register_match},
};
static const device_fragment_t mali_fragments[] = {
    {"register-reset", countof(reset_register_fragment), reset_register_fragment},
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
  using ::fuchsia_hardware_gpu_amlogic::wire::Metadata;
  auto metadata = Metadata::Builder(std::make_unique<Metadata::Frame>())
                      .set_supports_protected_mode(std::make_unique<bool>(true))
                      .build();
  fidl::OwnedEncodedMessage<Metadata> encoded_metadata(&metadata);
  if (!encoded_metadata.ok() || (encoded_metadata.error() != nullptr)) {
    zxlogf(ERROR, "%s: Could not build metadata %s\n", __func__, encoded_metadata.error());
    return encoded_metadata.status();
  }
  const pbus_metadata_t mali_metadata_list[] = {
      {
          .type = fuchsia_hardware_gpu_amlogic::wire::MALI_METADATA,
          .data_buffer = encoded_metadata.GetOutgoingMessage().bytes(),
          .data_size = encoded_metadata.GetOutgoingMessage().byte_actual(),
      },
  };
  mali_dev.metadata_list = mali_metadata_list;
  mali_dev.metadata_count = countof(mali_metadata_list);
  mali_dev.smc_list = nelson_mali_smcs;
  mali_dev.smc_count = countof(nelson_mali_smcs);

  // Populate the BTI information
  mali_btis[0].iommu_index = 0;
  mali_btis[0].bti_id = BTI_MALI;

  zx_status_t status = pbus_.CompositeDeviceAdd(
      &mali_dev, reinterpret_cast<uint64_t>(mali_fragments), countof(mali_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "CompositeDeviceAdd failed: %d", status);
    return status;
  }
  return status;
}

}  // namespace nelson
