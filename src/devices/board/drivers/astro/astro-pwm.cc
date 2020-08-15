// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/pwm.h>
#include <ddk/platform-defs.h>
#include <soc/aml-s905d2/s905d2-pwm.h>

#include "astro-gpios.h"
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

// Composite binding rules for wifi driver.
static const zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t pwm_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, S905D2_PWM_E),
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
    {countof(root_match), root_match},
    {countof(pwm_match), pwm_match},
};
static const device_fragment_part_t wifi_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(wifi_gpio_match), wifi_gpio_match},
};
static const device_fragment_part_t bt_gpio_fragment[] = {
    {countof(root_match), root_match},
    {countof(bt_gpio_match), bt_gpio_match},
};
static const device_fragment_t composite[] = {
    {countof(pwm_fragment), pwm_fragment},
    {countof(wifi_gpio_fragment), wifi_gpio_fragment},
    {countof(bt_gpio_fragment), bt_gpio_fragment},
};

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
      .fragments = composite,
      .fragments_count = countof(composite),
      .coresident_device_index = UINT32_MAX,
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
