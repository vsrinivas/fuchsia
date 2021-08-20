// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/pwm.h>
#include <soc/aml-s905d2/s905d2-pwm.h>

#include "astro-gpios.h"
#include "astro.h"
#include "src/devices/board/drivers/astro/astro-pwm-bind.h"

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

/*
    PWM_AO_B used by bootloader to control PP800_EE rail.  The protect flag is set
    to true to prevent access to that channel as the configuration set by the
    bootloader must be preserved for proper SoC operation.
*/
static const pwm_id_t pwm_ids[] = {
    {S905D2_PWM_A},    {S905D2_PWM_B},    {S905D2_PWM_C},    {S905D2_PWM_D},
    {S905D2_PWM_E},    {S905D2_PWM_F},    {S905D2_PWM_AO_A}, {S905D2_PWM_AO_B, true},
    {S905D2_PWM_AO_C}, {S905D2_PWM_AO_D},
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
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  // Add a composite device for pwm init driver.
  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMLOGIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_AMLOGIC_S905D2},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AMLOGIC_PWM_INIT},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = pwm_init_fragments,
      .fragments_count = countof(pwm_init_fragments),
      .primary_fragment = "pwm",
      .spawn_colocated = false,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite("pwm-init", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace astro
