// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/power.h>
#include <soc/aml-common/aml-power.h>
#include <soc/aml-s905d2/s905d2-power.h>
#include <soc/aml-s905d2/s905d2-pwm.h>

#include "astro-gpios.h"
#include "astro.h"

namespace astro {

namespace {

constexpr aml_voltage_table_t kS905D2VoltageTable[] = {
    {1'022'000, 0}, {1'011'000, 3}, {1'001'000, 6}, {991'000, 10}, {981'000, 13}, {971'000, 16},
    {961'000, 20},  {951'000, 23},  {941'000, 26},  {931'000, 30}, {921'000, 33}, {911'000, 36},
    {901'000, 40},  {891'000, 43},  {881'000, 46},  {871'000, 50}, {861'000, 53}, {851'000, 56},
    {841'000, 60},  {831'000, 63},  {821'000, 67},  {811'000, 70}, {801'000, 73}, {791'000, 76},
    {781'000, 80},  {771'000, 83},  {761'000, 86},  {751'000, 90}, {741'000, 93}, {731'000, 96},
    {721'000, 100},
};

constexpr voltage_pwm_period_ns_t kS905d2PwmPeriodNs = 1250;

const pbus_metadata_t power_impl_metadata[] = {
    {
        .type = DEVICE_METADATA_AML_VOLTAGE_TABLE,
        .data_buffer = reinterpret_cast<const uint8_t*>(&kS905D2VoltageTable),
        .data_size = sizeof(kS905D2VoltageTable),
    },
    {
        .type = DEVICE_METADATA_AML_PWM_PERIOD_NS,
        .data_buffer = reinterpret_cast<const uint8_t*>(&kS905d2PwmPeriodNs),
        .data_size = sizeof(kS905d2PwmPeriodNs),
    },
};

constexpr zx_bind_inst_t power_impl_driver_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_POWER_IMPL),
};

constexpr device_fragment_part_t power_impl_fragment[] = {
    {countof(power_impl_driver_match), power_impl_driver_match},
};

zx_device_prop_t power_domain_arm_core_props[] = {
    {BIND_POWER_DOMAIN_COMPOSITE, 0, PDEV_DID_POWER_DOMAIN_COMPOSITE},
};

constexpr device_fragment_t power_domain_arm_core_fragments[] = {
    {"power-impl", countof(power_impl_fragment), power_impl_fragment},
};

constexpr power_domain_t domains[] = {
    {static_cast<uint32_t>(S905d2PowerDomains::kArmCore)},
};

constexpr device_metadata_t power_domain_arm_core_metadata[] = {
    {
        .type = DEVICE_METADATA_POWER_DOMAINS,
        .data = &domains,
        .length = sizeof(domains),
    },
};

constexpr composite_device_desc_t power_domain_arm_core_desc = {
    .props = power_domain_arm_core_props,
    .props_count = countof(power_domain_arm_core_props),
    .fragments = power_domain_arm_core_fragments,
    .fragments_count = countof(power_domain_arm_core_fragments),
    .primary_fragment = "power-impl",
    .spawn_colocated = true,
    .metadata_list = power_domain_arm_core_metadata,
    .metadata_count = countof(power_domain_arm_core_metadata),
};

constexpr zx_bind_inst_t pwm_ao_d_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, S905D2_PWM_AO_D),
};

constexpr device_fragment_part_t pwm_ao_d_fragment[] = {
    {countof(pwm_ao_d_match), pwm_ao_d_match},
};

constexpr device_fragment_t power_impl_fragments[] = {
    {"pwm-ao-d", countof(pwm_ao_d_fragment), pwm_ao_d_fragment},
};

}  // namespace

static const pbus_dev_t power_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-power-impl-composite";
  dev.vid = PDEV_VID_GOOGLE;
  dev.pid = PDEV_PID_ASTRO;
  dev.did = PDEV_DID_AMLOGIC_POWER;
  dev.metadata_list = power_impl_metadata;
  dev.metadata_count = countof(power_impl_metadata);
  return dev;
}();

zx_status_t Astro::PowerInit() {
  zx_status_t st;

  st = pbus_.CompositeDeviceAdd(&power_dev, reinterpret_cast<uint64_t>(power_impl_fragments),
                                countof(power_impl_fragments), UINT32_MAX);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd for powerimpl failed, st = %d", __FUNCTION__, st);
    return st;
  }

  st = DdkAddComposite("composite-pd-armcore", &power_domain_arm_core_desc);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd for power domain ArmCore failed, st = %d", __FUNCTION__,
           st);
    return st;
  }

  return ZX_OK;
}

}  // namespace astro
