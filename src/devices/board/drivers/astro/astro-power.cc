// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
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
#include "src/devices/board/drivers/astro/pd-armcore-bind.h"
#include "src/devices/board/drivers/astro/pwm-ao-d-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace astro {
namespace fpbus = fuchsia_hardware_platform_bus;

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

static const std::vector<fpbus::Metadata> power_impl_metadata{
    {{
        .type = DEVICE_METADATA_AML_VOLTAGE_TABLE,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&kS905D2VoltageTable),
            reinterpret_cast<const uint8_t*>(&kS905D2VoltageTable) + sizeof(kS905D2VoltageTable)),
    }},
    {{
        .type = DEVICE_METADATA_AML_PWM_PERIOD_NS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&kS905d2PwmPeriodNs),
            reinterpret_cast<const uint8_t*>(&kS905d2PwmPeriodNs) + sizeof(kS905d2PwmPeriodNs)),
    }},
};

zx_device_prop_t power_domain_arm_core_props[] = {
    {BIND_POWER_DOMAIN_COMPOSITE, 0, PDEV_DID_POWER_DOMAIN_COMPOSITE},
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
    .props_count = std::size(power_domain_arm_core_props),
    .fragments = power_domain_arm_core_fragments,
    .fragments_count = std::size(power_domain_arm_core_fragments),
    .primary_fragment = "power-impl",
    .spawn_colocated = true,
    .metadata_list = power_domain_arm_core_metadata,
    .metadata_count = std::size(power_domain_arm_core_metadata),
};

}  // namespace

static const fpbus::Node power_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-power-impl-composite";
  dev.vid() = PDEV_VID_GOOGLE;
  dev.pid() = PDEV_PID_ASTRO;
  dev.did() = PDEV_DID_AMLOGIC_POWER;
  dev.metadata() = power_impl_metadata;
  return dev;
}();

zx_status_t Astro::PowerInit() {
  zx_status_t st;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('POWE');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, power_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, aml_power_impl_fragments,
                                               std::size(aml_power_impl_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Power(power_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Power(power_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  st = DdkAddComposite("composite-pd-armcore", &power_domain_arm_core_desc);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite for power domain ArmCore failed, st = %d", __FUNCTION__, st);
    return st;
  }

  return ZX_OK;
}

}  // namespace astro
