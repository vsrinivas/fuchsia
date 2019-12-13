// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/buttons.h"

#include <fuchsia/ui/input/cpp/fidl.h>

#include <gtest/gtest.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/visalia-touch.h>

#include "src/lib/fxl/time/time_point.h"

namespace input {

// Each test parses the report descriptor for the mouse and then sends one
// report to ensure that it has been parsed correctly.
namespace test {

TEST(ButtonsTest, VisaliaButtons) {
  const uint8_t* visalia_descriptor;
  size_t visalia_descriptor_size = get_visalia_touch_buttons_report_desc(&visalia_descriptor);

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res =
      hid::ParseReportDescriptor(visalia_descriptor, visalia_descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  ui_input::Buttons buttons = {};
  ui_input::Device::Descriptor device_descriptor = {};
  bool success = buttons.ParseReportDescriptor(dev_desc->report[0], &device_descriptor);
  ASSERT_TRUE(success);
  EXPECT_EQ(device_descriptor.has_media_buttons, true);
  EXPECT_EQ(device_descriptor.buttons_descriptor->buttons, fuchsia::ui::input::kVolumeUp |
                                                               fuchsia::ui::input::kVolumeDown |
                                                               fuchsia::ui::input::kPause);

  // Bit 0 - Volume up.
  // Bit 1 - Volume down.
  // Bit 2 - Pause.
  uint8_t report_data[2] = {BUTTONS_RPT_ID_INPUT, 0b101};

  fuchsia::ui::input::InputReport report;
  report.media_buttons = fuchsia::ui::input::MediaButtonsReport::New();
  success = buttons.ParseReport(report_data, sizeof(report_data), &report);
  EXPECT_EQ(true, success);

  EXPECT_TRUE(report.media_buttons->volume_up);
  EXPECT_FALSE(report.media_buttons->volume_down);
  EXPECT_TRUE(report.media_buttons->pause);
}
}  // namespace test
}  // namespace input
