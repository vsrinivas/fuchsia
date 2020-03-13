// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/input_reader/stylus.h"

#include <fuchsia/ui/input/cpp/fidl.h>

#include <gtest/gtest.h>
#include <hid-parser/parser.h>
#include <hid-parser/usages.h>
#include <hid/acer12.h>
#include <hid/paradise.h>

namespace input {

namespace {
bool usage_is_stylus(hid::Usage usage) {
  using ::hid::usage::Digitizer;
  using ::hid::usage::Page;
  return ((usage == hid::USAGE(Page::kDigitizer, Digitizer::kStylus)) ||
          (usage == hid::USAGE(Page::kDigitizer, Digitizer::kPen)));
}
const hid::ReportDescriptor *get_stylus_descriptor(const hid::DeviceDescriptor *dev_desc) {
  const hid::ReportDescriptor *desc = nullptr;
  for (size_t rep = 0; rep < dev_desc->rep_count; rep++) {
    const hid::ReportDescriptor *tmp_desc = &dev_desc->report[rep];
    // Traverse up the nested collections to the Application collection.
    hid::Collection *collection = tmp_desc->input_fields[0].col;
    while (collection != nullptr) {
      if (collection->type == hid::CollectionType::kApplication) {
        break;
      }
      collection = collection->parent;
    }
    if (!collection) {
      return nullptr;
    }
    if (usage_is_stylus(collection->usage)) {
      desc = tmp_desc;
      break;
    }
  }
  return desc;
}
}  // namespace

// Each test parses the report descriptor for the mouse and then sends one
// report to ensure that it has been parsed correctly.
namespace test {

TEST(StylusTest, Paradise) {
  hid::DeviceDescriptor *dev_desc = nullptr;
  size_t desc_size;
  const uint8_t *paradise_touch_v1_report_desc = get_paradise_touch_report_desc(&desc_size);

  auto parse_res = hid::ParseReportDescriptor(paradise_touch_v1_report_desc, desc_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  auto desc = get_stylus_descriptor(dev_desc);
  ASSERT_NE(nullptr, desc);

  ui_input::Stylus stylus = {};
  ui_input::Device::Descriptor device_descriptor = {};
  bool success = stylus.ParseReportDescriptor(*desc, &device_descriptor);
  ASSERT_TRUE(success);
  ASSERT_TRUE(device_descriptor.has_stylus);
  ASSERT_TRUE(device_descriptor.stylus_descriptor != nullptr);

  EXPECT_EQ(0, device_descriptor.stylus_descriptor->x.range.min);
  EXPECT_EQ(25920, device_descriptor.stylus_descriptor->x.range.max);
  EXPECT_EQ(0, device_descriptor.stylus_descriptor->y.range.min);
  EXPECT_EQ(17280, device_descriptor.stylus_descriptor->y.range.max);

  EXPECT_TRUE(device_descriptor.stylus_descriptor->is_invertible);
  EXPECT_EQ(fuchsia::ui::input::kStylusBarrel, device_descriptor.stylus_descriptor->buttons);

  const uint8_t report_data[20] = {
      0x06,        // Report ID
      0xFF,        // Tip switch, barrel switch, eraser, invert, in_range
      0x34, 0x12,  // X
      0x34, 0x12,  // Y
      0x20, 0x00,  // Tip pressure
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  fuchsia::ui::input::InputReport report;
  report.stylus = fuchsia::ui::input::StylusReport::New();
  success = stylus.ParseReport(report_data, sizeof(report_data), &report);
  ASSERT_EQ(true, success);

  // Multiply by a hundred for the unit standardization
  EXPECT_EQ(0x1234 * 100, report.stylus->x);
  EXPECT_EQ(0x1234 * 100, report.stylus->y);
  EXPECT_EQ(0x20U, report.stylus->pressure);
  EXPECT_EQ(true, report.stylus->is_in_contact);
  EXPECT_EQ(true, report.stylus->is_inverted);
  EXPECT_EQ(fuchsia::ui::input::kStylusBarrel, report.stylus->pressed_buttons);
}

TEST(StylusTest, Acer12) {
  hid::DeviceDescriptor *dev_desc = nullptr;
  size_t desc_size;
  const uint8_t *acer12_touch_report_desc = get_acer12_touch_report_desc(&desc_size);

  auto parse_res = hid::ParseReportDescriptor(acer12_touch_report_desc, desc_size, &dev_desc);
  ASSERT_EQ(hid::ParseResult::kParseOk, parse_res);

  auto desc = get_stylus_descriptor(dev_desc);
  ASSERT_NE(nullptr, desc);

  ui_input::Stylus stylus = {};
  ui_input::Device::Descriptor device_descriptor = {};
  bool success = stylus.ParseReportDescriptor(*desc, &device_descriptor);
  ASSERT_TRUE(success);
  ASSERT_TRUE(device_descriptor.has_stylus);
  ASSERT_TRUE(device_descriptor.stylus_descriptor != nullptr);

  EXPECT_EQ(0, device_descriptor.stylus_descriptor->x.range.min);
  EXPECT_EQ(254, device_descriptor.stylus_descriptor->x.range.max);
  EXPECT_EQ(0, device_descriptor.stylus_descriptor->y.range.min);
  EXPECT_EQ(169, device_descriptor.stylus_descriptor->y.range.max);

  EXPECT_TRUE(device_descriptor.stylus_descriptor->is_invertible);
  EXPECT_EQ(fuchsia::ui::input::kStylusBarrel, device_descriptor.stylus_descriptor->buttons);

  const uint8_t report_data[8] = {
      0x07,        // Report ID
      0xFF,        // Tip switch, barrel switch, eraser, invert, in_range
      0x23, 0x01,  // X
      0x23, 0x01,  // Y
      0x20, 0x00,  // Tip pressure
  };

  fuchsia::ui::input::InputReport report;
  report.stylus = fuchsia::ui::input::StylusReport::New();
  success = stylus.ParseReport(report_data, sizeof(report_data), &report);
  ASSERT_EQ(true, success);

  // Manually calculated logical -> physical units
  EXPECT_EQ(183318, report.stylus->x);
  EXPECT_EQ(178702, report.stylus->y);
  EXPECT_EQ(0x20U, report.stylus->pressure);
  EXPECT_EQ(true, report.stylus->is_in_contact);
  EXPECT_EQ(true, report.stylus->is_inverted);
  EXPECT_EQ(fuchsia::ui::input::kStylusBarrel, report.stylus->pressed_buttons);
}

}  // namespace test
}  // namespace input
