// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/lib/hid-input-report/touch.h"

#include <lib/fit/defer.h>

#include <variant>

#include <hid/atlas-touchpad.h>
#include <hid/paradise.h>
#include <zxtest/zxtest.h>

#include "src/ui/input/lib/hid-input-report/device.h"
#include "src/ui/input/lib/hid-input-report/mouse.h"
#include "src/ui/input/lib/hid-input-report/test/test.h"

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
  auto free_descriptor = fit::defer([hid_desc]() { hid::FreeDeviceDescriptor(hid_desc); });

  hid_input_report::Touch touch;
  EXPECT_EQ(hid_input_report::ParseResult::kOk, touch.ParseReportDescriptor(*hid_report_desc));

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  fuchsia_input_report::wire::DeviceDescriptor descriptor(descriptor_allocator);
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            touch.CreateDescriptor(descriptor_allocator, descriptor));
  EXPECT_TRUE(descriptor.has_touch());
  EXPECT_TRUE(descriptor.touch().has_input());

  EXPECT_EQ(5UL, descriptor.touch().input().contacts().count());

  EXPECT_TRUE(descriptor.touch().input().contacts()[0].has_position_x());
  EXPECT_EQ(0, descriptor.touch().input().contacts()[0].position_x().range.min);
  EXPECT_EQ(259200, descriptor.touch().input().contacts()[0].position_x().range.max);

  EXPECT_TRUE(descriptor.touch().input().contacts()[0].has_position_y());
  EXPECT_EQ(0, descriptor.touch().input().contacts()[0].position_y().range.min);
  EXPECT_EQ(172800, descriptor.touch().input().contacts()[0].position_y().range.max);

  // Now use the parsed descriptor to interpret a touchpad report.
  paradise_touch_t touch_v1_report = {};
  touch_v1_report.rpt_id = 12;
  touch_v1_report.contact_count = 1;
  touch_v1_report.fingers[1].flags = 0xF;
  touch_v1_report.fingers[1].finger_id = 0x1;
  touch_v1_report.fingers[1].x = 100;
  touch_v1_report.fingers[1].y = 200;

  hid_input_report::TestReportAllocator report_allocator;
  fuchsia_input_report::wire::InputReport input_report(report_allocator);
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            touch.ParseInputReport(reinterpret_cast<uint8_t*>(&touch_v1_report),
                                   sizeof(touch_v1_report), report_allocator, input_report));
  ASSERT_TRUE(input_report.has_touch());

  EXPECT_EQ(1UL, input_report.touch().contacts().count());

  // The expected values below have been manually converted from logical to physical units based
  // on the report descriptor.

  EXPECT_TRUE(input_report.touch().contacts()[0].has_contact_id());
  EXPECT_EQ(1U, input_report.touch().contacts()[0].contact_id());

  EXPECT_TRUE(input_report.touch().contacts()[0].has_position_x());
  EXPECT_EQ(2500, input_report.touch().contacts()[0].position_x());

  EXPECT_TRUE(input_report.touch().contacts()[0].has_position_y());
  EXPECT_EQ(5000, input_report.touch().contacts()[0].position_y());
}

