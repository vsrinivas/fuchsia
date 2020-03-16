// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/mouse.h"

#include <variant>

#include <hid/boot.h>
#include <hid/mouse.h>
#include <zxtest/zxtest.h>

#include "src/ui/input/lib/hid-input-report/device.h"

// Each test parses the report descriptor for the mouse and then sends one
// report to ensure that it has been parsed correctly.

const uint8_t vnc_mouse_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x16, 0x00, 0x00,  //     Logical Minimum (0)
    0x26, 0xFF, 0x3F,  //     Logical Maximum (16383)
    0x36, 0x00, 0x00,  //     Physical Minimum (0)
    0x46, 0xFF, 0x3F,  //     Physical Maximum (16383)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x31,        //     Usage (Y)
    0x16, 0x00, 0x00,  //     Logical Minimum (0)
    0x26, 0xFF, 0x3F,  //     Logical Maximum (16383)
    0x36, 0x00, 0x00,  //     Physical Minimum (0)
    0x46, 0xFF, 0x3F,  //     Physical Maximum (16383)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x03,        //     Usage Maximum (0x03)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x01,  //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,  //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x38,  //     Usage (Wheel)
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x35, 0x81,  //     Physical Minimum (-127)
    0x45, 0x7F,  //     Physical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x01,  //     Report Count (1)
    0x81, 0x06,  //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,        //   End Collection
    0xC0,        // End Collection
};

typedef struct {
  uint16_t position_x;
  uint16_t position_y;
  int8_t buttons;
  int8_t wheel;
} __attribute__((packed)) vnc_mouse_report_t;

TEST(MouseTest, BootMouse) {
  hid::DeviceDescriptor* dev_desc = nullptr;
  size_t boot_mouse_desc_size;
  const uint8_t* boot_mouse_desc = get_boot_mouse_report_desc(&boot_mouse_desc_size);
  auto parse_res = hid::ParseReportDescriptor(boot_mouse_desc, boot_mouse_desc_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  hid_input_report::Mouse mouse;

  EXPECT_EQ(hid_input_report::ParseResult::kOk, mouse.ParseReportDescriptor(dev_desc->report[0]));
  hid_input_report::ReportDescriptor report_descriptor = mouse.GetDescriptor();

  hid_input_report::MouseDescriptor* mouse_descriptor =
      std::get_if<hid_input_report::MouseDescriptor>(&report_descriptor.descriptor);
  ASSERT_NOT_NULL(mouse_descriptor);

  EXPECT_TRUE(mouse_descriptor->input->movement_x);
  EXPECT_TRUE(mouse_descriptor->input->movement_y);
  constexpr uint8_t kNumButtons = 3;
  EXPECT_EQ(kNumButtons, mouse_descriptor->input->num_buttons);

  EXPECT_EQ(0, mouse.InputReportId());

  hid_boot_mouse_report_t report_data = {};
  const int kXTestVal = 10;
  const int kYTestVal = -5;
  report_data.rel_x = kXTestVal;
  report_data.rel_y = kYTestVal;
  report_data.buttons = 0xFF;

  hid_input_report::InputReport report = {};
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            mouse.ParseInputReport(reinterpret_cast<uint8_t*>(&report_data), sizeof(report_data),
                                   &report));

  hid_input_report::MouseInputReport* mouse_report =
      std::get_if<hid_input_report::MouseInputReport>(&report.report);
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

TEST(MouseTest, ScrollMouse) {
  hid::DeviceDescriptor* dev_desc = nullptr;
  {
    size_t descriptor_size;
    const uint8_t* descriptor = get_scroll_mouse_report_desc(&descriptor_size);
    auto parse_res = hid::ParseReportDescriptor(descriptor, descriptor_size, &dev_desc);
    ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);
  }

  hid_input_report::Mouse mouse;

  EXPECT_EQ(hid_input_report::ParseResult::kOk, mouse.ParseReportDescriptor(dev_desc->report[0]));
  hid_input_report::ReportDescriptor report_descriptor = mouse.GetDescriptor();

  hid_input_report::MouseDescriptor* mouse_descriptor =
      std::get_if<hid_input_report::MouseDescriptor>(&report_descriptor.descriptor);
  ASSERT_NOT_NULL(mouse_descriptor);

  EXPECT_TRUE(mouse_descriptor->input->scroll_v);
  EXPECT_EQ(-127, mouse_descriptor->input->scroll_v->range.min);
  EXPECT_EQ(127, mouse_descriptor->input->scroll_v->range.max);

  hid_scroll_mouse_report_t report_data = {};
  report_data.scroll = 100;

  hid_input_report::InputReport report = {};
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            mouse.ParseInputReport(reinterpret_cast<uint8_t*>(&report_data), sizeof(report_data),
                                   &report));

  hid_input_report::MouseInputReport* mouse_report =
      std::get_if<hid_input_report::MouseInputReport>(&report.report);
  ASSERT_NOT_NULL(mouse_report);
  EXPECT_TRUE(mouse_report->scroll_v);
  EXPECT_EQ(100, mouse_report->scroll_v);
}

TEST(MouseTest, VncMouse) {
  hid::DeviceDescriptor* dev_desc = nullptr;
  {
    auto parse_res =
        hid::ParseReportDescriptor(vnc_mouse_descriptor, sizeof(vnc_mouse_descriptor), &dev_desc);
    ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);
  }

  hid_input_report::Mouse mouse;

  EXPECT_EQ(hid_input_report::ParseResult::kOk, mouse.ParseReportDescriptor(dev_desc->report[0]));
  hid_input_report::ReportDescriptor report_descriptor = mouse.GetDescriptor();

  hid_input_report::MouseDescriptor* mouse_descriptor =
      std::get_if<hid_input_report::MouseDescriptor>(&report_descriptor.descriptor);
  ASSERT_NOT_NULL(mouse_descriptor);

  EXPECT_TRUE(mouse_descriptor->input->position_x);
  EXPECT_EQ(0, mouse_descriptor->input->position_x->range.min);
  EXPECT_EQ(16383, mouse_descriptor->input->position_x->range.max);

  EXPECT_TRUE(mouse_descriptor->input->position_y);
  EXPECT_EQ(0, mouse_descriptor->input->position_y->range.min);
  EXPECT_EQ(16383, mouse_descriptor->input->position_y->range.max);

  vnc_mouse_report_t report_data = {};
  report_data.position_y = 500;
  report_data.position_y = 1000;

  hid_input_report::InputReport report = {};
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            mouse.ParseInputReport(reinterpret_cast<uint8_t*>(&report_data), sizeof(report_data),
                                   &report));

  hid_input_report::MouseInputReport* mouse_report =
      std::get_if<hid_input_report::MouseInputReport>(&report.report);
  ASSERT_NOT_NULL(mouse_report);

  EXPECT_TRUE(mouse_report->position_x);
  EXPECT_EQ(report_data.position_x, mouse_report->position_x);

  EXPECT_TRUE(mouse_report->position_y);
  EXPECT_EQ(report_data.position_y, mouse_report->position_y);
}

TEST(MouseTest, DeviceType) {
  hid_input_report::Mouse device;
  ASSERT_EQ(hid_input_report::DeviceType::kMouse, device.GetDeviceType());
}
