// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.gpu.amlogic/cpp/wire.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include <soc/aml-common/aml-registers.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_mali_bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> mali_mmios{
    {{
        .base = S905D3_MALI_BASE,
        .length = S905D3_MALI_LENGTH,
    }},
    {{
        .base = S905D3_HIU_BASE,
        .length = S905D3_HIU_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> mali_irqs{
    {{
        .irq = S905D3_MALI_IRQ_PP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
    {{
        .irq = S905D3_MALI_IRQ_GPMMU,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
    {{
        .irq = S905D3_MALI_IRQ_GP,
        .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
    }},
};

static const std::vector<fpbus::Bti> mali_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_MALI,
    }},
};

// SMC is used to switch GPU into protected mode.
static const std::vector<fpbus::Smc> nelson_mali_smcs{
    {{
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_BASE,
        .count = 1,
        // The video decoder and TEE driver also use this SMC range. The aml-gpu driver only uses
        // the kFuncIdConfigDeviceSecure function with DMC_DEV_ID_GPU, and the other users don't
        // touch device ID.
        .exclusive = false,
    }},
};

zx_status_t Nelson::MaliInit() {
  fpbus::Node mali_dev;
  mali_dev.name() = "mali";
  mali_dev.vid() = PDEV_VID_AMLOGIC;
  mali_dev.pid() = PDEV_PID_AMLOGIC_S905D3;
  mali_dev.did() = PDEV_DID_AMLOGIC_MALI_INIT;
  mali_dev.mmio() = mali_mmios;
  mali_dev.irq() = mali_irqs;
  mali_dev.bti() = mali_btis;
  using fuchsia_hardware_gpu_amlogic::wire::Metadata;
  fidl::Arena allocator;
  Metadata metadata(allocator);
  metadata.set_supports_protected_mode(true);
  fidl::unstable::OwnedEncodedMessage<Metadata> encoded_metadata(
      fidl::internal::WireFormatVersion::kV2, &metadata);
  if (!encoded_metadata.ok()) {
    zxlogf(ERROR, "%s: Could not build metadata %s\n", __func__,
           encoded_metadata.FormatDescription().c_str());
    return encoded_metadata.status();
  }
  auto encoded_metadata_bytes = encoded_metadata.GetOutgoingMessage().CopyBytes();
  const std::vector<fpbus::Metadata> mali_metadata_list{
      {{
          .type = fuchsia_hardware_gpu_amlogic::wire::kMaliMetadata,
          .data =
              std::vector<uint8_t>(encoded_metadata_bytes.data(),
                                   encoded_metadata_bytes.data() + encoded_metadata_bytes.size()),
      }},
  };
  mali_dev.metadata() = mali_metadata_list;
  mali_dev.smc() = nelson_mali_smcs;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('MALI');
  auto result =
      pbus_.buffer(arena)->AddComposite(fidl::ToWire(fidl_arena, mali_dev),
                                        platform_bus_composite::MakeFidlFragment(
                                            fidl_arena, mali_fragments, std::size(mali_fragments)),
                                        "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Mali(mali_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Mali(mali_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace nelson
