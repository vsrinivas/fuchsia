// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <string>

#include <ddk/metadata/power.h>
#include <ddk/metadata/pwm.h>
#include <soc/aml-a311d/a311d-power.h>
#include <soc/aml-a311d/a311d-pwm.h>
#include <soc/aml-common/aml-power.h>

#include "src/devices/lib/metadata/llcpp/vreg.h"
#include "vim3-gpios.h"
#include "vim3.h"

namespace vim3 {

namespace {

const zx_device_prop_t vreg_props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_PWM_VREG},
};

constexpr zx_bind_inst_t pwm_ao_d_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, A311D_PWM_AO_D),
};

constexpr zx_bind_inst_t pwm_a_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, A311D_PWM_A),
};

constexpr device_fragment_part_t pwm_ao_d_fragment[] = {
    {countof(pwm_ao_d_match), pwm_ao_d_match},
};

constexpr device_fragment_part_t pwm_a_fragment[] = {
    {countof(pwm_a_match), pwm_a_match},
};
#define PWM_ID(x) #x
#define PWM_FRAGMENT_NAME(x) ("pwm-" PWM_ID(x))
constexpr device_fragment_t vreg_fragments[] = {
    {PWM_FRAGMENT_NAME(A311D_PWM_AO_D), countof(pwm_ao_d_fragment), pwm_ao_d_fragment},
    {PWM_FRAGMENT_NAME(A311D_PWM_A), countof(pwm_a_fragment), pwm_a_fragment},
};
#undef PWM_FRAGMENT_NAME
#undef PWM_ID

constexpr voltage_pwm_period_ns_t kA311dPwmPeriodNs = 1500;

const uint32_t kVoltageStepUv = 10'000;
static_assert((kMaxVoltageUv - kMinVoltageUv) % kVoltageStepUv == 0,
              "Voltage step must be a factor of (kMaxVoltageUv - kMinVoltageUv)\n");
const uint32_t kNumSteps = (kMaxVoltageUv - kMinVoltageUv) / kVoltageStepUv + 1;

enum VregIdx {
  PWM_AO_D_VREG,
  PWM_A_VREG,

  VREG_COUNT,
};

constexpr zx_bind_inst_t vreg_pwm_ao_d_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_VREG),
    BI_MATCH_IF(EQ, BIND_PWM_ID, A311D_PWM_AO_D),
};

constexpr zx_bind_inst_t vreg_pwm_a_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_VREG),
    BI_MATCH_IF(EQ, BIND_PWM_ID, A311D_PWM_A),
};

constexpr device_fragment_part_t vreg_pwm_ao_d_fragment[] = {
    {countof(vreg_pwm_ao_d_match), vreg_pwm_ao_d_match},
};

constexpr device_fragment_part_t vreg_pwm_a_fragment[] = {
    {countof(vreg_pwm_a_match), vreg_pwm_a_match},
};

constexpr device_fragment_t power_impl_fragments[] = {
    {"vreg-pwm-ao-d", countof(vreg_pwm_ao_d_fragment), vreg_pwm_ao_d_fragment},
    {"vreg-pwm-a", countof(vreg_pwm_a_fragment), vreg_pwm_a_fragment},
};

constexpr zx_bind_inst_t power_impl_driver_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_POWER_IMPL),
};

constexpr device_fragment_part_t power_impl_fragment[] = {
    {countof(power_impl_driver_match), power_impl_driver_match},
};

static const pbus_dev_t power_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-power-impl-composite";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A311D;
  dev.did = PDEV_DID_AMLOGIC_POWER;
  return dev;
}();

zx_device_prop_t power_domain_arm_core_props[] = {
    {BIND_POWER_DOMAIN_COMPOSITE, 0, PDEV_DID_POWER_DOMAIN_COMPOSITE},
};

constexpr device_fragment_t power_domain_arm_core_fragments[] = {
    {"power-impl", countof(power_impl_fragment), power_impl_fragment},
};

constexpr power_domain_t big_domain[] = {
    {static_cast<uint32_t>(A311dPowerDomains::kArmCoreBig)},
};

constexpr device_metadata_t power_domain_big_core_metadata[] = {
    {
        .type = DEVICE_METADATA_POWER_DOMAINS,
        .data = &big_domain,
        .length = sizeof(big_domain),
    },
};

constexpr composite_device_desc_t power_domain_big_core_desc = {
    .props = power_domain_arm_core_props,
    .props_count = countof(power_domain_arm_core_props),
    .fragments = power_domain_arm_core_fragments,
    .fragments_count = countof(power_domain_arm_core_fragments),
    .primary_fragment = "power-impl",
    .coresident_device_index = 0,
    .metadata_list = power_domain_big_core_metadata,
    .metadata_count = countof(power_domain_big_core_metadata),
};

constexpr power_domain_t little_domain[] = {
    {static_cast<uint32_t>(A311dPowerDomains::kArmCoreLittle)},
};

constexpr device_metadata_t power_domain_little_core_metadata[] = {
    {
        .type = DEVICE_METADATA_POWER_DOMAINS,
        .data = &little_domain,
        .length = sizeof(little_domain),
    },
};