TEST(TouchscreenTest, ParadiseV1Touchpad) {
  size_t desc_size;
  const uint8_t* desc = get_paradise_touchpad_v1_report_desc(&desc_size);

  hid::DeviceDescriptor* hid_desc;
  hid::ReportDescriptor* hid_report_desc;
  HidParseTouchscreen(desc, desc_size, &hid_desc, &hid_report_desc);
  auto free_descriptor = fit::defer([hid_desc]() { hid::FreeDeviceDescriptor(hid_desc); });

  hid_input_report::Touch touch;
  EXPECT_EQ(hid_input_report::ParseResult::kOk, touch.ParseReportDescriptor(*hid_report_desc));

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  fuchsia_input_report::wire::DeviceDescriptor descriptor(descriptor_allocator);
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            touch.CreateDescriptor(descriptor_allocator, descriptor));
  EXPECT_TRUE(descriptor.has_touch());
  EXPECT_TRUE(descriptor.touch().has_input());

  EXPECT_EQ(5UL, descriptor.touch().input().contacts().count());

  EXPECT_TRUE(descriptor.touch().input().contacts()[0].has_position_x());
  EXPECT_EQ(0, descriptor.touch().input().contacts()[0].position_x().range.min);
  EXPECT_EQ(103000, descriptor.touch().input().contacts()[0].position_x().range.max);

  EXPECT_TRUE(descriptor.touch().input().contacts()[0].has_position_y());
  EXPECT_EQ(0, descriptor.touch().input().contacts()[0].position_y().range.min);
  EXPECT_EQ(68000, descriptor.touch().input().contacts()[0].position_y().range.max);

  EXPECT_EQ(1, descriptor.touch().input().buttons().count());
  EXPECT_EQ(1, descriptor.touch().input().buttons()[0]);

  // Now use the parsed descriptor to interpret a touchpad report.
  paradise_touchpad_v1_t touch_report = {};
  touch_report.report_id = 1;
  touch_report.button = 1;
  touch_report.contact_count = 1;
  touch_report.fingers[0].tip_switch = 1;
  touch_report.fingers[0].id = 5;
  touch_report.fingers[0].x = 200;
  touch_report.fingers[0].y = 100;

  hid_input_report::TestReportAllocator report_allocator;
  fuchsia_input_report::wire::InputReport input_report(report_allocator);
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            touch.ParseInputReport(reinterpret_cast<uint8_t*>(&touch_report), sizeof(touch_report),
                                   report_allocator, input_report));
  ASSERT_TRUE(input_report.has_touch());

  EXPECT_EQ(1UL, input_report.touch().contacts().count());

  // The expected values below have been manually converted from logical to physical units based
  // on the report descriptor.
  EXPECT_EQ(5U, input_report.touch().contacts()[0].contact_id());
  EXPECT_EQ(1562, input_report.touch().contacts()[0].position_x());
  EXPECT_EQ(781, input_report.touch().contacts()[0].position_y());
  EXPECT_EQ(1, input_report.touch().pressed_buttons().count());
  EXPECT_EQ(1, input_report.touch().pressed_buttons()[0]);
}

TEST(TouchscreenTest, DeviceType) {
  hid_input_report::Touch device;
  ASSERT_EQ(hid_input_report::DeviceType::kTouch, device.GetDeviceType());
}

