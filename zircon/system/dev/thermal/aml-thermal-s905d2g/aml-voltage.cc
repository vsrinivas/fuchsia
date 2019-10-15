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

#include "aml-pwm.h"

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
constexpr int kPeriod = 1250;

}  // namespace

zx_status_t AmlVoltageRegulator::Create(zx_device_t* parent,
                                        aml_voltage_table_info_t* voltage_table_info) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "aml-voltage: failed to get composite protocol\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // zeroth component is pdev
  size_t actual;
  zx_device_t* component = nullptr;
  composite.GetComponents(&component, 1, &actual);
  if (actual != 1) {
    zxlogf(ERROR, "%s: failed to get pdev component\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PDev pdev(component);
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

  // Create a PWM period = 1250, hwpwm - 1 to signify using PWM_D from PWM_C/D.
  // Source: Amlogic SDK.
  fbl::AllocChecker ac;
  pwm_AO_D_ = fbl::make_unique_checked<AmlPwm>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  status = pwm_AO_D_->Create(component, AmlPwm::PWM_AO_CD);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-voltage: Could not initialize PWM PWM_AO_CD: %d\n", status);
    return status;
  }

  pid_ = device_info.pid;
  switch (pid_) {
    {
      case PDEV_PID_AMLOGIC_T931: {
        // Sherlock
        // Create a PWM period = 1250, hwpwm - 0 to signify using PWM_A from PWM_A/B.
        // Source: Amlogic SDK.
        fbl::AllocChecker ac;
        pwm_A_ = fbl::make_unique_checked<AmlPwm>(&ac);
        if (!ac.check()) {
          return ZX_ERR_NO_MEMORY;
        }
        zx_status_t status = pwm_A_->Create(component, AmlPwm::PWM_AB);
        if (status != ZX_OK) {
          zxlogf(ERROR, "aml-voltage: Could not initialize pwm_A_ PWM: %d\n", status);
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

  return Init(voltage_table_info);
}

zx_status_t AmlVoltageRegulator::Init(ddk::MmioBuffer pwm_AO_D_mmio, ddk::MmioBuffer pwm_A_mmio,
                                      uint32_t pid, aml_voltage_table_info_t* voltage_table_info) {
  pid_ = pid;

  // Create a PWM period = 1250, hwpwm - 1 to signify using PWM_D from PWM_C/D.
  // Source: Amlogic SDK.
  fbl::AllocChecker ac;
  pwm_AO_D_ = fbl::make_unique_checked<AmlPwm>(&ac, std::move(pwm_AO_D_mmio));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  switch (pid) {
    case PDEV_PID_AMLOGIC_T931: {
      // Sherlock
      // Create a PWM period = 1250, hwpwm - 0 to signify using PWM_A from PWM_A/B.
      // Source: Amlogic SDK.
      fbl::AllocChecker ac;
      pwm_A_ = fbl::make_unique_checked<AmlPwm>(&ac, std::move(pwm_A_mmio));
      if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
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

  return Init(voltage_table_info);
}

zx_status_t AmlVoltageRegulator::Init(aml_voltage_table_info_t* voltage_table_info) {
  ZX_DEBUG_ASSERT(voltage_table_info);

  // Initialize the PWM.
  zx_status_t status = pwm_AO_D_->Init(kPeriod, 1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-voltage: Could not initialize PWM PWM_AO_CD: %d\n", status);
    return status;
  }

  if (pid_ == PDEV_PID_AMLOGIC_T931) {
    // Initialize the PWM.
    zx_status_t status = pwm_A_->Init(kPeriod, 0);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-voltage: Could not initialize pwm_A_ PWM: %d\n", status);
      return status;
    }
  }

  // Get the voltage-table metadata.
  memcpy(&voltage_table_info_, voltage_table_info, sizeof(aml_voltage_table_info_t));

  current_big_cluster_voltage_index_ = kInvalidIndex;
  current_little_cluster_voltage_index_ = kInvalidIndex;

  // Set the voltage to maximum to start with
  // TODO(braval):  Figure out a better way to set initial voltage.
  switch (pid_) {
    case PDEV_PID_AMLOGIC_T931: {
      status = SetBigClusterVoltage(voltage_table_info->voltage_table[13].microvolt);
      if (status != ZX_OK) {
        return status;
      }
      status = SetLittleClusterVoltage(voltage_table_info->voltage_table[1].microvolt);
      if (status != ZX_OK) {
        return status;
      }
      break;
    }

    case PDEV_PID_AMLOGIC_S905D2: {
      status = SetBigClusterVoltage(voltage_table_info->voltage_table[4].microvolt);
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
                                                   std::unique_ptr<thermal::AmlPwm>* pwm,
                                                   uint32_t microvolt) {
  // Find the entry in the voltage-table.
  int target_index;
  for (target_index = 0; target_index < MAX_VOLTAGE_TABLE; target_index++) {
    if (voltage_table_info_.voltage_table[target_index].microvolt == microvolt) {
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
    status = (*pwm)->Configure(voltage_table_info_.voltage_table[target_index].duty_cycle);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-voltage: Failed to set new duty_cycle %d\n", status);
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
    status =
        (*pwm)->Configure(voltage_table_info_.voltage_table[*current_voltage_index].duty_cycle);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-voltage: Failed to set new duty_cycle %d\n", status);
      return status;
    }
    usleep(kSleep);
  }

  return ZX_OK;
}

uint32_t AmlVoltageRegulator::GetBigClusterVoltage() {
  ZX_DEBUG_ASSERT(current_big_cluster_voltage_index_ != kInvalidIndex);
  return voltage_table_info_.voltage_table[current_big_cluster_voltage_index_].microvolt;
}

uint32_t AmlVoltageRegulator::GetLittleClusterVoltage() {
  ZX_DEBUG_ASSERT(current_little_cluster_voltage_index_ != kInvalidIndex);
  return voltage_table_info_.voltage_table[current_little_cluster_voltage_index_].microvolt;
}

}  // namespace thermal
