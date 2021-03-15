// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/platform-defs.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/pwm.h>
#include <soc/aml-a311d/a311d-pwm.h>

#include "vim3-gpios.h"
#include "vim3.h"

namespace vim3 {

static const pbus_mmio_t pwm_mmios[] = {
    {
        .base = A311D_PWM_AB_BASE,
        .length = A311D_PWM_LENGTH,
    },
    {
        .base = A311D_PWM_CD_BASE,
        .length = A311D_PWM_LENGTH,
    },
    {
        .base = A311D_PWM_EF_BASE,
        .length = A311D_PWM_LENGTH,
    },
    {
        .base = A311D_AO_PWM_AB_BASE,
        .length = A311D_AO_PWM_LENGTH,
    },
    {
        .base = A311D_AO_PWM_CD_BASE,
        .length = A311D_AO_PWM_LENGTH,
    },
};

static const pwm_id_t pwm_ids[] = {
    {A311D_PWM_A}, {A311D_PWM_B},    {A311D_PWM_C},    {A311D_PWM_D},    {A311D_PWM_E},
    {A311D_PWM_F}, {A311D_PWM_AO_A}, {A311D_PWM_AO_B}, {A311D_PWM_AO_C}, {A311D_PWM_AO_D},
};

static const pbus_metadata_t pwm_metadata[] = {
    {
        .type = DEVICE_METADATA_PWM_IDS,
        .data_buffer = reinterpret_cast<const uint8_t*>(&pwm_ids),
        .data_size = sizeof(pwm_ids),
    },
};

static pbus_dev_t pwm_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "pwm";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A311D;
  dev.did = PDEV_DID_AMLOGIC_PWM;
  dev.mmio_list = pwm_mmios;
  dev.mmio_count = countof(pwm_mmios);
  dev.metadata_list = pwm_metadata;
  dev.metadata_count = countof(pwm_metadata);
  return dev;
}();

zx_status_t Vim3::PwmInit() {
  zx_status_t status = pbus_.DeviceAdd(&pwm_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace vim3
