// Copyright 2021 The Fuchsia Authors. All rights reserved.
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
#include <soc/aml-a311d/a311d-pwm.h>

#include "vim3-gpios.h"
#include "vim3.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> pwm_mmios{
    {{
        .base = A311D_PWM_AB_BASE,
        .length = A311D_PWM_LENGTH,
    }},
    {{
        .base = A311D_PWM_CD_BASE,
        .length = A311D_PWM_LENGTH,
    }},
    {{
        .base = A311D_PWM_EF_BASE,
        .length = A311D_PWM_LENGTH,
    }},
    {{
        .base = A311D_AO_PWM_AB_BASE,
        .length = A311D_AO_PWM_LENGTH,
    }},
    {{
        .base = A311D_AO_PWM_CD_BASE,
        .length = A311D_AO_PWM_LENGTH,
    }},
};

static const pwm_id_t pwm_ids[] = {
    {A311D_PWM_A}, {A311D_PWM_B},    {A311D_PWM_C},    {A311D_PWM_D},    {A311D_PWM_E},
    {A311D_PWM_F}, {A311D_PWM_AO_A}, {A311D_PWM_AO_B}, {A311D_PWM_AO_C}, {A311D_PWM_AO_D},
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
  dev.pid() = PDEV_PID_AMLOGIC_A311D;
  dev.did() = PDEV_DID_AMLOGIC_PWM;
  dev.mmio() = pwm_mmios;
  dev.metadata() = pwm_metadata;
  return dev;
}();

zx_status_t Vim3::PwmInit() {
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

  return ZX_OK;
}

}  // namespace vim3
