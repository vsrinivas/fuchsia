// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-voltage.h"

#include <lib/device-protocol/pdev.h>
#include <string.h>
#include <unistd.h>

#include <memory>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

namespace thermal {

namespace {

// Sleep for 200 microseconds inorder to let the voltage change
// take effect. Source: Amlogic SDK.
constexpr uint32_t kSleep = 200;
// Step up or down 3 steps in the voltage table while changing
// voltage and not directly. Source: Amlogic SDK
constexpr int kSteps = 3;
// Invalid index in the voltage-table
constexpr int kInvalidIndex = -1;

}  // namespace

zx_status_t AmlVoltageRegulator::Create(
    zx_device_t* parent, const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config,
    const aml_thermal_info_t* thermal_info) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "aml-voltage: failed to get composite protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PDev pdev(composite);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "aml-voltage: failed to get pdev protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_device_info_t device_info;
  zx_status_t status = pdev.GetDeviceInfo(&device_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-voltage: failed to get GetDeviceInfo ");
    return status;
  }

  big_cluster_pwm_ = ddk::PwmProtocolClient(composite, "pwm-a");
  if (!big_cluster_pwm_.is_valid()) {
    zxlogf(ERROR, "%s: failed to get big cluster PWM fragment", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if ((status = big_cluster_pwm_.Enable()) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not enable PWM", __func__);
    return status;
  }

  big_little_ = thermal_config.big_little;
  if (big_little_) {
    little_cluster_pwm_ = ddk::PwmProtocolClient(composite, "pwm-ao-d");
    if (!little_cluster_pwm_.is_valid()) {
      zxlogf(ERROR, "%s: failed to get little cluster PWM fragment", __func__);
      return ZX_ERR_NOT_SUPPORTED;
    }
    if ((status = little_cluster_pwm_.Enable()) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not enable PWM", __func__);
      return status;
    }
  }

  return Init(thermal_config, thermal_info);
}

zx_status_t AmlVoltageRegulator::Init(
    const pwm_protocol_t* big_cluster_pwm, const pwm_protocol_t* little_cluster_pwm,
    const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config,
    const aml_thermal_info_t* thermal_info) {
  zx_status_t status = ZX_OK;
  big_little_ = thermal_config.big_little;

  big_cluster_pwm_ = ddk::PwmProtocolClient(big_cluster_pwm);
  if ((status = big_cluster_pwm_.Enable()) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not enable PWM", __func__);
    return status;
  }

  if (big_little_) {
    little_cluster_pwm_ = ddk::PwmProtocolClient(little_cluster_pwm);
    if ((status = little_cluster_pwm_.Enable()) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not enable PWM", __func__);
      return status;
    }
  }

  return Init(thermal_config, thermal_info);
}

zx_status_t AmlVoltageRegulator::Init(
    const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config,
    const aml_thermal_info_t* thermal_info) {
  ZX_DEBUG_ASSERT(thermal_info);
  zx_status_t status = ZX_OK;

  // Get the voltage-table metadata.
  memcpy(&thermal_info_, thermal_info, sizeof(aml_thermal_info_t));

  current_big_cluster_voltage_index_ = kInvalidIndex;
  current_little_cluster_voltage_index_ = kInvalidIndex;

  uint32_t max_big_cluster_microvolt = 0;
  uint32_t max_little_cluster_microvolt = 0;

  const fuchsia_hardware_thermal_OperatingPoint& big_cluster_opp =
      thermal_config.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN];
  const fuchsia_hardware_thermal_OperatingPoint& little_cluster_opp =
      thermal_config.opps[fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN];

  for (uint32_t i = 0; i < fuchsia_hardware_thermal_MAX_DVFS_OPPS; i++) {
    if (i < big_cluster_opp.count && big_cluster_opp.opp[i].volt_uv > max_big_cluster_microvolt) {
      max_big_cluster_microvolt = big_cluster_opp.opp[i].volt_uv;
    }
    if (i < little_cluster_opp.count &&
        little_cluster_opp.opp[i].volt_uv > max_little_cluster_microvolt) {
      max_little_cluster_microvolt = little_cluster_opp.opp[i].volt_uv;
    }
  }

  // Set the voltage to maximum to start with
  if ((status = SetBigClusterVoltage(max_big_cluster_microvolt)) != ZX_OK) {
    return status;
  }
  if (big_little_ && (status = SetLittleClusterVoltage(max_little_cluster_microvolt)) != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t AmlVoltageRegulator::SetClusterVoltage(int* current_voltage_index,
                                                   const ddk::PwmProtocolClient& pwm,
                                                   uint32_t microvolt) {
  // Find the entry in the voltage-table.
  int target_index;
  for (target_index = 0; target_index < MAX_VOLTAGE_TABLE; target_index++) {
    if (thermal_info_.voltage_table[target_index].microvolt == microvolt) {
      break;
    }
  }

  // Invalid voltage request.
  if (target_index == MAX_VOLTAGE_TABLE) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ZX_OK;
  // If this is the first time we are setting up the voltage
  // we directly set it.
  if (*current_voltage_index < 0) {
    // Update new duty cycle.
    aml_pwm::mode_config on = {aml_pwm::ON, {}};
    pwm_config_t cfg = {false, thermal_info_.voltage_pwm_period_ns,
                        static_cast<float>(thermal_info_.voltage_table[target_index].duty_cycle),
                        &on, sizeof(on)};
    if ((status = pwm.SetConfig(&cfg)) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not initialize PWM", __func__);
      return status;
    }
    usleep(kSleep);
    *current_voltage_index = target_index;
    return ZX_OK;
  }

  // Otherwise we adjust to the target voltage step by step.
  while (*current_voltage_index != target_index) {
    if (*current_voltage_index < target_index) {
      if (*current_voltage_index < target_index - kSteps) {
        // Step up by 3 in the voltage table.
        *current_voltage_index += kSteps;
      } else {
        *current_voltage_index = target_index;
      }
    } else {
      if (*current_voltage_index > target_index + kSteps) {
        // Step down by 3 in the voltage table.
        *current_voltage_index -= kSteps;
      } else {
        *current_voltage_index = target_index;
      }
    }
    // Update new duty cycle.
    aml_pwm::mode_config on = {aml_pwm::ON, {}};
    pwm_config_t cfg = {
        false, thermal_info_.voltage_pwm_period_ns,
        static_cast<float>(thermal_info_.voltage_table[*current_voltage_index].duty_cycle), &on,
        sizeof(on)};
    if ((status = pwm.SetConfig(&cfg)) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not initialize PWM", __func__);
      return status;
    }
    usleep(kSleep);
  }

  return ZX_OK;
}

}  // namespace thermal
