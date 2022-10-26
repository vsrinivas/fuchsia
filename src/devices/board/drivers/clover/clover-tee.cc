// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include "clover.h"
#include "src/devices/board/drivers/clover/clover-tee-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace clover {
namespace fpbus = fuchsia_hardware_platform_bus;
// The CLover Secure OS memory region is defined within the bootloader image. The ZBI provided to
// the kernel must mark this memory space as reserved. The OP-TEE driver will query OP-TEE for the
// exact sub-range of this memory space to be used by the driver.
#define CLOVER_SECURE_OS_BASE 0x03d00000
#define CLOVER_SECURE_OS_LENGTH 0x00300000

static const std::vector<fpbus::Mmio> tee_mmios{
    {{
        .base = CLOVER_SECURE_OS_BASE,
        .length = CLOVER_SECURE_OS_LENGTH,
    }},
};

static const std::vector<fpbus::Bti> tee_btis{
    {{
        .iommu_index = 0,
        .bti_id = BTI_TEE,
    }},
};

static const std::vector<fpbus::Smc> tee_smcs{
    {{
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_TRUSTED_OS_LENGTH,
        .exclusive = false,
    }},
};

static const fpbus::Node tee_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "tee";
  dev.vid() = PDEV_VID_GENERIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_OPTEE;
  dev.mmio() = tee_mmios;
  dev.bti() = tee_btis;
  dev.smc() = tee_smcs;
  return dev;
}();

zx_status_t Clover::TeeInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('TEE_');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, tee_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, tee_fragments, std::size(tee_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "AddComposite Tee(tee_dev) request failed: %s",
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "AddComposite Tee(tee_dev) failed: %s",
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace clover
