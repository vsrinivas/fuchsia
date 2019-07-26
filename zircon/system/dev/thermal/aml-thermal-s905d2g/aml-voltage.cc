// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-voltage.h"
#include <ddk/debug.h>
#include <unistd.h>
#include <string.h>

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

zx_status_t AmlVoltageRegulator::Init(zx_device_t* parent, aml_opp_info_t* opp_info) {
  ZX_DEBUG_ASSERT(opp_info);

  // Create a PWM period = 1250, hwpwm - 1 to signify using PWM_D from PWM_C/D.
  // Source: Amlogic SDK.
  fbl::AllocChecker ac;
  pwm_ = fbl::make_unique_checked<AmlPwm>(&ac, 1250, 1);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  // Initialize the PWM.
  zx_status_t status = pwm_->Init(parent);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-voltage: Could not initialize PWM: %d\n", status);
    return status;
  }

  // Get the voltage-table metadata.
  memcpy(&opp_info_, opp_info, sizeof(aml_opp_info_t));

  current_voltage_index_ = kInvalidIndex;

  // Set the voltage to maximum to start with
  // TODO(braval):  Figure out a better way to set initialize
  //.               voltage.
  status = SetVoltage(981000);
  return ZX_OK;
}

zx_status_t AmlVoltageRegulator::SetVoltage(uint32_t microvolt) {
  // Find the entry in the voltage-table.
  int target_index;
  for (target_index = 0; target_index < MAX_VOLTAGE_TABLE; target_index++) {
    if (opp_info_.voltage_table[target_index].microvolt == microvolt) {
      break;
    }
  }

  // Invalid voltage request.
  if (target_index == MAX_VOLTAGE_TABLE) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status;
  // If this is the first time we are setting up the voltage
  // we directly set it.
  if (current_voltage_index_ < 0) {
    // Update new duty cycle.
    status = pwm_->Configure(opp_info_.voltage_table[target_index].duty_cycle);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-voltage: Failed to set bew duty_cycle %d\n", status);
      return status;
    }
    usleep(kSleep);
    current_voltage_index_ = target_index;
    return ZX_OK;
  }

  // Otherwise we adjust to the target voltage step by step.
  while (current_voltage_index_ != target_index) {
    if (current_voltage_index_ < target_index) {
      if (current_voltage_index_ < target_index - kSteps) {
        // Step up by 3 in the voltage table.
        current_voltage_index_ += kSteps;
      } else {
        current_voltage_index_ = target_index;
      }
    } else {
      if (current_voltage_index_ > target_index + kSteps) {
        // Step down by 3 in the voltage table.
        current_voltage_index_ -= kSteps;
      } else {
        current_voltage_index_ = target_index;
      }
    }
    // Update new duty cycle.
    status = pwm_->Configure(opp_info_.voltage_table[target_index].duty_cycle);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-voltage: Failed to set bew duty_cycle %d\n", status);
      return status;
    }
    usleep(kSleep);
  }

  // Update the current voltage index.
  current_voltage_index_ = target_index;
  return ZX_OK;
}

uint32_t AmlVoltageRegulator::GetVoltage() {
  ZX_DEBUG_ASSERT(current_voltage_index_ != kInvalidIndex);
  return opp_info_.voltage_table[current_voltage_index_].microvolt;
}
}  // namespace thermal
