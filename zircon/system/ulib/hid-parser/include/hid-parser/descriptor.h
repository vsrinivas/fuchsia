// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_PARSER_DESCRIPTOR_H_
#define HID_PARSER_DESCRIPTOR_H_

#include <stdint.h>

#include <hid-parser/parser.h>

namespace hid {

enum ReportType : uint32_t {
  kReportInput = 1,
  kReportOutput = 2,
  kReportFeature = 3,
};

// Gets the size of the report from the first byte of the report.
size_t GetReportSizeFromFirstByte(const DeviceDescriptor& desc, ReportType type, uint8_t byte);

}  // namespace hid

#endif  // HID_PARSER_DESCRIPTOR_H_
