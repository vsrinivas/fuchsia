// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_POWER_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_POWER_H_

#include <stdint.h>

#include <lib/ddk/metadata.h>

// Note: The voltage table must be sorted in descending order.
#define DEVICE_METADATA_AML_VOLTAGE_TABLE (0x41565400 | (DEVICE_METADATA_PRIVATE))  // AVTp
#define DEVICE_METADATA_AML_PWM_PERIOD_NS (0x41505000 | (DEVICE_METADATA_PRIVATE))  // APPp

typedef struct aml_voltage_table {
  uint32_t microvolt;
  uint32_t duty_cycle;
} aml_voltage_table_t;

using voltage_pwm_period_ns_t = uint32_t;

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_POWER_H_
