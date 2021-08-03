// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/sensor.h"

#include <stdint.h>
#include <zircon/compiler.h>

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include "src/ui/input/lib/hid-input-report/axis.h"
#include "src/ui/input/lib/hid-input-report/device.h"

namespace hid_input_report {

ParseResult Sensor::ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) {
  hid::Attributes values[fuchsia_input_report::wire::kSensorMaxValues] = {};
  size_t num_values = 0;

  for (size_t i = 0; i < hid_report_descriptor.input_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.input_fields[i];

    if (field.attr.usage.page != static_cast<uint32_t>(hid::usage::Page::kSensor)) {
      continue;
    }

    fuchsia_input_report::wire::SensorType type;
    zx_status_t status = HidSensorUsageToLlcppSensorType(
        static_cast<hid::usage::Sensor>(field.attr.usage.usage), &type);
    if (status != ZX_OK) {
      continue;
    }

    if (num_values == fuchsia_input_report::wire::kSensorMaxValues) {
      return ParseResult::kTooManyItems;
    }
    values[num_values] = field.attr;
    num_values++;
  }

  // No error, write to class members.
  num_values_ = num_values;
  for (size_t i = 0; i < num_values; i++) {
    values_[i] = values[i];
  }

  report_size_ = hid_report_descriptor.input_byte_sz;
  report_id_ = hid_report_descriptor.report_id;

  return ParseResult::kOk;
}

ParseResult Sensor::CreateDescriptor(fidl::AnyArena& allocator,
                                     fuchsia_input_report::wire::DeviceDescriptor& descriptor) {
  fuchsia_input_report::wire::SensorInputDescriptor input(allocator);

  // Set the values array.
  {
    fidl::VectorView<fuchsia_input_report::wire::SensorAxis> values(allocator, num_values_);
    for (size_t i = 0; i < num_values_; i++) {
      if (HidSensorUsageToLlcppSensorType(static_cast<hid::usage::Sensor>(values_[i].usage.usage),
                                          &values[i].type) != ZX_OK) {
        return ParseResult::kItemNotFound;
      }
      values[i].axis = LlcppAxisFromAttribute(values_[i]);
    }
    input.set_values(allocator, std::move(values));
  }

  fuchsia_input_report::wire::SensorDescriptor sensor(allocator);
  sensor.set_input(allocator, std::move(input));
  descriptor.set_sensor(allocator, std::move(sensor));

  return ParseResult::kOk;
}

ParseResult Sensor::ParseInputReport(const uint8_t* data, size_t len, fidl::AnyArena& allocator,
                                     fuchsia_input_report::wire::InputReport& input_report) {
  fuchsia_input_report::wire::SensorInputReport sensor_report(allocator);

  fidl::VectorView<int64_t> values(allocator, num_values_);
  for (size_t i = 0; i < num_values_; i++) {
    double value_out;
    if (hid::ExtractAsUnitType(data, len, values_[i], &value_out)) {
      values[i] = static_cast<int64_t>(value_out);
    }
  }

  sensor_report.set_values(allocator, std::move(values));

  input_report.set_sensor(allocator, std::move(sensor_report));
  return ParseResult::kOk;
}

}  // namespace hid_input_report
