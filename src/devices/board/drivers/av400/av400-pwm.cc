// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/pwm.h>
#include <soc/aml-a5/a5-pwm.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/pwm_init_bind.h"

namespace av400 {

static const pbus_mmio_t pwm_mmios[] = {
    {
        .base = A5_PWM_AB_BASE,
        .length = A5_PWM_LENGTH,
    },
    {
        .base = A5_PWM_CD_BASE,
        .length = A5_PWM_LENGTH,
    },
    {
        .base = A5_PWM_EF_BASE,
        .length = A5_PWM_LENGTH,
    },
    {
        .base = A5_PWM_GH_BASE,
        .length = A5_PWM_LENGTH,
    },
};

static const pwm_id_t pwm_ids[] = {
    {A5_PWM_A},
    {A5_PWM_B},
    {A5_PWM_C},
    {A5_PWM_D},
    // PWM_E drives the regulator for the EE voltage rail.
    // Marked as false, so we don't try to turn off it.
    {A5_PWM_E, /* init = */ false},
    // PWM_F drives the regulator for the CPU voltage rail.
    // Marked as false, so we don't try to turn off it.
    {A5_PWM_F, /* init = */ false},
    {A5_PWM_G},
    {A5_PWM_H},
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
  dev.pid = PDEV_PID_AMLOGIC_A5;
  dev.did = PDEV_DID_AMLOGIC_PWM;
  dev.mmio_list = pwm_mmios;
  dev.mmio_count = std::size(pwm_mmios);
  dev.metadata_list = pwm_metadata;
  dev.metadata_count = std::size(pwm_metadata);
  return dev;
}();

zx_status_t Av400::PwmInit() {
  zx_status_t status = pbus_.DeviceAdd(&pwm_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %s", __func__, zx_status_get_string(status));
    return status;
  }

  // Add a composite device for pwm init driver.
  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMLOGIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_AMLOGIC_A5},
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

  status = DdkAddComposite("pwm-init", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite failed: %d", __func__, status);
    return status;
  }

  zxlogf(INFO, "Added PwmInitDevice");

  return ZX_OK;
}

}  // namespace av400
