// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_HID_INPUT_REPORT_DEVICE_H_
#define SRC_UI_LIB_HID_INPUT_REPORT_DEVICE_H_

#include <stddef.h>
#include <stdint.h>

#include <hid-parser/parser.h>
#include <hid-parser/units.h>

#include "src/ui/lib/hid-input-report/descriptors.h"

namespace hid_input_report {

enum ParseResult : uint32_t {
  kParseOk = 0,
  kParseNoMemory = 1,
  kParseTooManyItems = 2,
  kParseReportSizeMismatch = 3,
  kParseNoCollection = 4,
  kParseBadReport = 5,
  kParseNotImplemented = 6,
  kParseItemNotFound = 7,
};

class Device {
 public:
  virtual ~Device() = default;

  virtual ParseResult ParseReportDescriptor(const hid::ReportDescriptor& hid_report_descriptor) = 0;
  virtual ReportDescriptor GetDescriptor() = 0;

  virtual ParseResult ParseInputReport(const uint8_t* data, size_t len, InputReport* report) = 0;

  virtual ParseResult SetOutputReport(const fuchsia_input_report::OutputReport* report,
                                      uint8_t* data, size_t data_size, size_t* data_out_size) {
    return kParseNotImplemented;
  }

  virtual uint8_t InputReportId() const = 0;
};

}  // namespace hid_input_report

#endif  // SRC_UI_LIB_HID_INPUT_REPORT_DEVICE_H_
