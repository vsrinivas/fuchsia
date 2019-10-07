// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid-parser/descriptor.h>

namespace hid {

// Gets the size of the report from the first byte of the report.
size_t GetReportSizeFromFirstByte(const DeviceDescriptor& desc, ReportType type, uint8_t byte) {
  for (size_t i = 0; i < desc.rep_count; i++) {
    const ReportDescriptor& report = desc.report[i];
    if ((report.report_id == byte) || (report.report_id == 0)) {
      switch (type) {
        case ReportType::kReportInput:
          return report.input_byte_sz;
        case ReportType::kReportOutput:
          return report.output_byte_sz;
        case ReportType::kReportFeature:
          return report.feature_byte_sz;
      }
    }
  }
  return 0;
}

}  // namespace hid
