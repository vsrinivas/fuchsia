// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/pwm.h>
#include <soc/aml-t931/t931-pwm.h>

#include "test.h"

namespace board_test {
namespace fpbus = fuchsia_hardware_platform_bus;

namespace {
static const pwm_id_t pwm_ids[] = {
    {T931_PWM_A},
};

static const std::vector<fpbus::Metadata> pwm_metadata{
    []() {
      fpbus::Metadata ret;
      ret.type() = DEVICE_METADATA_PWM_IDS;
      ret.data() =
          std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&pwm_ids),
                               reinterpret_cast<const uint8_t*>(&pwm_ids) + sizeof(pwm_ids));
      return ret;
    }(),
};
}  // namespace

zx_status_t TestBoard::PwmInit() {
  fpbus::Node pwm_dev;
  pwm_dev.name() = "pwm";
  pwm_dev.vid() = PDEV_VID_TEST;
  pwm_dev.pid() = PDEV_PID_PBUS_TEST;
  pwm_dev.did() = PDEV_DID_TEST_PWM;
  pwm_dev.metadata() = pwm_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('TPWM');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, pwm_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: DeviceAdd Pwm request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: DeviceAdd Pwm failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace board_test
