// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_THERMAL_H_
#define ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_THERMAL_H_

#define MAX_VOLTAGE_TABLE 31

typedef struct {
  uint32_t microvolt;
  uint32_t duty_cycle;
} aml_voltage_table_t;

typedef struct {
  aml_voltage_table_t voltage_table[MAX_VOLTAGE_TABLE];
} aml_thermal_info_t;

#endif  // ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_THERMAL_H_
