// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_PARSER_H_
#define SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_PARSER_H_

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace goldfish::sensor {

// Numeric class is a `variant' type supporting storing both int64_t
// (timestamp) and double (sensor reading) types.
class Numeric {
 public:
  explicit Numeric(double d) : val_(d) {}
  explicit Numeric(int64_t i) : val_(i) {}

  bool IsDouble() const { return std::holds_alternative<double>(val_); }
  bool IsInt() const { return std::holds_alternative<int64_t>(val_); }

  double Double() const {
    if (auto pint = std::get_if<double>(&val_)) {
      return *pint;
    }
    // May lose precision
    return static_cast<double>(std::get<int64_t>(val_));
  }

  int64_t Int() const {
    if (auto pint = std::get_if<int64_t>(&val_)) {
      return *pint;
    }
    // May lose precision
    return static_cast<int64_t>(std::get<double>(val_));
  }

 private:
  std::variant<int64_t, double> val_;
};

struct SensorReport {
  using DataType = std::variant<std::string, Numeric>;

  std::string name;
  std::vector<DataType> data;
};

// Parse raw sensor device input with format "name:<field1>:<field2>:..." to a
// |SensorReport| struct.
//
// Arguments:
//   |data|: Sensor device input string.
//   |size|: Maximum input size. If [data, data + size) doesn't contain '\0',
//           the input size will be this value; otherwise it will be
//           |strlen(data)|.
//   |max_fields|: Maximum number of fields to be parsed. If |max_fields| is 0,
//           all fields will be parsed. Default is 0 (unlimited).
//   |delimiter|: Delimiter between fields. Default is ':'.
SensorReport ParseSensorReport(const char* data, size_t size, size_t max_fields = 0u,
                               char delimiter = ':');

}  // namespace goldfish::sensor

#endif  // SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_PARSER_H_