constexpr composite_device_desc_t power_domain_little_core_desc = {
    .props = power_domain_arm_core_props,
    .props_count = countof(power_domain_arm_core_props),
    .fragments = power_domain_arm_core_fragments,
    .fragments_count = countof(power_domain_arm_core_fragments),
    .primary_fragment = "power-impl",
    .coresident_device_index = 0,
    .metadata_list = power_domain_little_core_metadata,
    .metadata_count = countof(power_domain_little_core_metadata),
};

zx_device_prop_t fusb302_props[] = {
    {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
    {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
    {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_FUSB302},
};

constexpr zx_bind_inst_t i2c_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
    BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x22),
};

constexpr device_fragment_part_t i2c_fragment[] = {
    {countof(i2c_match), i2c_match},
};

constexpr device_fragment_t fusb302_fragments[] = {
    {"i2c", countof(i2c_fragment), i2c_fragment},
};

constexpr composite_device_desc_t fusb302_desc = {
    .props = fusb302_props,
    .props_count = countof(fusb302_props),
    .fragments = fusb302_fragments,
    .fragments_count = countof(fusb302_fragments),
    .primary_fragment = "i2c",
    .coresident_device_index = 0,
};

}  // namespace

zx_status_t Vim3::PowerInit() {
  zx_status_t st;
  st = gpio_impl_.ConfigOut(A311D_GPIOE(1), 0);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d", __func__, st);
    return st;
  }

  // Configure the GPIO to be Output & set it to alternate
  // function 3 which puts in PWM_D mode. A53 cluster (Small)
  st = gpio_impl_.SetAltFunction(A311D_GPIOE(1), A311D_GPIOE_1_PWM_D_FN);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: SetAltFunction failed: %d", __func__, st);
    return st;
  }

  st = gpio_impl_.ConfigOut(A311D_GPIOE(2), 0);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d", __func__, st);
    return st;
  }

  // Configure the GPIO to be Output & set it to alternate
  // function 3 which puts in PWM_D mode. A73 cluster (Big)
  st = gpio_impl_.SetAltFunction(A311D_GPIOE(2), A311D_GPIOE_2_PWM_D_FN);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: SetAltFunction failed: %d", __func__, st);
    return st;
  }

  // Add voltage regulator
  fidl::FidlAllocator<2048> allocator;
  fidl::VectorView<vreg::PwmVregMetadataEntry> pwm_vreg_entries(allocator, VREG_COUNT);

  pwm_vreg_entries[PWM_AO_D_VREG] = vreg::BuildMetadata(
      allocator, A311D_PWM_AO_D, kA311dPwmPeriodNs, kMinVoltageUv, kVoltageStepUv, kNumSteps);
  pwm_vreg_entries[PWM_A_VREG] = vreg::BuildMetadata(allocator, A311D_PWM_A, kA311dPwmPeriodNs,
                                                     kMinVoltageUv, kVoltageStepUv, kNumSteps);

  auto metadata = vreg::BuildMetadata(allocator, std::move(pwm_vreg_entries));
  fidl::OwnedEncodedMessage<vreg::Metadata> encoded_metadata(&metadata);
  if (!encoded_metadata.ok()) {
    zxlogf(ERROR, "%s: Could not build metadata %s\n", __func__,
           encoded_metadata.FormatDescription().c_str());
    return encoded_metadata.status();
  }

  auto encoded_metadata_bytes = encoded_metadata.GetOutgoingMessage().CopyBytes();
  static const device_metadata_t vreg_metadata[] = {
      {
          .type = DEVICE_METADATA_VREG,
          .data = encoded_metadata_bytes.data(),
          .length = encoded_metadata_bytes.size(),
      },
  };

  static composite_device_desc_t vreg_desc = []() {
    composite_device_desc_t dev = {};
    dev.props = vreg_props;
    dev.props_count = countof(vreg_props);
    dev.fragments = vreg_fragments;
    dev.fragments_count = countof(vreg_fragments);
    dev.primary_fragment = vreg_fragments[0].name,  // ???
        dev.coresident_device_index = 0;
    dev.metadata_list = vreg_metadata;
    dev.metadata_count = countof(vreg_metadata);
    return dev;
  }();

  st = DdkAddComposite("vreg", &vreg_desc);
  if (st != ZX_OK) {
    zxlogf(ERROR, "DdkAddComposite for vreg failed, st = %d", st);
    return st;
  }

  st = pbus_.CompositeDeviceAdd(&power_dev, reinterpret_cast<uint64_t>(power_impl_fragments),
                                countof(power_impl_fragments), UINT32_MAX);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd for powerimpl failed, st = %d", __FUNCTION__, st);
    return st;
  }

  st = DdkAddComposite("pd-big-core", &power_domain_big_core_desc);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd for power domain Big Arm Core failed, st = %d",
           __FUNCTION__, st);
    return st;
  }

  st = DdkAddComposite("pd-little-core", &power_domain_little_core_desc);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd for power domain Little Arm Core failed, st = %d",
           __FUNCTION__, st);
    return st;
  }

  // Add USB power delivery unit
  st = DdkAddComposite("fusb302", &fusb302_desc);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite for fusb302 failed, st = %d", __FUNCTION__, st);
    return st;
  }

  return ZX_OK;
}

}  // namespace vim3
