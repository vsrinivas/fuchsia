// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/hid-input-report/mouse.h"

#include <variant>

#include <hid/boot.h>
#include <zxtest/zxtest.h>

#include "src/ui/lib/hid-input-report/device.h"

// Each test parses the report descriptor for the mouse and then sends one
// report to ensure that it has been parsed correctly.

TEST(MouseTest, BootMouse) {
  hid::DeviceDescriptor* dev_desc = nullptr;
  size_t boot_mouse_desc_size;
  const uint8_t* boot_mouse_desc = get_boot_mouse_report_desc(&boot_mouse_desc_size);
  auto parse_res = hid::ParseReportDescriptor(boot_mouse_desc, boot_mouse_desc_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  hid_input_report::Mouse mouse;

  EXPECT_EQ(hid_input_report::ParseResult::kParseOk,
            mouse.ParseReportDescriptor(dev_desc->report[0]));
  hid_input_report::ReportDescriptor report_descriptor = mouse.GetDescriptor();

  hid_input_report::MouseDescriptor* mouse_descriptor =
      std::get_if<hid_input_report::MouseDescriptor>(&report_descriptor.descriptor);
  ASSERT_NOT_NULL(mouse_descriptor);

  EXPECT_TRUE(mouse_descriptor->movement_x);
  EXPECT_TRUE(mouse_descriptor->movement_y);
  constexpr uint8_t kNumButtons = 3;
  EXPECT_EQ(kNumButtons, mouse_descriptor->num_buttons);

  EXPECT_EQ(0, mouse.ReportId());

  hid_boot_mouse_report_t report_data = {};
  const int kXTestVal = 10;
  const int kYTestVal = -5;
  report_data.rel_x = kXTestVal;
  report_data.rel_y = kYTestVal;
  report_data.buttons = 0xFF;

  hid_input_report::Report report = {};
  EXPECT_EQ(
      hid_input_report::ParseResult::kParseOk,
      mouse.ParseReport(reinterpret_cast<uint8_t*>(&report_data), sizeof(report_data), &report));

  hid_input_report::MouseReport* mouse_report =
      std::get_if<hid_input_report::MouseReport>(&report.report);
  ASSERT_NOT_NULL(mouse_report);
  EXPECT_TRUE(mouse_report->movement_x);
  EXPECT_EQ(kXTestVal, mouse_report->movement_x);

  EXPECT_TRUE(mouse_report->movement_x);
  EXPECT_EQ(kYTestVal, mouse_report->movement_y);

  EXPECT_EQ(kNumButtons, mouse_report->num_buttons_pressed);
  EXPECT_EQ(1, mouse_report->buttons_pressed[0]);
  EXPECT_EQ(2, mouse_report->buttons_pressed[1]);
  EXPECT_EQ(3, mouse_report->buttons_pressed[2]);
}
