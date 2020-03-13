// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/ft3x27.h>
#include <hid/paradise.h>

#include "src/ui/lib/input_reader/tests/touchscreen_test_data.h"
#include "src/ui/lib/input_reader/touch.h"

namespace input {

namespace {

void ParseTouchscreen(const uint8_t *desc, size_t desc_len, ui_input::Touch *ts) {
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

// These unit tests exercise the touchscreen examples in touchscreen_test_data.h
// Each test parses the report descriptor for the touchscreen and then sends one
// report to ensure that it has been parsed correctly.
namespace test {

TEST(TouchscreenTest, Gechic1101) {
  ui_input::Touch ts;
  ParseTouchscreen(gechic1101_hid_descriptor, sizeof(gechic1101_hid_descriptor), &ts);
  ui_input::Touch::Descriptor ts_desc;
  EXPECT_TRUE(ts.SetDescriptor(&ts_desc));

  EXPECT_EQ(10UL, ts.touch_points());
  EXPECT_EQ(ui_input::Touch::Capabilities::CONTACT_ID | ui_input::Touch::Capabilities::TIP_SWITCH |
                ui_input::Touch::Capabilities::X | ui_input::Touch::Capabilities::Y |
                ui_input::Touch::Capabilities::CONTACT_COUNT |
                ui_input::Touch::Capabilities::SCAN_TIME,
            ts.capabilities());
  EXPECT_EQ(0, ts_desc.x_min);
  EXPECT_EQ(2563000, ts_desc.x_max);
  EXPECT_EQ(0, ts_desc.y_min);
  EXPECT_EQ(1442000, ts_desc.y_max);

  uint8_t report_data[] = {
      0x04,                                            // Report ID
      0x40, 0x22, 0x21, 0x1f, 0x17,                    // Finger 0
      0x00, 0x00, 0x00, 0x00, 0x00,                    // Finger 1
      0x00, 0x00, 0x00, 0x00, 0x00,                    // Finger 2
      0x00, 0x00, 0x00, 0x00, 0x00,                    // Finger 3
      0x00, 0x00, 0x00, 0x00, 0x00,                    // Finger 4
      0x00, 0x00, 0x00, 0x00, 0x00,                    // Finger 5
      0x00, 0x00, 0x00, 0x00, 0x00,                    // Finger 6
      0x00, 0x00, 0x00, 0x00, 0x00,                    // Finger 7
      0x00, 0x00, 0x00, 0x00, 0x00,                    // Finger 8
      0x00, 0x00, 0x00, 0x00, 0x00,                    // Finger 9
      0x00, 0x0a, 0x00, 0x00,                          // Scan Time
      0x01,                                            // Contact Time
      0x01, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Constant Value
  };

  ui_input::Touch::Report report;
  auto success = ts.ParseReport(report_data, sizeof(report_data), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(1UL, report.contact_count);
  EXPECT_EQ(0xa00u, report.scan_time);

  EXPECT_EQ(0U, report.contacts[0].id);
  // Test that X and Y have been converted to micrometers
  // These values have been manually calculated based on the above report_data
  EXPECT_EQ(1326865, report.contacts[0].x);
  EXPECT_EQ(889083, report.contacts[0].y);
}

TEST(TouchscreenTest, CoolTouch) {
  ui_input::Touch ts;
  ParseTouchscreen(cooltouch_10x_hid_descriptor, sizeof(cooltouch_10x_hid_descriptor), &ts);

  ui_input::Touch::Descriptor ts_desc;
  EXPECT_TRUE(ts.SetDescriptor(&ts_desc));

  EXPECT_EQ(5UL, ts.touch_points());
  EXPECT_EQ(ui_input::Touch::Capabilities::CONTACT_ID | ui_input::Touch::Capabilities::TIP_SWITCH |
                ui_input::Touch::Capabilities::X | ui_input::Touch::Capabilities::Y |
                ui_input::Touch::Capabilities::CONTACT_COUNT |
                ui_input::Touch::Capabilities::SCAN_TIME,
            ts.capabilities());
  EXPECT_EQ(0, ts_desc.x_min);
  EXPECT_EQ(2771000, ts_desc.x_max);
  EXPECT_EQ(0, ts_desc.y_min);
  EXPECT_EQ(1561000, ts_desc.y_max);

  uint8_t report_data[] = {
      0x01,                          // Report ID
      0x09, 0x6f, 0x3b, 0x1e, 0x4b,  // Finger 0
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 1
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 2
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 3
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 4
      0x4c, 0x00,                    // Scan Time
      0x01,                          // Contact Count
  };

  ui_input::Touch::Report report;
  auto success = ts.ParseReport(report_data, sizeof(report_data), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(1UL, report.contact_count);
  EXPECT_EQ(0x004cU, report.scan_time);

  EXPECT_EQ(1U, report.contacts[0].id);
  // Test that X and Y have been converted to micrometers
  // These values have been manually calculated based on the above report_data
  EXPECT_EQ(1286683, report.contacts[0].x);
  EXPECT_EQ(916105, report.contacts[0].y);
}

TEST(TouchscreenTest, WaveShare) {
  ui_input::Touch ts;
  ParseTouchscreen(waveshare_hid_descriptor, sizeof(waveshare_hid_descriptor), &ts);

  ui_input::Touch::Descriptor ts_desc;
  EXPECT_TRUE(ts.SetDescriptor(&ts_desc));

  EXPECT_EQ(1UL, ts.touch_points());
  EXPECT_EQ(ui_input::Touch::Capabilities::CONTACT_ID | ui_input::Touch::Capabilities::TIP_SWITCH |
                ui_input::Touch::Capabilities::X | ui_input::Touch::Capabilities::Y |
                ui_input::Touch::Capabilities::CONTACT_COUNT |
                ui_input::Touch::Capabilities::SCAN_TIME,
            ts.capabilities());
  EXPECT_EQ(0, ts_desc.x_min);
  EXPECT_EQ(655350000, ts_desc.x_max);
  EXPECT_EQ(0, ts_desc.y_min);
  EXPECT_EQ(655350000, ts_desc.y_max);

  uint8_t report_data[] = {
      0x01,        // Report ID
      0x01,        // Tip Switch
      0x00,        // Contact ID
      0x03,        // Tip Pressure
      0xa0, 0x02,  // X
      0x46, 0x01,  // Y
      0xf4, 0xd4,  // Scan Time
      0x01,        // Contact Count
  };

  ui_input::Touch::Report report;
  auto success = ts.ParseReport(report_data, sizeof(report_data), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(1UL, report.contact_count);
  // Scan time converts from 10^-4 seconds to 10^-6 seconds.
  EXPECT_EQ(0xd4f4U * 100, report.scan_time);

  EXPECT_EQ(0U, report.contacts[0].id);
  // Test that X and Y have been converted to micrometers
  // These values have been manually calculated based on the above report_data
  EXPECT_EQ(430073437, report.contacts[0].x);
  EXPECT_EQ(356073500, report.contacts[0].y);
}

TEST(TouchscreenTest, Gechic1303) {
  ui_input::Touch ts;
  ParseTouchscreen(gechic_1303_hid_descriptor, sizeof(gechic_1303_hid_descriptor), &ts);

  ui_input::Touch::Descriptor ts_desc;
  EXPECT_TRUE(ts.SetDescriptor(&ts_desc));

  EXPECT_EQ(10UL, ts.touch_points());
  EXPECT_EQ(ui_input::Touch::Capabilities::CONTACT_ID | ui_input::Touch::Capabilities::TIP_SWITCH |
                ui_input::Touch::Capabilities::X | ui_input::Touch::Capabilities::Y |
                ui_input::Touch::Capabilities::CONTACT_COUNT |
                ui_input::Touch::Capabilities::SCAN_TIME,
            ts.capabilities());
  EXPECT_EQ(0, ts_desc.x_min);
  EXPECT_EQ(5090000, ts_desc.x_max);
  EXPECT_EQ(0, ts_desc.y_min);
  EXPECT_EQ(2860000, ts_desc.y_max);

  uint8_t report_data[] = {
      0x04,                          // Report ID
      0x40, 0xef, 0x1e, 0xe9, 0x15,  // Finger 0
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 1
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 2
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 3
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 4
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 5
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 6
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 7
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 8
      0x00, 0x00, 0x00, 0x00, 0x00,  // Finger 9
      0xc0, 0x2b, 0x00, 0x00,        // Scan Time
      0x01,                          // Contact Count
  };

  ui_input::Touch::Report report;
  auto success = ts.ParseReport(report_data, sizeof(report_data), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(1UL, report.contact_count);
  EXPECT_EQ(0x2bc0U, report.scan_time);

  EXPECT_EQ(0U, report.contacts[0].id);

  // Test that X and Y have been converted to micrometers
  // These values have been manually calculated based on the above report_data
  EXPECT_EQ(2460187, report.contacts[0].x);
  EXPECT_EQ(1671014, report.contacts[0].y);
}

TEST(TouchscreenTest, ParadiseV1) {
  ui_input::Touch ts;
  size_t desc_size;
  const uint8_t *paradise_touch_v1_report_desc = get_paradise_touch_report_desc(&desc_size);

  ParseTouchscreen(paradise_touch_v1_report_desc, desc_size, &ts);
  ui_input::Touch::Descriptor ts_desc;
  EXPECT_TRUE(ts.SetDescriptor(&ts_desc));

  EXPECT_EQ(5UL, ts.touch_points());
  EXPECT_EQ(ui_input::Touch::Capabilities::CONTACT_ID | ui_input::Touch::Capabilities::TIP_SWITCH |
                ui_input::Touch::Capabilities::X | ui_input::Touch::Capabilities::Y |
                ui_input::Touch::Capabilities::CONTACT_COUNT |
                ui_input::Touch::Capabilities::SCAN_TIME,
            ts.capabilities());
  EXPECT_EQ(0, ts_desc.x_min);
  EXPECT_EQ(2592000, ts_desc.x_max);
  EXPECT_EQ(0, ts_desc.y_min);
  EXPECT_EQ(1728000, ts_desc.y_max);

  // Now use the parsed descriptor to interpret a touchpad report.
  paradise_touch_t touch_v1_report = {};
  touch_v1_report.rpt_id = 12;
  touch_v1_report.contact_count = 1;
  touch_v1_report.scan_time = 0xabc;
  touch_v1_report.fingers[1].flags = 0xF;
  touch_v1_report.fingers[1].finger_id = 0x1;
  touch_v1_report.fingers[1].x = 100;
  touch_v1_report.fingers[1].y = 200;

  uint8_t *report_data = reinterpret_cast<uint8_t *>(&touch_v1_report);

  ui_input::Touch::Report report;
  auto success = ts.ParseReport(report_data, sizeof(touch_v1_report), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(1UL, report.contact_count);
  EXPECT_EQ(72U, report.scan_time);

  EXPECT_EQ(1U, report.contacts[0].id);
  EXPECT_EQ(25000, report.contacts[0].x);
  EXPECT_EQ(50000, report.contacts[0].y);
}

TEST(TouchscreenTest, ParadiseV2) {
  ui_input::Touch ts;
  size_t desc_size;
  const uint8_t *paradise_touch_v2_report_desc = get_paradise_touch_v2_report_desc(&desc_size);

  ParseTouchscreen(paradise_touch_v2_report_desc, desc_size, &ts);
  ui_input::Touch::Descriptor ts_desc;
  EXPECT_TRUE(ts.SetDescriptor(&ts_desc));

  EXPECT_EQ(5UL, ts.touch_points());
  EXPECT_EQ(ui_input::Touch::Capabilities::CONTACT_ID | ui_input::Touch::Capabilities::TIP_SWITCH |
                ui_input::Touch::Capabilities::X | ui_input::Touch::Capabilities::Y |
                ui_input::Touch::Capabilities::CONTACT_COUNT |
                ui_input::Touch::Capabilities::SCAN_TIME,
            ts.capabilities());
  EXPECT_EQ(0, ts_desc.x_min);
  EXPECT_EQ(2592000, ts_desc.x_max);
  EXPECT_EQ(0, ts_desc.y_min);
  EXPECT_EQ(1728000, ts_desc.y_max);

  // Now use the parsed descriptor to interpret a touchpad report.
  paradise_touch_v2_t touch_v2_report = {};
  touch_v2_report.rpt_id = 12;
  touch_v2_report.contact_count = 1;
  touch_v2_report.scan_time = 0xabc;
  touch_v2_report.fingers[1].flags = 0xF;
  touch_v2_report.fingers[1].finger_id = 0x1;
  touch_v2_report.fingers[1].x = 100;
  touch_v2_report.fingers[1].y = 200;

  uint8_t *report_data = reinterpret_cast<uint8_t *>(&touch_v2_report);

  ui_input::Touch::Report report;
  auto success = ts.ParseReport(report_data, sizeof(touch_v2_report), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(1UL, report.contact_count);
  EXPECT_EQ(0xabcU, report.scan_time);

  EXPECT_EQ(1U, report.contacts[0].id);
  EXPECT_EQ(25000, report.contacts[0].x);
  EXPECT_EQ(50000, report.contacts[0].y);
}

TEST(TouchscreenTest, ParadiseV3) {
  ui_input::Touch ts;
  size_t desc_size;
  const uint8_t *paradise_touch_v3_report_desc = get_paradise_touch_v3_report_desc(&desc_size);

  ParseTouchscreen(paradise_touch_v3_report_desc, desc_size, &ts);
  ui_input::Touch::Descriptor ts_desc;
  EXPECT_TRUE(ts.SetDescriptor(&ts_desc));

  EXPECT_EQ(5UL, ts.touch_points());
  EXPECT_EQ(ui_input::Touch::Capabilities::CONTACT_ID | ui_input::Touch::Capabilities::TIP_SWITCH |
                ui_input::Touch::Capabilities::X | ui_input::Touch::Capabilities::Y |
                ui_input::Touch::Capabilities::CONTACT_COUNT |
                ui_input::Touch::Capabilities::SCAN_TIME,
            ts.capabilities());
  EXPECT_EQ(0, ts_desc.x_min);
  EXPECT_EQ(2593000, ts_desc.x_max);
  EXPECT_EQ(0, ts_desc.y_min);
  EXPECT_EQ(1729000, ts_desc.y_max);

  // Now use the parsed descriptor to interpret a touchpad report.
  // The v3 report is the same as the v1 report.
  paradise_touch_t touch_v3_report = {};
  touch_v3_report.rpt_id = 12;
  touch_v3_report.contact_count = 1;
  touch_v3_report.scan_time = 0xabc;
  touch_v3_report.fingers[1].flags = 0xF;
  touch_v3_report.fingers[1].finger_id = 0x1;
  touch_v3_report.fingers[1].x = 100;
  touch_v3_report.fingers[1].y = 200;

  uint8_t *report_data = reinterpret_cast<uint8_t *>(&touch_v3_report);

  ui_input::Touch::Report report;
  auto success = ts.ParseReport(report_data, sizeof(touch_v3_report), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(1UL, report.contact_count);
  EXPECT_EQ(72U, report.scan_time);

  EXPECT_EQ(1U, report.contacts[0].id);
  EXPECT_EQ(25000, report.contacts[0].x);
  EXPECT_EQ(50000, report.contacts[0].y);
}

TEST(TouchscreenTest, Ft3x27) {
  ui_input::Touch ts;
  const uint8_t *ft3x27_report_desc;
  size_t desc_size = get_ft3x27_report_desc(&ft3x27_report_desc);

  ParseTouchscreen(ft3x27_report_desc, desc_size, &ts);
  ui_input::Touch::Descriptor ts_desc;
  EXPECT_TRUE(ts.SetDescriptor(&ts_desc));

  EXPECT_EQ(5UL, ts.touch_points());
  EXPECT_EQ(ui_input::Touch::Capabilities::CONTACT_ID | ui_input::Touch::Capabilities::TIP_SWITCH |
                ui_input::Touch::Capabilities::X | ui_input::Touch::Capabilities::Y |
                ui_input::Touch::Capabilities::CONTACT_COUNT,
            ts.capabilities());
  EXPECT_EQ(0, ts_desc.x_min);
  EXPECT_EQ(600, ts_desc.x_max);
  EXPECT_EQ(0, ts_desc.y_min);
  EXPECT_EQ(1024, ts_desc.y_max);

  // Now use the parsed descriptor to interpret a touchpad report.
  ft3x27_touch touch_report = {};
  touch_report.rpt_id = 1;
  touch_report.contact_count = 1;
  touch_report.fingers[1].finger_id = 0xFF;
  touch_report.fingers[1].x = 100;
  touch_report.fingers[1].y = 200;

  uint8_t *report_data = reinterpret_cast<uint8_t *>(&touch_report);

  ui_input::Touch::Report report;
  auto success = ts.ParseReport(report_data, sizeof(touch_report), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(1UL, report.contact_count);

  // 63 is the max allowed ID since the contactID field is only 6 bits wide.
  EXPECT_EQ(63U, report.contacts[0].id);
  EXPECT_EQ(100, report.contacts[0].x);
  EXPECT_EQ(200, report.contacts[0].y);
}

}  // namespace test
}  // namespace input
