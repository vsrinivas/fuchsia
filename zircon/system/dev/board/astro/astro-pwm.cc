// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/pwm.h>
#include <ddk/platform-defs.h>
#include <soc/aml-s905d2/s905d2-pwm.h>

#include "astro.h"

namespace astro {

static const pbus_mmio_t pwm_mmios[] = {
    {
        .base = S905D2_PWM_AB_BASE,
        .length = S905D2_PWM_AB_LENGTH,
    },
    {
        .base = S905D2_PWM_CD_BASE,
        .length = S905D2_PWM_AB_LENGTH,
    },
    {
        .base = S905D2_PWM_EF_BASE,
        .length = S905D2_PWM_AB_LENGTH,
    },
    {
        .base = S905D2_AO_PWM_AB_BASE,
        .length = S905D2_AO_PWM_LENGTH,
    },
    {
        .base = S905D2_AO_PWM_CD_BASE,
        .length = S905D2_AO_PWM_LENGTH,
    },
};

static const pwm_id_t pwm_ids[] = {
    {S905D2_PWM_A}, {S905D2_PWM_B},    {S905D2_PWM_C},    {S905D2_PWM_D},    {S905D2_PWM_E},
    {S905D2_PWM_F}, {S905D2_PWM_AO_A}, {S905D2_PWM_AO_B}, {S905D2_PWM_AO_C}, {S905D2_PWM_AO_D},
};

static const pbus_metadata_t pwm_metadata[] = {
    {
        .type = DEVICE_METADATA_PWM_IDS,
        .data_buffer = &pwm_ids,
        .data_size = sizeof(pwm_ids),
    },
};

static pbus_dev_t pwm_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "pwm";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_PWM;
  dev.mmio_list = pwm_mmios;
  dev.mmio_count = countof(pwm_mmios);
  dev.metadata_list = pwm_metadata;
  dev.metadata_count = countof(pwm_metadata);
  return dev;
}();

zx_status_t Astro::PwmInit() {
  zx_status_t status = pbus_.DeviceAdd(&pwm_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace astro
