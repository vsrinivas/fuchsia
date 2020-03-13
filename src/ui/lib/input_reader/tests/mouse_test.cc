// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/mouse.h"

#include <fuchsia/ui/input/cpp/fidl.h>

#include <gtest/gtest.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/paradise.h>

namespace input {

namespace {

const uint8_t boot_mouse_desc[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,  // Usage (Mouse)
    0xA1, 0x01,  // Collection (Application)
    0x09, 0x01,  //   Usage (Pointer)
    0xA1, 0x00,  //   Collection (Physical)
    0x05, 0x09,  //     Usage Page (Button)
    0x19, 0x01,  //     Usage Minimum (0x01)
    0x29, 0x03,  //     Usage Maximum (0x03)
    0x15, 0x00,  //     Logical Minimum (0)
    0x25, 0x01,  //     Logical Maximum (1)
    0x95, 0x03,  //     Report Count (3)
    0x75, 0x01,  //     Report Size (1)
    0x81, 0x02,  //     Input (Data,Var,Abs,No Wrap,Linear,No Null Position)
    0x95, 0x01,  //     Report Count (1)
    0x75, 0x05,  //     Report Size (5)
    0x81, 0x03,  //     Input (Const,Var,Abs,No Wrap,Linear,No Null Position
    0x05, 0x01,  //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,  //     Usage (X)
    0x09, 0x31,  //     Usage (Y)
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x02,  //     Report Count (2)
    0x81, 0x06,  //     Input (Data,Var,Rel,No Wrap,Linear,No Null Position)
    0xC0,        //   End Collection
    0xC0,        // End Collection
};
}  // namespace

// Each test parses the report descriptor for the mouse and then sends one
// report to ensure that it has been parsed correctly.
namespace test {

TEST(MouseTest, BootMouse) {
  hid::DeviceDescriptor *dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(boot_mouse_desc, sizeof(boot_mouse_desc), &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  ui_input::Mouse mouse = {};
  ui_input::Device::Descriptor device_descriptor = {};
  bool success = mouse.ParseReportDescriptor(dev_desc->report[0], &device_descriptor);
  ASSERT_TRUE(success);
  EXPECT_EQ(device_descriptor.has_mouse, true);
  EXPECT_EQ(device_descriptor.mouse_type, ui_input::MouseDeviceType::HID);
  EXPECT_EQ(device_descriptor.mouse_descriptor->buttons,
            fuchsia::ui::input::kMouseButtonPrimary | fuchsia::ui::input::kMouseButtonSecondary |
                fuchsia::ui::input::kMouseButtonTertiary);

  const uint8_t report_data[] = {
      0xFF,  // Buttons
      100,   // X
      0xFF,  // Y
  };

  fuchsia::ui::input::InputReport report;
  report.mouse = fuchsia::ui::input::MouseReport::New();
  success = mouse.ParseReport(report_data, sizeof(report_data), &report);
  EXPECT_EQ(true, success);

  EXPECT_EQ(fuchsia::ui::input::kMouseButtonPrimary | fuchsia::ui::input::kMouseButtonSecondary |
                fuchsia::ui::input::kMouseButtonTertiary,
            report.mouse->pressed_buttons);
  EXPECT_EQ(100, report.mouse->rel_x);
  EXPECT_EQ(-1, report.mouse->rel_y);
}
}  // namespace test
}  // namespace input
