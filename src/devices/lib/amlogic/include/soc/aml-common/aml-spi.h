// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_SPI_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_SPI_H_

#define DEVICE_METADATA_AMLSPI_CONFIG 0x53435364  // 'SCSd'

#include <stdint.h>
#include <zircon/time.h>

typedef struct {
  // The capacity and period to use when setting the scheduler profile for the driver thread(s). No
  // profile will be set if either capacity_ns or period_ns is zero.
  zx_duration_t capacity;
  zx_duration_t period;
  uint32_t bus_id;
  uint32_t cs_count;
  uint32_t cs[4];
  // The clock divider register value (NOT the actual clock divider) to use for SCLK. Written to
  // ENHANCE_CNTL if use_enhanced_clock_mode is true, in which case the value must be in [0, 255].
  // Written to CONREG otherwise, in which case the value must be in [0, 7].
  uint32_t clock_divider_register_value;
  // If true, the SPI driver uses the enhanced clock mode instead of the regular clock mode.
  bool use_enhanced_clock_mode;
} amlspi_config_t;

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_SPI_H_
