// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <variant>

#include <ddk/metadata/buttons.h>
#include <hid/buttons.h>
#include <zxtest/zxtest.h>

#include "src/ui/input/lib/hid-input-report/consumer_control.h"
#include "src/ui/input/lib/hid-input-report/device.h"

TEST(ConsumerControlTest, HidButtonsTest) {
  const uint8_t* descriptor;
  size_t descriptor_size = get_buttons_report_desc(&descriptor);

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(descriptor, descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  hid_input_report::ConsumerControl consumer_control;
  EXPECT_EQ(hid_input_report::ParseResult::kParseOk,
            consumer_control.ParseReportDescriptor(dev_desc->report[0]));
  hid_input_report::ReportDescriptor report_descriptor = consumer_control.GetDescriptor();

  hid_input_report::ConsumerControlDescriptor* consumer_control_descriptor =
      std::get_if<hid_input_report::ConsumerControlDescriptor>(&report_descriptor.descriptor);
  ASSERT_NOT_NULL(consumer_control_descriptor);

  // Test the descriptor.
  EXPECT_EQ(consumer_control_descriptor->input->num_buttons, 4U);
  EXPECT_EQ(consumer_control_descriptor->input->buttons[0],
            llcpp::fuchsia::input::report::ConsumerControlButton::VOLUME_UP);
  EXPECT_EQ(consumer_control_descriptor->input->buttons[1],
            llcpp::fuchsia::input::report::ConsumerControlButton::VOLUME_DOWN);
  EXPECT_EQ(consumer_control_descriptor->input->buttons[2],
            llcpp::fuchsia::input::report::ConsumerControlButton::REBOOT);
  EXPECT_EQ(consumer_control_descriptor->input->buttons[3],
            llcpp::fuchsia::input::report::ConsumerControlButton::MIC_MUTE);

  // Test a report parses correctly.
  struct buttons_input_rpt report = {};
  report.rpt_id = BUTTONS_RPT_ID_INPUT;
  fill_button_in_report(BUTTONS_ID_VOLUME_UP, true, &report);
  fill_button_in_report(BUTTONS_ID_FDR, true, &report);
  fill_button_in_report(BUTTONS_ID_MIC_MUTE, true, &report);

  hid_input_report::InputReport input_report = {};
  EXPECT_EQ(hid_input_report::ParseResult::kParseOk,
            consumer_control.ParseInputReport(reinterpret_cast<uint8_t*>(&report), sizeof(report),
                                              &input_report));
  hid_input_report::ConsumerControlInputReport* consumer_control_report =
      std::get_if<hid_input_report::ConsumerControlInputReport>(&input_report.report);
  ASSERT_NOT_NULL(consumer_control_report);

  EXPECT_EQ(consumer_control_report->num_pressed_buttons, 3U);
  EXPECT_EQ(consumer_control_report->pressed_buttons[0],
            llcpp::fuchsia::input::report::ConsumerControlButton::VOLUME_UP);
  EXPECT_EQ(consumer_control_report->pressed_buttons[1],
            llcpp::fuchsia::input::report::ConsumerControlButton::REBOOT);
  EXPECT_EQ(consumer_control_report->pressed_buttons[2],
            llcpp::fuchsia::input::report::ConsumerControlButton::MIC_MUTE);
}

TEST(ConsumerControlTest, MaxButtonsTest) {
  constexpr uint32_t kMaxButtons = llcpp::fuchsia::input::report::CONSUMER_CONTROL_MAX_NUM_BUTTONS;

  hid::ReportDescriptor descriptor = {};
  descriptor.input_count = kMaxButtons;
  descriptor.input_byte_sz = kMaxButtons * 8;

  hid::ReportField field = {};
  field.attr.usage.page = static_cast<uint32_t>(hid::usage::Page::kConsumer);
  field.attr.usage.usage = static_cast<uint32_t>(hid::usage::Consumer::kVolumeUp);
  std::vector<hid::ReportField> fields(kMaxButtons, field);

  descriptor.input_fields = fields.data();

  hid_input_report::ConsumerControl consumer_control;
  ASSERT_EQ(hid_input_report::ParseResult::kParseOk,
            consumer_control.ParseReportDescriptor(descriptor));
}

TEST(ConsumerControlTest, OverMaxButtonsTest) {
  constexpr uint32_t kOverMaxButtons =
      1 + llcpp::fuchsia::input::report::CONSUMER_CONTROL_MAX_NUM_BUTTONS;

  hid::ReportDescriptor descriptor = {};
  descriptor.input_count = kOverMaxButtons;
  descriptor.input_byte_sz = kOverMaxButtons * 8;

  hid::ReportField field = {};
  field.attr.usage.page = static_cast<uint32_t>(hid::usage::Page::kConsumer);
  field.attr.usage.usage = static_cast<uint32_t>(hid::usage::Consumer::kVolumeUp);
  std::vector<hid::ReportField> fields(kOverMaxButtons, field);

  descriptor.input_fields = fields.data();

  hid_input_report::ConsumerControl consumer_control;
  ASSERT_EQ(hid_input_report::ParseResult::kParseTooManyItems,
            consumer_control.ParseReportDescriptor(descriptor));
}
