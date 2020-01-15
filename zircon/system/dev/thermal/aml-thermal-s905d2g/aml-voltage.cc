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
// Init period
constexpr int kPwmPeriodNs = 1250;

}  // namespace

zx_status_t AmlVoltageRegulator::Create(zx_device_t* parent,
                                        aml_thermal_info_t* thermal_info) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "aml-voltage: failed to get composite protocol\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // zeroth component is pdev
  size_t actual;
  zx_device_t* components[COMPONENT_COUNT];
  composite.GetComponents(components, countof(components), &actual);
  if (actual < 1) {
    zxlogf(ERROR, "%s: failed to get pdev component\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PDev pdev(components[COMPONENT_PDEV]);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "aml-voltage: failed to get pdev protocol\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_device_info_t device_info;
  zx_status_t status = pdev.GetDeviceInfo(&device_info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-voltage: failed to get GetDeviceInfo \n");
    return status;
  }

  pwm_AO_D_ = ddk::PwmProtocolClient(components[COMPONENT_PWM_AO_D]);
  if (!pwm_AO_D_.is_valid()) {
    zxlogf(ERROR, "%s: failed to get PWM_AO_D component\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if ((status = pwm_AO_D_.Enable()) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not enable PWM\n", __func__);
    return status;
  }

  pid_ = device_info.pid;
  switch (pid_) {
    {
      case PDEV_PID_AMLOGIC_T931: {
        // Sherlock
        pwm_A_ = ddk::PwmProtocolClient(components[COMPONENT_PWM_A]);
        if (!pwm_A_.is_valid()) {
          zxlogf(ERROR, "%s: failed to get PWM_A component\n", __func__);
          return ZX_ERR_NOT_SUPPORTED;
        }
        if ((status = pwm_A_.Enable()) != ZX_OK) {
          zxlogf(ERROR, "%s: Could not enable PWM\n", __func__);
          return status;
        }
        break;
      }
      case PDEV_PID_AMLOGIC_S905D2: {
        // Astro
        // Only 1 PWM used in this case.
        break;
      }
      default:
        zxlogf(ERROR, "aml-cpufreq: unsupported SOC PID %u\n", device_info.pid);
        return ZX_ERR_INVALID_ARGS;
    }
  }

  return Init(thermal_info);
}

zx_status_t AmlVoltageRegulator::Init(const pwm_protocol_t* pwm_AO_D, const pwm_protocol_t* pwm_A,
                                      uint32_t pid, aml_thermal_info_t* thermal_info) {
  zx_status_t status = ZX_OK;
  pid_ = pid;
  pwm_AO_D_ = ddk::PwmProtocolClient(pwm_AO_D);
  if ((status = pwm_AO_D_.Enable()) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not enable PWM\n", __func__);
    return status;
  }

  switch (pid) {
    case PDEV_PID_AMLOGIC_T931: {
      // Sherlock
      pwm_A_ = ddk::PwmProtocolClient(pwm_A);
      if ((status = pwm_A_.Enable()) != ZX_OK) {
        zxlogf(ERROR, "%s: Could not enable PWM\n", __func__);
        return status;
      }
      break;
    }
    case PDEV_PID_AMLOGIC_S905D2: {
      // Astro
      // Only 1 PWM used in this case.
      break;
    }
    default:
      zxlogf(ERROR, "aml-voltage-test: unsupported SOC PID %u\n", pid);
      return ZX_ERR_INVALID_ARGS;
  }

  return Init(thermal_info);
}

zx_status_t AmlVoltageRegulator::Init(aml_thermal_info_t* thermal_info) {
  ZX_DEBUG_ASSERT(thermal_info);
  zx_status_t status = ZX_OK;

  // Get the voltage-table metadata.
  memcpy(&thermal_info_, thermal_info, sizeof(aml_thermal_info_t));

  current_big_cluster_voltage_index_ = kInvalidIndex;
  current_little_cluster_voltage_index_ = kInvalidIndex;

  // Set the voltage to maximum to start with
  // TODO(braval):  Figure out a better way to set initial voltage.
  switch (pid_) {
    case PDEV_PID_AMLOGIC_T931: {
      status = SetBigClusterVoltage(thermal_info->voltage_table[13].microvolt);
      if (status != ZX_OK) {
        return status;
      }
      status = SetLittleClusterVoltage(thermal_info->voltage_table[1].microvolt);
      if (status != ZX_OK) {
        return status;
      }
      break;
    }

    case PDEV_PID_AMLOGIC_S905D2: {
      status = SetBigClusterVoltage(thermal_info->voltage_table[4].microvolt);
      if (status != ZX_OK) {
        return status;
      }
      break;
    }

    default:
      zxlogf(ERROR, "aml-voltage: unsupported SOC PID %u\n", pid_);
      return ZX_ERR_INVALID_ARGS;
  }
  return status;
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
    pwm_config_t cfg = {
        false, kPwmPeriodNs,
        static_cast<float>(thermal_info_.voltage_table[target_index].duty_cycle), &on,
        sizeof(on)};
    if ((status = pwm.SetConfig(&cfg)) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not initialize PWM\n", __func__);
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
        false, kPwmPeriodNs,
        static_cast<float>(thermal_info_.voltage_table[*current_voltage_index].duty_cycle),
        &on, sizeof(on)};
    if ((status = pwm.SetConfig(&cfg)) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not initialize PWM\n", __func__);
      return status;
    }
    usleep(kSleep);
  }

  return ZX_OK;
}

uint32_t AmlVoltageRegulator::GetBigClusterVoltage() {
  ZX_DEBUG_ASSERT(current_big_cluster_voltage_index_ != kInvalidIndex);
  return thermal_info_.voltage_table[current_big_cluster_voltage_index_].microvolt;
}

uint32_t AmlVoltageRegulator::GetLittleClusterVoltage() {
  ZX_DEBUG_ASSERT(current_little_cluster_voltage_index_ != kInvalidIndex);
  return thermal_info_.voltage_table[current_little_cluster_voltage_index_].microvolt;
}

}  // namespace thermal
