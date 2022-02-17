// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>

#include <variant>
#include <vector>

#include <ddk/metadata/buttons.h>
#include <hid/buttons.h>
#include <zxtest/zxtest.h>

#include "src/ui/input/lib/hid-input-report/consumer_control.h"
#include "src/ui/input/lib/hid-input-report/device.h"
#include "src/ui/input/lib/hid-input-report/test/test.h"

TEST(ConsumerControlTest, HidButtonsTest) {
  const uint8_t* descriptor_data;
  size_t descriptor_size = get_buttons_report_desc(&descriptor_data);

  hid::DeviceDescriptor* dev_desc = nullptr;
  auto parse_res = hid::ParseReportDescriptor(descriptor_data, descriptor_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);
  auto free_descriptor = fit::defer([dev_desc]() { hid::FreeDeviceDescriptor(dev_desc); });

  hid_input_report::ConsumerControl consumer_control;
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            consumer_control.ParseReportDescriptor(dev_desc->report[0]));

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  fuchsia_input_report::wire::DeviceDescriptor descriptor(descriptor_allocator);
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            consumer_control.CreateDescriptor(descriptor_allocator, descriptor));
  EXPECT_TRUE(descriptor.has_consumer_control());
  EXPECT_TRUE(descriptor.consumer_control().has_input());

  // Test the descriptor.
  EXPECT_EQ(descriptor.consumer_control().input().buttons().count(), 5U);
  EXPECT_EQ(descriptor.consumer_control().input().buttons()[0],
            fuchsia_input_report::wire::ConsumerControlButton::kVolumeUp);
  EXPECT_EQ(descriptor.consumer_control().input().buttons()[1],
            fuchsia_input_report::wire::ConsumerControlButton::kVolumeDown);
  EXPECT_EQ(descriptor.consumer_control().input().buttons()[2],
            fuchsia_input_report::wire::ConsumerControlButton::kFactoryReset);
  EXPECT_EQ(descriptor.consumer_control().input().buttons()[3],
            fuchsia_input_report::wire::ConsumerControlButton::kCameraDisable);
  EXPECT_EQ(descriptor.consumer_control().input().buttons()[4],
            fuchsia_input_report::wire::ConsumerControlButton::kMicMute);

  // Test a report parses correctly.
  struct buttons_input_rpt report = {};
  report.rpt_id = BUTTONS_RPT_ID_INPUT;
  fill_button_in_report(BUTTONS_ID_VOLUME_UP, true, &report);
  fill_button_in_report(BUTTONS_ID_FDR, true, &report);
  fill_button_in_report(BUTTONS_ID_MIC_MUTE, true, &report);

  hid_input_report::TestReportAllocator report_allocator;
  fuchsia_input_report::wire::InputReport input_report(report_allocator);
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            consumer_control.ParseInputReport(reinterpret_cast<uint8_t*>(&report), sizeof(report),
                                              report_allocator, input_report));

  EXPECT_EQ(input_report.consumer_control().pressed_buttons().count(), 3U);
  EXPECT_EQ(input_report.consumer_control().pressed_buttons()[0],
            fuchsia_input_report::wire::ConsumerControlButton::kVolumeUp);
  EXPECT_EQ(input_report.consumer_control().pressed_buttons()[1],
            fuchsia_input_report::wire::ConsumerControlButton::kFactoryReset);
  EXPECT_EQ(input_report.consumer_control().pressed_buttons()[2],
            fuchsia_input_report::wire::ConsumerControlButton::kMicMute);
}

TEST(ConsumerControlTest, MaxButtonsTest) {
  constexpr uint32_t kMaxButtons = fuchsia_input_report::wire::kConsumerControlMaxNumButtons;

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
      1 + fuchsia_input_report::wire::kConsumerControlMaxNumButtons;

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
