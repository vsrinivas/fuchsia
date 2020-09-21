// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <variant>

#include <ddk/metadata/buttons.h>
#include <hid/buttons.h>
#include <zxtest/zxtest.h>

#include "src/ui/input/lib/hid-input-report/consumer_control.h"
#include "src/ui/input/lib/hid-input-report/device.h"
#include "src/ui/input/lib/hid-input-report/test/test.h"

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

TEST(ConsumerControlTest, HidButtonsTest) {
  const uint8_t* descriptor_data;
  size_t descriptor_size = get_buttons_report_desc(&descriptor_data);

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(descriptor_data, descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  hid_input_report::ConsumerControl consumer_control;
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            consumer_control.ParseReportDescriptor(dev_desc->report[0]));

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  auto descriptor_builder = fuchsia_input_report::DeviceDescriptor::Builder(
      descriptor_allocator.make<fuchsia_input_report::DeviceDescriptor::Frame>());
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            consumer_control.CreateDescriptor(&descriptor_allocator, &descriptor_builder));
  fuchsia_input_report::DeviceDescriptor descriptor = descriptor_builder.build();
  EXPECT_TRUE(descriptor.has_consumer_control());
  EXPECT_TRUE(descriptor.consumer_control().has_input());

  // Test the descriptor.
  EXPECT_EQ(descriptor.consumer_control().input().buttons().count(), 5U);
  EXPECT_EQ(descriptor.consumer_control().input().buttons()[0],
            llcpp::fuchsia::input::report::ConsumerControlButton::VOLUME_UP);
  EXPECT_EQ(descriptor.consumer_control().input().buttons()[1],
            llcpp::fuchsia::input::report::ConsumerControlButton::VOLUME_DOWN);
  EXPECT_EQ(descriptor.consumer_control().input().buttons()[2],
            llcpp::fuchsia::input::report::ConsumerControlButton::REBOOT);
  EXPECT_EQ(descriptor.consumer_control().input().buttons()[3],
            llcpp::fuchsia::input::report::ConsumerControlButton::CAMERA_DISABLE);
  EXPECT_EQ(descriptor.consumer_control().input().buttons()[4],
            llcpp::fuchsia::input::report::ConsumerControlButton::MIC_MUTE);

  // Test a report parses correctly.
  struct buttons_input_rpt report = {};
  report.rpt_id = BUTTONS_RPT_ID_INPUT;
  fill_button_in_report(BUTTONS_ID_VOLUME_UP, true, &report);
  fill_button_in_report(BUTTONS_ID_FDR, true, &report);
  fill_button_in_report(BUTTONS_ID_MIC_MUTE, true, &report);

  hid_input_report::TestReportAllocator report_allocator;
  auto report_builder = fuchsia_input_report::InputReport::Builder(
      report_allocator.make<fuchsia_input_report::InputReport::Frame>());
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            consumer_control.ParseInputReport(reinterpret_cast<uint8_t*>(&report), sizeof(report),
                                              &report_allocator, &report_builder));

  fuchsia_input_report::InputReport input_report = report_builder.build();

  EXPECT_EQ(input_report.consumer_control().pressed_buttons().count(), 3U);
  EXPECT_EQ(input_report.consumer_control().pressed_buttons()[0],
            llcpp::fuchsia::input::report::ConsumerControlButton::VOLUME_UP);
  EXPECT_EQ(input_report.consumer_control().pressed_buttons()[1],
            llcpp::fuchsia::input::report::ConsumerControlButton::REBOOT);
  EXPECT_EQ(input_report.consumer_control().pressed_buttons()[2],
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
  ASSERT_EQ(hid_input_report::ParseResult::kOk, consumer_control.ParseReportDescriptor(descriptor));
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
  ASSERT_EQ(hid_input_report::ParseResult::kTooManyItems,
            consumer_control.ParseReportDescriptor(descriptor));
}

TEST(ConsumerControlTest, DeviceType) {
  hid_input_report::ConsumerControl device;
  ASSERT_EQ(hid_input_report::DeviceType::kConsumerControl, device.GetDeviceType());
}
