// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/pwm.h>
#include <ddk/platform-defs.h>
#include <soc/aml-t931/t931-pwm.h>

#include "test.h"

namespace board_test {

namespace {
static const pwm_id_t pwm_ids[] = {
    {T931_PWM_A},
};

static const pbus_metadata_t pwm_metadata[] = {
    {
        .type = DEVICE_METADATA_PWM_IDS,
        .data_buffer = &pwm_ids,
        .data_size = sizeof(pwm_ids),
    },
};
}  // namespace

zx_status_t TestBoard::PwmInit() {
  pbus_dev_t pwm_dev = {};
  pwm_dev.name = "pwm";
  pwm_dev.vid = PDEV_VID_TEST;
  pwm_dev.pid = PDEV_PID_PBUS_TEST;
  pwm_dev.did = PDEV_DID_TEST_PWM;
  pwm_dev.metadata_list = pwm_metadata;
  pwm_dev.metadata_count = countof(pwm_metadata);

  zx_status_t status = pbus_.DeviceAdd(&pwm_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