TEST(TouchscreenTest, AtlasTouchpad) {
  // Create the descriptor.
  hid::DeviceDescriptor* dev_desc = nullptr;
  const uint8_t* desc;
  size_t desc_size = get_atlas_touchpad_report_desc(&desc);
  ASSERT_NOT_NULL(desc);
  hid::ParseResult parse_res = hid::ParseReportDescriptor(desc, desc_size, &dev_desc);
  ASSERT_NOT_NULL(dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);
  auto free_descriptor = fit::defer([dev_desc]() { hid::FreeDeviceDescriptor(dev_desc); });
  ASSERT_EQ(12, dev_desc->rep_count);

  hid_input_report::TestDescriptorAllocator descriptor_allocator;
  fuchsia_input_report::wire::DeviceDescriptor descriptor(descriptor_allocator);

  // Parse mouse descriptor. (Report 0)
  hid_input_report::Mouse mouse;
  EXPECT_EQ(hid_input_report::ParseResult::kOk, mouse.ParseReportDescriptor(dev_desc->report[0]));
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            mouse.CreateDescriptor(descriptor_allocator, descriptor));

  // Report 1-5 skipped. Vendor defined.

  // Parse touch descriptor. (Report 6)
  hid_input_report::Touch touch;
  EXPECT_EQ(hid_input_report::ParseResult::kOk, touch.ParseReportDescriptor(dev_desc->report[6]));
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            touch.CreateDescriptor(descriptor_allocator, descriptor));

  // Report 7-9 are unsupported collections.
  for (size_t i = 7; i < 10; i++) {
    hid_input_report::Touch tmp_touch;
    EXPECT_EQ(hid_input_report::ParseResult::kItemNotFound,
              tmp_touch.ParseReportDescriptor(dev_desc->report[i]));
  }

  // Parse touch descriptor. (Report 10)
  hid_input_report::TouchConfiguration input_mode;
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            input_mode.ParseReportDescriptor(dev_desc->report[10]));
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            input_mode.CreateDescriptor(descriptor_allocator, descriptor));

  // Parse touch descriptor. (Report 11)
  hid_input_report::TouchConfiguration selective_reporting;
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            selective_reporting.ParseReportDescriptor(dev_desc->report[11]));
  EXPECT_EQ(hid_input_report::ParseResult::kOk,
            selective_reporting.CreateDescriptor(descriptor_allocator, descriptor));

  ASSERT_TRUE(descriptor.has_mouse());
  ASSERT_TRUE(descriptor.mouse().has_input());
  EXPECT_TRUE(descriptor.mouse().input().has_buttons());
  EXPECT_EQ(descriptor.mouse().input().buttons().count(), 2);
  ASSERT_TRUE(descriptor.mouse().input().has_movement_x());
  EXPECT_EQ(descriptor.mouse().input().movement_x().range.min, -127);
  EXPECT_EQ(descriptor.mouse().input().movement_x().range.max, 127);
  EXPECT_EQ(descriptor.mouse().input().movement_x().unit.type,
            fuchsia_input_report::wire::UnitType::kNone);
  EXPECT_EQ(descriptor.mouse().input().movement_x().unit.exponent, 0);
  ASSERT_TRUE(descriptor.mouse().input().has_movement_y());
  EXPECT_EQ(descriptor.mouse().input().movement_y().range.min, -127);
  EXPECT_EQ(descriptor.mouse().input().movement_y().range.max, 127);
  EXPECT_EQ(descriptor.mouse().input().movement_x().unit.type,
            fuchsia_input_report::wire::UnitType::kNone);
  EXPECT_EQ(descriptor.mouse().input().movement_y().unit.exponent, 0);

  ASSERT_TRUE(descriptor.has_touch());
  ASSERT_TRUE(descriptor.touch().has_input());
  ASSERT_TRUE(descriptor.touch().input().has_touch_type());
  EXPECT_EQ(descriptor.touch().input().touch_type(),
            fuchsia_input_report::wire::TouchType::kTouchpad);
  ASSERT_TRUE(descriptor.touch().input().has_buttons());
  EXPECT_EQ(descriptor.touch().input().buttons().count(), 1);
  ASSERT_TRUE(descriptor.touch().input().has_contacts());
  ASSERT_EQ(descriptor.touch().input().contacts().count(), 5);
  for (size_t i = 0; i < descriptor.touch().input().contacts().count(); i++) {
    ASSERT_TRUE(descriptor.touch().input().contacts()[i].has_position_x());
    EXPECT_EQ(descriptor.touch().input().contacts()[i].position_x().unit.type,
              fuchsia_input_report::wire::UnitType::kMeters);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].position_x().unit.exponent, -6);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].position_x().range.min, 0);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].position_x().range.max,
              hid::unit::ConvertValToUnitType(
                  hid::Unit{
                      .type = 0x13,
                      .exp = -2,
                  },
                  450));
    ASSERT_TRUE(descriptor.touch().input().contacts()[i].has_position_y());
    EXPECT_EQ(descriptor.touch().input().contacts()[i].position_y().unit.type,
              fuchsia_input_report::wire::UnitType::kMeters);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].position_y().unit.exponent, -6);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].position_y().range.min, 0);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].position_y().range.max,
              hid::unit::ConvertValToUnitType(
                  hid::Unit{
                      .type = 0x13,
                      .exp = -2,
                  },
                  248));
    ASSERT_TRUE(descriptor.touch().input().contacts()[i].has_contact_width());
    EXPECT_EQ(descriptor.touch().input().contacts()[i].contact_width().unit.type,
              fuchsia_input_report::wire::UnitType::kMeters);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].contact_width().unit.exponent, -6);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].contact_width().range.min, 0);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].contact_width().range.max,
              hid::unit::ConvertValToUnitType(
                  hid::Unit{
                      .type = 0x13,
                      .exp = -2,
                  },
                  248));
    ASSERT_TRUE(descriptor.touch().input().contacts()[i].has_contact_height());
    EXPECT_EQ(descriptor.touch().input().contacts()[i].contact_height().unit.type,
              fuchsia_input_report::wire::UnitType::kMeters);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].contact_height().unit.exponent, -6);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].contact_height().range.min, 0);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].contact_height().range.max,
              hid::unit::ConvertValToUnitType(
                  hid::Unit{
                      .type = 0x13,
                      .exp = -2,
                  },
                  248));
    ASSERT_TRUE(descriptor.touch().input().contacts()[i].has_pressure());
    EXPECT_EQ(descriptor.touch().input().contacts()[i].pressure().unit.type,
              fuchsia_input_report::wire::UnitType::kMeters);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].pressure().unit.exponent, -6);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].pressure().range.min, 0);
    EXPECT_EQ(descriptor.touch().input().contacts()[i].pressure().range.max,
              hid::unit::ConvertValToUnitType(
                  hid::Unit{
                      .type = 0x13,
                      .exp = -2,
                  },
                  248));
  }
  ASSERT_TRUE(descriptor.touch().has_feature());
  ASSERT_TRUE(descriptor.touch().feature().has_supports_input_mode());
  EXPECT_EQ(descriptor.touch().feature().supports_input_mode(), true);
  EXPECT_TRUE(descriptor.touch().feature().has_supports_selective_reporting());
  EXPECT_EQ(descriptor.touch().feature().supports_selective_reporting(), true);

  // Parse Input Reports
  {
    // Mouse
    multitouch_mouse_input_rpt_t mouse_data = {};
    // Values are arbitrarily chosen.
    constexpr bool kMouseButton1TestVal = true;
    constexpr bool kMouseButton2TestVal = false;
    constexpr int kMouseXTestVal = 52;
    constexpr int kMouseYTestVal = -4;
    mouse_data.button1 = kMouseButton1TestVal;
    mouse_data.button2 = kMouseButton2TestVal;
    mouse_data.x = kMouseXTestVal;
    mouse_data.y = kMouseYTestVal;

    hid_input_report::TestReportAllocator report_allocator;
    fuchsia_input_report::wire::InputReport input_report(report_allocator);

    EXPECT_EQ(hid_input_report::ParseResult::kOk,
              mouse.ParseInputReport(reinterpret_cast<uint8_t*>(&mouse_data), sizeof(mouse_data),
                                     report_allocator, input_report));

    ASSERT_TRUE(input_report.has_mouse());
    ASSERT_TRUE(input_report.mouse().has_movement_x());
    EXPECT_EQ(kMouseXTestVal, input_report.mouse().movement_x());
    ASSERT_TRUE(input_report.mouse().has_movement_y());
    EXPECT_EQ(kMouseYTestVal, input_report.mouse().movement_y());
    ASSERT_TRUE(input_report.mouse().has_pressed_buttons());
    ASSERT_EQ(input_report.mouse().pressed_buttons().count(), 1);
    EXPECT_EQ(input_report.mouse().pressed_buttons()[0], 1);
  }

  {
    // Touch
    multitouch_touch_input_rpt_t touch_data = {};
    // Values are arbitrarily chosen.
    constexpr bool kTouchButtonTestVal = true;
    constexpr bool kTouchTipSwitchTestVal[5] = {true, false, true, true, false};
    constexpr int kTouchXTestVal[5] = {52, 53, 54, 55, 56};
    constexpr int kTouchYTestVal[5] = {9, 8, 7, 6, 5};
    constexpr int kTouchWidthTestVal[5] = {16, 15, 14, 13, 12};
    constexpr int kTouchHeightTestVal[5] = {85, 86, 87, 88, 89};
    constexpr int kTouchPressureTestVal[5] = {45, 46, 47, 48, 49};
    touch_data.button = kTouchButtonTestVal;
    for (size_t i = 0; i < 5; i++) {
      touch_data.contact[i].tip_switch = kTouchTipSwitchTestVal[i];
      touch_data.contact[i].x = kTouchXTestVal[i];
      touch_data.contact[i].y = kTouchYTestVal[i];
      touch_data.contact[i].width = kTouchWidthTestVal[i];
      touch_data.contact[i].height = kTouchHeightTestVal[i];
      touch_data.contact[i].pressure = kTouchPressureTestVal[i];
    }

    // Parse the report.
    hid_input_report::TestReportAllocator report_allocator;
    fuchsia_input_report::wire::InputReport input_report(report_allocator);

    EXPECT_EQ(hid_input_report::ParseResult::kOk,
              touch.ParseInputReport(reinterpret_cast<uint8_t*>(&touch_data), sizeof(touch_data),
                                     report_allocator, input_report));

    ASSERT_TRUE(input_report.has_touch());
    ASSERT_TRUE(input_report.touch().has_pressed_buttons());
    ASSERT_EQ(input_report.touch().pressed_buttons().count(), 1);
    EXPECT_EQ(input_report.touch().pressed_buttons()[0], 1);
    ASSERT_TRUE(input_report.touch().has_contacts());
    ASSERT_EQ(input_report.touch().contacts().count(), 3);
    size_t tmp_counter = 0;
    for (size_t i = 0; i < 5; i++) {
      if (!kTouchTipSwitchTestVal[i]) {
        continue;
      }

      ASSERT_TRUE(input_report.touch().contacts()[tmp_counter].has_position_x());
      EXPECT_EQ(input_report.touch().contacts()[tmp_counter].position_x(),
                hid::unit::ConvertValToUnitType(
                    hid::Unit{
                        .type = 0x13,
                        .exp = -2,
                    },
                    kTouchXTestVal[i]));
      ASSERT_TRUE(input_report.touch().contacts()[tmp_counter].has_position_y());
      EXPECT_EQ(input_report.touch().contacts()[tmp_counter].position_y(),
                hid::unit::ConvertValToUnitType(
                    hid::Unit{
                        .type = 0x13,
                        .exp = -2,
                    },
                    kTouchYTestVal[i]));
      ASSERT_TRUE(input_report.touch().contacts()[tmp_counter].has_contact_width());
      EXPECT_EQ(input_report.touch().contacts()[tmp_counter].contact_width(),
                hid::unit::ConvertValToUnitType(
                    hid::Unit{
                        .type = 0x13,
                        .exp = -2,
                    },
                    kTouchWidthTestVal[i]));
      ASSERT_TRUE(input_report.touch().contacts()[tmp_counter].has_contact_height());
      EXPECT_EQ(input_report.touch().contacts()[tmp_counter].contact_height(),
                hid::unit::ConvertValToUnitType(
                    hid::Unit{
                        .type = 0x13,
                        .exp = -2,
                    },
                    kTouchHeightTestVal[i]));
      ASSERT_TRUE(input_report.touch().contacts()[tmp_counter].has_pressure());
      EXPECT_EQ(input_report.touch().contacts()[tmp_counter].pressure(),
                hid::unit::ConvertValToUnitType(
                    hid::Unit{
                        .type = 0x13,
                        .exp = -2,
                    },
                    kTouchPressureTestVal[i]));

      tmp_counter++;
    }
  }

  // Parse Feature Reports
  {
    // InputMode
    multitouch_input_mode_rpt_t input_mode_data = {};
    // Values are arbitrarily chosen.
    constexpr uint8_t kInputModeTestVal = 3;
    input_mode_data.input_mode = kInputModeTestVal;

    hid_input_report::TestReportAllocator report_allocator;
    fuchsia_input_report::wire::FeatureReport feature_report(report_allocator);

    EXPECT_EQ(
        hid_input_report::ParseResult::kOk,
        input_mode.ParseFeatureReport(reinterpret_cast<uint8_t*>(&input_mode_data),
                                      sizeof(input_mode_data), report_allocator, feature_report));

    ASSERT_TRUE(feature_report.has_touch());
    ASSERT_TRUE(feature_report.touch().has_input_mode());
    EXPECT_EQ(static_cast<fuchsia_input_report::TouchConfigurationInputMode>(kInputModeTestVal),
              feature_report.touch().input_mode());
  }

  {
    // SelectiveReporting
    multitouch_selective_reporting_rpt_t selective_reporting_data = {};
    // Values are arbitrarily chosen.
    constexpr bool kSelectiveReportingSurfaceSwitchTestVal = true;
    constexpr bool kSelectiveReportingButtonSwitchTestVal = false;
    selective_reporting_data.surface_switch = kSelectiveReportingSurfaceSwitchTestVal;
    selective_reporting_data.button_switch = kSelectiveReportingButtonSwitchTestVal;

    hid_input_report::TestReportAllocator report_allocator;
    fuchsia_input_report::wire::FeatureReport feature_report(report_allocator);

    EXPECT_EQ(hid_input_report::ParseResult::kOk,
              selective_reporting.ParseFeatureReport(
                  reinterpret_cast<uint8_t*>(&selective_reporting_data),
                  sizeof(selective_reporting_data), report_allocator, feature_report));

    ASSERT_TRUE(feature_report.has_touch());
    ASSERT_TRUE(feature_report.touch().has_selective_reporting());
    ASSERT_TRUE(feature_report.touch().selective_reporting().has_surface_switch());
    EXPECT_EQ(kSelectiveReportingSurfaceSwitchTestVal,
              feature_report.touch().selective_reporting().surface_switch());
    ASSERT_TRUE(feature_report.touch().selective_reporting().has_button_switch());
    EXPECT_EQ(kSelectiveReportingButtonSwitchTestVal,
              feature_report.touch().selective_reporting().button_switch());
  }

  // Set Feature Report
  {
    auto kInputModeTestVal = fuchsia_input_report::wire::TouchConfigurationInputMode::
        kWindowsPrecisionTouchpadCollection;
    bool kSurfaceSwitchTestVal = false;
    bool kButtonSwitchTestVal = true;

    hid_input_report::TestReportAllocator report_allocator;
    fuchsia_input_report::wire::FeatureReport feature_report(report_allocator);
    fuchsia_input_report::wire::TouchFeatureReport touch_report(report_allocator);
    touch_report.set_input_mode(kInputModeTestVal);
    fuchsia_input_report::wire::SelectiveReportingFeatureReport selective_reporting_report(
        report_allocator);
    selective_reporting_report.set_surface_switch(kSurfaceSwitchTestVal);
    selective_reporting_report.set_button_switch(kButtonSwitchTestVal);
    touch_report.set_selective_reporting(report_allocator, selective_reporting_report);
    feature_report.set_touch(report_allocator, touch_report);

    size_t out_size;
    multitouch_input_mode_rpt_t input_mode_data = {};
    auto result =
        input_mode.SetFeatureReport(&feature_report, reinterpret_cast<uint8_t*>(&input_mode_data),
                                    sizeof(input_mode_data), &out_size);
    ASSERT_EQ(result, hid_input_report::ParseResult::kOk);
    EXPECT_EQ(out_size, sizeof(input_mode_data));
    // memcpy to avoid alignment issues
    uint16_t input_mode;
    memcpy(&input_mode, reinterpret_cast<uint8_t*>(&input_mode_data) + sizeof(uint8_t),
           sizeof(input_mode));
    EXPECT_EQ(static_cast<uint32_t>(input_mode), static_cast<uint32_t>(kInputModeTestVal));

    multitouch_selective_reporting_rpt_t selective_reporting_data = {};
    result = selective_reporting.SetFeatureReport(
        &feature_report, reinterpret_cast<uint8_t*>(&selective_reporting_data),
        sizeof(selective_reporting_data), &out_size);
    ASSERT_EQ(result, hid_input_report::ParseResult::kOk);
    EXPECT_EQ(out_size, sizeof(selective_reporting_data));
    // memcpy to avoid alignment issues
    uint8_t switches;
    memcpy(&switches, reinterpret_cast<uint8_t*>(&selective_reporting_data) + sizeof(uint8_t),
           sizeof(uint8_t));
    bool surface_switch = switches & 0x1;
    EXPECT_EQ(surface_switch, static_cast<uint16_t>(kSurfaceSwitchTestVal));
    bool button_switch = switches & 0x2;
    EXPECT_EQ(button_switch, static_cast<uint16_t>(kButtonSwitchTestVal));
  }
}
