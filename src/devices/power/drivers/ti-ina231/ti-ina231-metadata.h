// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_TI_INA231_TI_INA231_METADATA_H_
#define SRC_DEVICES_POWER_DRIVERS_TI_INA231_TI_INA231_METADATA_H_

#include <stdint.h>

namespace power_sensor {

struct Ina231Metadata {
  enum Mode : uint16_t {
    kModeShuntAndBusContinuous = 0b111,
  };

  enum ConversionTime : uint16_t {
    kConversionTime332us = 0b010,
  };

  enum Averages : uint16_t {
    kAverages1024 = 0b111,
  };

  enum Alert : uint16_t {
    kAlertNone = 0,
    kAlertBusUnderVoltage = 1 << 12,
  };

  uint16_t mode;
  uint16_t shunt_voltage_conversion_time;
  uint16_t bus_voltage_conversion_time;
  uint16_t averages;

  // May not be zero.
  uint64_t shunt_resistance_microohm;
  // Resolution is 1250 uV regardless of other settings. Only used if alert is not kAlertNone.
  uint64_t bus_voltage_limit_microvolt;
  uint16_t alert;
};

}  // namespace power_sensor

#endif  // SRC_DEVICES_POWER_DRIVERS_TI_INA231_TI_INA231_METADATA_H_
