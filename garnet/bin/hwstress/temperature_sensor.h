// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_TEMPERATURE_SENSOR_H_
#define GARNET_BIN_HWSTRESS_TEMPERATURE_SENSOR_H_

#include <lib/zx/channel.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hwstress {

// A TemperatureSensor monitors the hardware's temperature.
class TemperatureSensor {
 public:
  virtual ~TemperatureSensor() = default;

  // Read the temperature of the system temperature sensor, in degrees celcius,
  // or nullopt if no value is available.
  //
  // Currently selects a single "sensible" sensor, because platforms of interest
  // only have one. We may want to expand this in future.
  virtual std::optional<double> ReadCelcius() = 0;
};

// Create a temperature sensor using devices on the system.
std::unique_ptr<TemperatureSensor> CreateSystemTemperatureSensor(std::string_view device_path);
std::unique_ptr<TemperatureSensor> CreateSystemTemperatureSensor(zx::channel channel);

// Create a null temperature sensor. Always returns "unknown".
std::unique_ptr<TemperatureSensor> CreateNullTemperatureSensor();

// A global, null temperature sensor.
TemperatureSensor* GetNullTemperatureSensor();

// Convert a temperature to a human-readable string.
std::string TemperatureToString(std::optional<double> temperature);

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_TEMPERATURE_SENSOR_H_
