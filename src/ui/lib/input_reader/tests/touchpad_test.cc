// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/paradise.h>

#include "src/ui/lib/input_reader/touch.h"

namespace input {

namespace {

void ParseTouchpad(const uint8_t *desc, size_t desc_len, ui_input::Touch *ts) {
  hid::DeviceDescriptor *dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(desc, desc_len, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  auto count = dev_desc->rep_count;
  ASSERT_LT(0UL, count);

  // Find the first input report.
  const hid::ReportDescriptor *input_desc = nullptr;
  for (size_t rep = 0; rep < count; rep++) {
    const hid::ReportDescriptor *desc = &dev_desc->report[rep];
    if (desc->input_count != 0) {
      input_desc = desc;
      break;
    }
  }
  ASSERT_NE(nullptr, input_desc);
  ASSERT_LT(0UL, input_desc->input_count);

  auto success = ts->ParseTouchDescriptor(*input_desc);
  ASSERT_EQ(true, success);
}
}  // namespace

// These unit tests exercise any touchpad report descriptors we've collected.
// Each test parses the report descriptor for the touchpad and then sends one
// report to ensure that it has been parsed correctly.
namespace test {

TEST(TouchpadTest, ParadiseV1) {
  ui_input::Touch ts;
  size_t desc_size;
  const uint8_t *paradise_touchpad_v1_report_desc =
      get_paradise_touchpad_v1_report_desc(&desc_size);

  ParseTouchpad(paradise_touchpad_v1_report_desc, desc_size, &ts);
  ui_input::Touch::Descriptor ts_desc;
  EXPECT_TRUE(ts.SetDescriptor(&ts_desc));

  EXPECT_EQ(5UL, ts.touch_points());
  EXPECT_EQ(ui_input::Touch::Capabilities::CONTACT_ID |
                ui_input::Touch::Capabilities::CONTACT_COUNT |
                ui_input::Touch::Capabilities::BUTTON | ui_input::Touch::Capabilities::TIP_SWITCH |
                ui_input::Touch::Capabilities::X | ui_input::Touch::Capabilities::Y,
            ts.capabilities());
  EXPECT_EQ(0, ts_desc.x_min);
  EXPECT_EQ(1030000, ts_desc.x_max);
  EXPECT_EQ(0, ts_desc.y_min);
  EXPECT_EQ(680000, ts_desc.y_max);

  // Now use the parsed descriptor to interpret a touchpad report.
  paradise_touchpad_v1_t touchpad_v1_report = {};
  touchpad_v1_report.report_id = 12;
  touchpad_v1_report.fingers[1].tip_switch = 0x1;
  touchpad_v1_report.fingers[1].id = 0x1;
  touchpad_v1_report.fingers[1].x = 100;
  touchpad_v1_report.fingers[1].y = 200;

  touchpad_v1_report.fingers[2].tip_switch = 0x1;
  touchpad_v1_report.fingers[2].id = 0x2;
  touchpad_v1_report.fingers[2].x = 300;
  touchpad_v1_report.fingers[2].y = 400;

  uint8_t *report_data = reinterpret_cast<uint8_t *>(&touchpad_v1_report);

  ui_input::Touch::Report report;
  auto success = ts.ParseReport(report_data, sizeof(touchpad_v1_report), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(2UL, report.contact_count);

  // The X and Y values below have been manually calculated based on the above
  // input and the logical/physical conversion given by the report descriptor.
  EXPECT_EQ(1U, report.contacts[0].id);
  EXPECT_EQ(7812, report.contacts[0].x);
  EXPECT_EQ(15625, report.contacts[0].y);

  EXPECT_EQ(2U, report.contacts[1].id);
  EXPECT_EQ(23437, report.contacts[1].x);
  EXPECT_EQ(31250, report.contacts[1].y);
}

TEST(TouchpadTest, ParadiseV2) {
  ui_input::Touch ts;
  size_t desc_size;
  const uint8_t *paradise_touchpad_v2_report_desc =
      get_paradise_touchpad_v2_report_desc(&desc_size);

  ParseTouchpad(paradise_touchpad_v2_report_desc, desc_size, &ts);
  ui_input::Touch::Descriptor ts_desc;
  EXPECT_TRUE(ts.SetDescriptor(&ts_desc));

  EXPECT_EQ(5UL, ts.touch_points());
  EXPECT_EQ(ui_input::Touch::Capabilities::CONTACT_ID |
                ui_input::Touch::Capabilities::CONTACT_COUNT |
                ui_input::Touch::Capabilities::BUTTON | ui_input::Touch::Capabilities::TIP_SWITCH |
                ui_input::Touch::Capabilities::X | ui_input::Touch::Capabilities::Y,
            ts.capabilities());
  EXPECT_EQ(0, ts_desc.x_min);
  EXPECT_EQ(1030000, ts_desc.x_max);
  EXPECT_EQ(0, ts_desc.y_min);
  EXPECT_EQ(680000, ts_desc.y_max);

  // Now use the parsed descriptor to interpret a touchpad report.
  paradise_touchpad_v2_t touchpad_v2_report = {};
  touchpad_v2_report.report_id = 12;
  touchpad_v2_report.fingers[1].tip_switch = 0x1;
  touchpad_v2_report.fingers[1].id = 0x1;
  touchpad_v2_report.fingers[1].x = 100;
  touchpad_v2_report.fingers[1].y = 200;

  touchpad_v2_report.fingers[2].tip_switch = 0x1;
  touchpad_v2_report.fingers[2].id = 0x2;
  touchpad_v2_report.fingers[2].x = 300;
  touchpad_v2_report.fingers[2].y = 400;

  uint8_t *report_data = reinterpret_cast<uint8_t *>(&touchpad_v2_report);

  ui_input::Touch::Report report;
  auto success = ts.ParseReport(report_data, sizeof(touchpad_v2_report), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(2UL, report.contact_count);

  // The X and Y values below have been manually calculated based on the above
  // input and the logical/physical conversion given by the report descriptor.
  EXPECT_EQ(1U, report.contacts[0].id);
  EXPECT_EQ(7812, report.contacts[0].x);
  EXPECT_EQ(15625, report.contacts[0].y);

  EXPECT_EQ(2U, report.contacts[1].id);
  EXPECT_EQ(23437, report.contacts[1].x);
  EXPECT_EQ(31250, report.contacts[1].y);
}

}  // namespace test
}  // namespace input
