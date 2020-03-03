// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/touch.h"

#include <variant>

#include <hid/paradise.h>
#include <zxtest/zxtest.h>

#include "src/ui/input/lib/hid-input-report/device.h"

// Each test parses the report descriptor for the touchscreen and then sends one
// report to ensure that it has been parsed correctly.

void HidParseTouchscreen(const uint8_t* desc, size_t desc_len, hid::DeviceDescriptor** out_desc,
                         hid::ReportDescriptor** out_report) {
  auto parse_res = hid::ParseReportDescriptor(desc, desc_len, out_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  auto count = (*out_desc)->rep_count;
  ASSERT_LT(0UL, count);

  // Find the first input report.
  for (size_t rep = 0; rep < count; rep++) {
    hid::ReportDescriptor* report = &(*out_desc)->report[rep];
    if (report->input_count != 0) {
      *out_report = report;
      return;
    }
  }
}

TEST(TouchscreenTest, ParadiseV1) {
  size_t desc_size;
  const uint8_t* paradise_touch_v1_report_desc = get_paradise_touch_report_desc(&desc_size);

  hid::DeviceDescriptor* hid_desc;
  hid::ReportDescriptor* hid_report_desc;
  HidParseTouchscreen(paradise_touch_v1_report_desc, desc_size, &hid_desc, &hid_report_desc);

  hid_input_report::Touch touch;
  EXPECT_EQ(hid_input_report::ParseResult::kOk, touch.ParseReportDescriptor(*hid_report_desc));
  hid_input_report::ReportDescriptor report_descriptor = touch.GetDescriptor();

  hid_input_report::TouchDescriptor* touch_descriptor =
      std::get_if<hid_input_report::TouchDescriptor>(&report_descriptor.descriptor);
  ASSERT_NOT_NULL(touch_descriptor);

  EXPECT_EQ(5UL, touch_descriptor->input->num_contacts);
  EXPECT_TRUE(touch_descriptor->input->contacts[0].contact_id);
  EXPECT_TRUE(touch_descriptor->input->contacts[0].is_pressed);

  EXPECT_TRUE(touch_descriptor->input->contacts[0].position_x);
  EXPECT_EQ(0, touch_descriptor->input->contacts[0].position_x->range.min);
  EXPECT_EQ(259200, touch_descriptor->input->contacts[0].position_x->range.max);

  EXPECT_TRUE(touch_descriptor->input->contacts[0].position_y);
  EXPECT_EQ(0, touch_descriptor->input->contacts[0].position_y->range.min);
  EXPECT_EQ(172800, touch_descriptor->input->contacts[0].position_y->range.max);

  // Now use the parsed descriptor to interpret a touchpad report.
  paradise_touch_t touch_v1_report = {};
  touch_v1_report.rpt_id = 12;
  touch_v1_report.contact_count = 1;
  touch_v1_report.fingers[1].flags = 0xF;
  touch_v1_report.fingers[1].finger_id = 0x1;
  touch_v1_report.fingers[1].x = 100;
  touch_v1_report.fingers[1].y = 200;

  hid_input_report::InputReport report = {};
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            touch.ParseInputReport(reinterpret_cast<uint8_t*>(&touch_v1_report),
                                   sizeof(touch_v1_report), &report));

  hid_input_report::TouchInputReport* touch_report =
      std::get_if<hid_input_report::TouchInputReport>(&report.report);
  ASSERT_NOT_NULL(touch_report);

  EXPECT_EQ(1UL, touch_report->num_contacts);

  // The expected values below have been manually converted from logical to physical units based
  // on the report descriptor.

  EXPECT_TRUE(touch_report->contacts[0].contact_id);
  EXPECT_EQ(1U, *touch_report->contacts[0].contact_id);

  EXPECT_TRUE(touch_report->contacts[0].position_x);
  EXPECT_EQ(2500, *touch_report->contacts[0].position_x);

  EXPECT_TRUE(touch_report->contacts[0].position_y);
  EXPECT_EQ(5000, *touch_report->contacts[0].position_y);
}

TEST(TouchscreenTest, ParadiseV1Touchpad) {
  size_t desc_size;
  const uint8_t* desc = get_paradise_touchpad_v1_report_desc(&desc_size);

  hid::DeviceDescriptor* hid_desc;
  hid::ReportDescriptor* hid_report_desc;
  HidParseTouchscreen(desc, desc_size, &hid_desc, &hid_report_desc);

  hid_input_report::Touch touch;
  EXPECT_EQ(hid_input_report::ParseResult::kOk, touch.ParseReportDescriptor(*hid_report_desc));
  hid_input_report::ReportDescriptor report_descriptor = touch.GetDescriptor();

  hid_input_report::TouchDescriptor* touch_descriptor =
      std::get_if<hid_input_report::TouchDescriptor>(&report_descriptor.descriptor);
  ASSERT_NOT_NULL(touch_descriptor);

  EXPECT_EQ(5UL, touch_descriptor->input->num_contacts);
  EXPECT_TRUE(touch_descriptor->input->contacts[0].contact_id);
  EXPECT_TRUE(touch_descriptor->input->contacts[0].is_pressed);

  EXPECT_TRUE(touch_descriptor->input->contacts[0].position_x);
  EXPECT_EQ(0, touch_descriptor->input->contacts[0].position_x->range.min);
  EXPECT_EQ(103000, touch_descriptor->input->contacts[0].position_x->range.max);

  EXPECT_TRUE(touch_descriptor->input->contacts[0].position_y);
  EXPECT_EQ(0, touch_descriptor->input->contacts[0].position_y->range.min);
  EXPECT_EQ(68000, touch_descriptor->input->contacts[0].position_y->range.max);

  EXPECT_EQ(1, touch_descriptor->input->num_buttons = 1);
  EXPECT_EQ(1, touch_descriptor->input->buttons[0]);

  // Now use the parsed descriptor to interpret a touchpad report.
  paradise_touchpad_v1_t touch_report = {};
  touch_report.report_id = 1;
  touch_report.button = 1;
  touch_report.contact_count = 1;
  touch_report.fingers[0].tip_switch = 1;
  touch_report.fingers[0].id = 5;
  touch_report.fingers[0].x = 200;
  touch_report.fingers[0].y = 100;

  hid_input_report::InputReport report = {};
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            touch.ParseInputReport(reinterpret_cast<uint8_t*>(&touch_report), sizeof(touch_report),
                                   &report));

  hid_input_report::TouchInputReport* touch_input_report =
      std::get_if<hid_input_report::TouchInputReport>(&report.report);
  ASSERT_NOT_NULL(touch_input_report);

  EXPECT_EQ(1UL, touch_input_report->num_contacts);

  // The expected values below have been manually converted from logical to physical units based
  // on the report descriptor.

  EXPECT_TRUE(touch_input_report->contacts[0].contact_id);
  EXPECT_EQ(5U, *touch_input_report->contacts[0].contact_id);

  EXPECT_TRUE(touch_input_report->contacts[0].position_x);
  EXPECT_EQ(1562, *touch_input_report->contacts[0].position_x);

  EXPECT_TRUE(touch_input_report->contacts[0].position_y);
  EXPECT_EQ(781, *touch_input_report->contacts[0].position_y);

  EXPECT_EQ(1, touch_input_report->num_pressed_buttons);
  EXPECT_EQ(1, touch_input_report->pressed_buttons[0]);
}

TEST(TouchscreenTest, DeviceType) {
  hid_input_report::Touch device;
  ASSERT_EQ(hid_input_report::DeviceType::kTouch, device.GetDeviceType());
}
