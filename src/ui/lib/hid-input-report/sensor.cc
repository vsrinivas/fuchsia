// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/hid-input-report/sensor.h"

#include <stdint.h>
#include <zircon/compiler.h>

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>

#include "src/ui/lib/hid-input-report/axis.h"
#include "src/ui/lib/hid-input-report/device.h"

namespace hid_input_report {

ParseResult Sensor::ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) {
  hid::Attributes values[fuchsia_input_report::SENSOR_MAX_VALUES] = {};
  size_t num_values = 0;

  SensorDescriptor descriptor = {};

  for (size_t i = 0; i < hid_report_descriptor.input_count; i++) {
    const hid::ReportField& field = hid_report_descriptor.input_fields[i];

    if (field.attr.usage.page != static_cast<uint32_t>(hid::usage::Page::kSensor)) {
      continue;
    }

    llcpp::fuchsia::input::report::SensorType type;
    zx_status_t status = HidSensorUsageToLlcppSensorType(
        static_cast<hid::usage::Sensor>(field.attr.usage.usage), &type);
    if (status != ZX_OK) {
      continue;
    }

    if (num_values == fuchsia_input_report::SENSOR_MAX_VALUES) {
      return kParseTooManyItems;
    }
    values[num_values] = field.attr;

    descriptor.values[num_values].type = type;
    descriptor.values[num_values].axis = LlcppAxisFromAttribute(values[num_values]);
    num_values++;
  }

  // No error, write to class members.
  descriptor.num_values = num_values;
  descriptor_ = descriptor;
  num_values_ = num_values;
  for (size_t i = 0; i < num_values; i++) {
    values_[i] = values[i];
  }

  report_size_ = hid_report_descriptor.input_byte_sz;
  report_id_ = hid_report_descriptor.report_id;

  return kParseOk;
}

ReportDescriptor Sensor::GetDescriptor() {
  ReportDescriptor report_descriptor = {};
  report_descriptor.descriptor = descriptor_;
  return report_descriptor;
}

ParseResult Sensor::ParseReport(const uint8_t* data, size_t len, Report* report) {
  SensorReport sensor_report = {};
  if (len != report_size_) {
    return kParseReportSizeMismatch;
  }

  for (size_t i = 0; i < num_values_; i++) {
    double value_out;
    if (hid::ExtractAsUnitType(data, len, values_[i], &value_out)) {
      sensor_report.values[i] = static_cast<int64_t>(value_out);
    }
  }
  sensor_report.num_values = num_values_;

  // Now that we can't fail, set the real report.
  report->report = sensor_report;

  return kParseOk;
}

}  // namespace hid_input_report
