// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_LIB_HID_INPUT_REPORT_DEVICE_H_
#define SRC_UI_INPUT_LIB_HID_INPUT_REPORT_DEVICE_H_

#include <stddef.h>
#include <stdint.h>

#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>

#include "src/ui/input/lib/hid-input-report/axis.h"

namespace hid_input_report {

enum class ParseResult : uint32_t {
  kOk = 0,
  kNoMemory = 1,
  kTooManyItems = 2,
  kReportSizeMismatch = 3,
  kNoCollection = 4,
  kBadReport = 5,
  kNotImplemented = 6,
  kItemNotFound = 7,
};

enum class DeviceType : uint32_t {
  kMouse = 1,
  kSensor = 2,
  kTouch = 3,
  kKeyboard = 4,
  kConsumerControl = 5,
};

template <typename T>
fidl::ObjectView<T> Extract(const uint8_t* data, size_t len, hid::Attributes attr,
                            fidl::AnyArena& allocator) {
  double value;
  if (!hid::ExtractAsUnitType(data, len, attr, &value)) {
    return fidl::ObjectView<T>();
  }
  return fidl::ObjectView<T>(allocator, value);
}

class Device {
 public:
  virtual ~Device() = default;

  virtual ParseResult ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) = 0;

  ParseResult SetOutputReport(const fuchsia_input_report::wire::OutputReport* report, uint8_t* data,
                              size_t data_size, size_t* data_out_size) {
    return OutputReportId().has_value()
               ? SetOutputReportInternal(report, data, data_size, data_out_size)
               : ParseResult::kNotImplemented;
  }

  virtual ParseResult CreateDescriptor(fidl::AnyArena& allocator,
                                       fuchsia_input_report::wire::DeviceDescriptor& descriptor) {
    return ParseResult::kNotImplemented;
  }

  ParseResult ParseFeatureReport(const uint8_t* data, size_t len, fidl::AnyArena& allocator,
                                 fuchsia_input_report::wire::FeatureReport& feature_report) {
    return FeatureReportId().has_value()
               ? ParseFeatureReportInternal(data, len, allocator, feature_report)
               : ParseResult::kNotImplemented;
  }

  ParseResult SetFeatureReport(const fuchsia_input_report::wire::FeatureReport* report,
                               uint8_t* data, size_t data_size, size_t* data_out_size) {
    return FeatureReportId().has_value()
               ? SetFeatureReportInternal(report, data, data_size, data_out_size)
               : ParseResult::kNotImplemented;
  }

  ParseResult ParseInputReport(const uint8_t* data, size_t len, fidl::AnyArena& allocator,
                               fuchsia_input_report::wire::InputReport& input_report) {
    return InputReportId().has_value()
               ? ParseInputReportInternal(data, len, allocator, input_report)
               : ParseResult::kNotImplemented;
  }

  virtual std::optional<uint8_t> InputReportId() const { return std::nullopt; }
  virtual std::optional<uint8_t> OutputReportId() const { return std::nullopt; }
  virtual std::optional<uint8_t> FeatureReportId() const { return std::nullopt; }

  virtual DeviceType GetDeviceType() const = 0;

 private:
  virtual ParseResult SetOutputReportInternal(
      const fuchsia_input_report::wire::OutputReport* report, uint8_t* data, size_t data_size,
      size_t* data_out_size) {
    return ParseResult::kNotImplemented;
  }

  virtual ParseResult ParseFeatureReportInternal(
      const uint8_t* data, size_t len, fidl::AnyArena& allocator,
      fuchsia_input_report::wire::FeatureReport& feature_report) {
    return ParseResult::kNotImplemented;
  }

  virtual ParseResult SetFeatureReportInternal(
      const fuchsia_input_report::wire::FeatureReport* report, uint8_t* data, size_t data_size,
      size_t* data_out_size) {
    return ParseResult::kNotImplemented;
  }

  virtual ParseResult ParseInputReportInternal(
      const uint8_t* data, size_t len, fidl::AnyArena& allocator,
      fuchsia_input_report::wire::InputReport& input_report) {
    return ParseResult::kNotImplemented;
  }
};

// Create `out_device` from a HID descriptor. `out_device` is returned fully formed,
// it does not need any additional initialization.
ParseResult CreateDevice(const hid::ReportDescriptor* descriptor,
                         std::unique_ptr<Device>* out_device);

}  // namespace hid_input_report

#endif  // SRC_UI_INPUT_LIB_HID_INPUT_REPORT_DEVICE_H_
