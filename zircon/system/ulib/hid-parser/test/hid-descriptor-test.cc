// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid-parser/descriptor.h>
#include <hid/boot.h>
#include <hid/paradise.h>
#include <zxtest/zxtest.h>

TEST(HidDescriptorTest, GetReportsSizeWithIds) {
  size_t len;
  const uint8_t* report_desc = get_paradise_touch_report_desc(&len);

  hid::DeviceDescriptor* desc = nullptr;
  hid::ParseResult res = hid::ParseReportDescriptor(report_desc, len, &desc);
  ASSERT_EQ(res, hid::ParseResult::kParseOk);

  size_t size =
      GetReportSizeFromFirstByte(*desc, hid::ReportType::kReportInput, PARADISE_RPT_ID_STYLUS);
  ASSERT_EQ(size, sizeof(paradise_stylus_t));

  size = GetReportSizeFromFirstByte(*desc, hid::ReportType::kReportInput, PARADISE_RPT_ID_TOUCH);
  ASSERT_EQ(size, sizeof(paradise_touch_t));

  FreeDeviceDescriptor(desc);
}

TEST(HidDescriptorTest, GetReportsSizeNoId) {
  size_t len;
  const uint8_t* report_desc = get_boot_mouse_report_desc(&len);

  hid::DeviceDescriptor* desc = nullptr;
  hid::ParseResult res = hid::ParseReportDescriptor(report_desc, len, &desc);
  ASSERT_EQ(res, hid::ParseResult::kParseOk);

  // First byte doesn't matter since there's only one report.
  size_t size = GetReportSizeFromFirstByte(*desc, hid::ReportType::kReportInput, 0xAB);
  ASSERT_EQ(size, sizeof(hid_boot_mouse_report_t));

  FreeDeviceDescriptor(desc);
}
