// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/pwm.h>
#include <soc/aml-t931/t931-pwm.h>

#include "sherlock-gpios.h"
#include "sherlock.h"

namespace sherlock {

static const pbus_mmio_t pwm_mmios[] = {
    {
        .base = T931_PWM_AB_BASE,
        .length = T931_PWM_LENGTH,
    },
    {
        .base = T931_PWM_CD_BASE,
        .length = T931_PWM_LENGTH,
    },
    {
        .base = T931_PWM_EF_BASE,
        .length = T931_PWM_LENGTH,
    },
    {
        .base = T931_AO_PWM_AB_BASE,
        .length = T931_AO_PWM_LENGTH,
    },
    {
        .base = T931_AO_PWM_CD_BASE,
        .length = T931_AO_PWM_LENGTH,
    },
};

static const pwm_id_t pwm_ids[] = {
    {T931_PWM_A}, {T931_PWM_B},    {T931_PWM_C},    {T931_PWM_D},    {T931_PWM_E},
    {T931_PWM_F}, {T931_PWM_AO_A}, {T931_PWM_AO_B}, {T931_PWM_AO_C}, {T931_PWM_AO_D},
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
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_PWM;
  dev.mmio_list = pwm_mmios;
  dev.mmio_count = countof(pwm_mmios);
  dev.metadata_list = pwm_metadata;
  dev.metadata_count = countof(pwm_metadata);
  return dev;
}();

// Composite binding rules for pwm init driver.
static const zx_bind_inst_t pwm_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, T931_PWM_E),
};
static const zx_bind_inst_t wifi_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_SOC_WIFI_LPO_32k768),
};
static const zx_bind_inst_t bt_gpio_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
    BI_MATCH_IF(EQ, BIND_GPIO_PIN, GPIO_SOC_BT_REG_ON),
};
static const device_fragment_part_t pwm_fragment[] = {
    {countof(pwm_match), pwm_match},
};
static const device_fragment_part_t wifi_gpio_fragment[] = {
    {countof(wifi_gpio_match), wifi_gpio_match},
};
static const device_fragment_part_t bt_gpio_fragment[] = {
    {countof(bt_gpio_match), bt_gpio_match},
};
static const device_fragment_t composite[] = {
    {"pwm", countof(pwm_fragment), pwm_fragment},
    {"gpio-wifi", countof(wifi_gpio_fragment), wifi_gpio_fragment},
    {"gpio-bt", countof(bt_gpio_fragment), bt_gpio_fragment},
};

zx_status_t Sherlock::PwmInit() {
  zx_status_t status = pbus_.DeviceAdd(&pwm_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __func__, status);
    return status;
  }

  // Add a composite device for pwm init driver.
  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_AMLOGIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_AMLOGIC_T931},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_AMLOGIC_PWM_INIT},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = composite,
      .fragments_count = countof(composite),
      .primary_fragment = "pwm",
      .spawn_colocated = true,
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

}  // namespace sherlock
