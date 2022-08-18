// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/power.h>
#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-a5/a5-power.h>
#include <soc/aml-common/aml-power.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/av400-power-domain-bind.h"
#include "src/devices/board/drivers/av400/av400-power-regulator-bind.h"

namespace av400 {

namespace {

// Vddcpu0: pwm-f regulator
static constexpr aml_voltage_table_t kA5VoltageTable[] = {
    {1'049'000, 0}, {1'039'000, 3}, {1'029'000, 6}, {1'019'000, 9}, {1'009'000, 12}, {999'000, 14},
    {989'000, 17},  {979'000, 20},  {969'000, 23},  {959'000, 26},  {949'000, 29},   {939'000, 31},
    {929'000, 34},  {919'000, 37},  {909'000, 40},  {899'000, 43},  {889'000, 45},   {879'000, 48},
    {869'000, 51},  {859'000, 54},  {849'000, 56},  {839'000, 59},  {829'000, 62},   {819'000, 65},
    {809'000, 68},  {799'000, 70},  {789'000, 73},  {779'000, 76},  {769'000, 79},   {759'000, 81},
    {749'000, 84},  {739'000, 87},  {729'000, 89},  {719'000, 92},  {709'000, 95},   {699'000, 98},
    {689'000, 100},
};

static constexpr voltage_pwm_period_ns_t kA5PwmPeriodNs = 1500;

static const pbus_metadata_t power_impl_metadata[] = {
    {
        .type = DEVICE_METADATA_AML_VOLTAGE_TABLE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&kA5VoltageTable),
        .data_size = sizeof(kA5VoltageTable),
    },
    {
        .type = DEVICE_METADATA_AML_PWM_PERIOD_NS,
        .data_buffer = reinterpret_cast<const uint8_t*>(&kA5PwmPeriodNs),
        .data_size = sizeof(kA5PwmPeriodNs),
    },
};

zx_device_prop_t power_domain_props[] = {
    {BIND_POWER_DOMAIN_COMPOSITE, 0, PDEV_DID_POWER_DOMAIN_COMPOSITE},
};

constexpr power_domain_t domains[] = {
    {static_cast<uint32_t>(A5PowerDomains::kArmCore)},
};

constexpr device_metadata_t power_domain_metadata[] = {
    {
        .type = DEVICE_METADATA_POWER_DOMAINS,
        .data = &domains,
        .length = sizeof(domains),
    },
};

constexpr composite_device_desc_t power_domain_desc = {
    .props = power_domain_props,
    .props_count = std::size(power_domain_props),
    .fragments = power_domain_fragments,
    .fragments_count = std::size(power_domain_fragments),
    .primary_fragment = "power-impl",
    .spawn_colocated = true,
    .metadata_list = power_domain_metadata,
    .metadata_count = std::size(power_domain_metadata),
};

}  // namespace

static constexpr pbus_dev_t power_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-power-impl-composite";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A5;
  dev.did = PDEV_DID_AMLOGIC_POWER;
  dev.metadata_list = power_impl_metadata;
  dev.metadata_count = std::size(power_impl_metadata);
  return dev;
}();

zx_status_t Av400::PowerInit() {
  zx_status_t status;

  status = pbus_.AddComposite(&power_dev, reinterpret_cast<uint64_t>(power_regulator_fragments),
                              std::size(power_regulator_fragments), "pdev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "AddComposite failed: %s", zx_status_get_string(status));
    return status;
  }

  status = DdkAddComposite("composite-pd-armcore", &power_domain_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAddComposite failed: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace av400
