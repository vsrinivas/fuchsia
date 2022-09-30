// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/pwm.h>
#include <soc/aml-s905d3/s905d3-pwm.h>

#include "nelson-gpios.h"
#include "nelson.h"
#include "src/devices/board/drivers/nelson/pwm_init_bind.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> pwm_mmios{
    {{
        .base = S905D3_PWM_AB_BASE,
        .length = S905D3_PWM_AB_LENGTH,
    }},
    {{
        .base = S905D3_PWM_CD_BASE,
        .length = S905D3_PWM_AB_LENGTH,
    }},
    {{
        .base = S905D3_PWM_EF_BASE,
        .length = S905D3_PWM_AB_LENGTH,
    }},
    {{
        .base = S905D3_AO_PWM_AB_BASE,
        .length = S905D3_AO_PWM_LENGTH,
    }},
    {{
        .base = S905D3_AO_PWM_CD_BASE,
        .length = S905D3_AO_PWM_LENGTH,
    }},
};

static const pwm_id_t pwm_ids[] = {
    {S905D3_PWM_A},    {S905D3_PWM_B},
    {S905D3_PWM_C},    {S905D3_PWM_D},
    {S905D3_PWM_E},    {S905D3_PWM_F},
    {S905D3_PWM_AO_A}, {S905D3_PWM_AO_B, /*init = */ false},
    {S905D3_PWM_AO_C}, {S905D3_PWM_AO_D, /*init = */ false},
};

static const std::vector<fpbus::Metadata> pwm_metadata{
    {{
        .type = DEVICE_METADATA_PWM_IDS,
        .data = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&pwm_ids),
                                     reinterpret_cast<const uint8_t*>(&pwm_ids) + sizeof(pwm_ids)),
    }},
};

static const fpbus::Node pwm_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "pwm";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_S905D3;
  dev.did() = PDEV_DID_AMLOGIC_PWM;
  dev.mmio() = pwm_mmios;
  dev.metadata() = pwm_metadata;
  return dev;
}();

// Composite binding rules for wifi driver.

zx_status_t Nelson::PwmInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('PWM_');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, pwm_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Pwm(pwm_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Pwm(pwm_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  // Add a composite device for pwm init driver.
  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMLOGIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_AMLOGIC_S905D3},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AMLOGIC_PWM_INIT},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = pwm_init_fragments,
      .fragments_count = std::size(pwm_init_fragments),
      .primary_fragment = "pwm",
      .spawn_colocated = false,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  zx_status_t status = DdkAddComposite("pwm-init", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite failed: %d", __func__, status);
    return status;
  }

  zxlogf(INFO, "Added PwmInitDevice");

  return ZX_OK;
}

}  // namespace nelson
