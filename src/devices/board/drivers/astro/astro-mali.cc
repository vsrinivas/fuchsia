// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpu/amlogic/llcpp/fidl.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-registers.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {

static const pbus_mmio_t mali_mmios[] = {
    {
        .base = S905D2_MALI_BASE,
        .length = S905D2_MALI_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
};

static const pbus_irq_t mali_irqs[] = {
    {
        .irq = S905D2_MALI_IRQ_PP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S905D2_MALI_IRQ_GPMMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
    {
        .irq = S905D2_MALI_IRQ_GP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    },
};

static pbus_bti_t mali_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = 0,
    },
};

static const zx_bind_inst_t reset_register_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_REGISTERS),
    BI_MATCH_IF(EQ, BIND_REGISTER_ID, aml_registers::REGISTER_MALI_RESET),
};
static const device_fragment_part_t reset_register_fragment[] = {
    {countof(reset_register_match), reset_register_match},
};
static const device_fragment_t mali_fragments[] = {
    {"register-reset", countof(reset_register_fragment), reset_register_fragment},
};

zx_status_t Astro::MaliInit() {
  pbus_dev_t mali_dev = {};
  mali_dev.name = "mali";
  mali_dev.vid = PDEV_VID_AMLOGIC;
  mali_dev.pid = PDEV_PID_AMLOGIC_S905D2;
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
  // Populate the BTI information
  mali_btis[0].iommu_index = 0;
  mali_btis[0].bti_id = BTI_MALI;

  zx_status_t status = pbus_.CompositeDeviceAdd(
      &mali_dev, reinterpret_cast<uint64_t>(mali_fragments), countof(mali_fragments), nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d", __func__, status);
    return status;
  }
  return status;
}

}  // namespace astro
