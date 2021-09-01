// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/goldfish_sensor/parser.h"

#include <cerrno>

namespace goldfish::sensor {

namespace {

std::variant<std::string, Numeric> ParseField(const std::string& field) {
  // For numbers, in order to keep the precision as much as possible, we
  // always try storing the data as int64_t first, otherwise we store it as
  // double. Non-numbers are stored as string.
  char* p = nullptr;
  errno = 0;
  int64_t integer = strtol(field.c_str(), &p, /*base=*/0);
  if (p && *p == '\0' && errno == 0) {
    return Numeric(integer);
  }

  errno = 0;
  double num = strtod(field.c_str(), &p);
  if (p && *p == '\0' && errno == 0) {
    return Numeric(num);
  }

  return field;
}

}  // namespace

SensorReport ParseSensorReport(const char* data, size_t size, size_t max_fields, char delimiter) {
  SensorReport result;

  std::string curr;
  size_t parsed_fields = 0;

  for (size_t i = 0; i <= size; i++) {
    if (i < size && data[i] == '\0') {
      size = i - 1;
    }
    if (i < size && data[i] != delimiter) {
      curr += data[i];
      continue;
    }

    if (result.name.empty()) {
      result.name = std::move(curr);
    } else {
      result.data.push_back(ParseField(curr));
    }
    curr.clear();

    if (max_fields > 0 && ++parsed_fields >= max_fields) {
      break;
    }
  }
  return result;
}

}  // namespace goldfish::sensor
