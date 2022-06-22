// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_I2C_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_I2C_H_

#include <stdint.h>

// One struct must be present for each bus managed by this driver. Default
// register values are preserved if delay values are set to zero.
struct aml_i2c_delay_values {
  uint16_t quarter_clock_delay;
  uint16_t clock_low_delay;
};

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_I2C_H_
