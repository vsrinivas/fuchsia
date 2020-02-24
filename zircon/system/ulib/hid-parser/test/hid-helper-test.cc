// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <hid-parser/item.h>
#include <hid-parser/parser.h>
#include <hid-parser/report.h>
#include <hid-parser/units.h>
#include <hid-parser/usages.h>
#include <zxtest/zxtest.h>

// See hid-utest-data.cpp for the definitions of the test data
extern "C" const uint8_t push_pop_test[70];
extern "C" const uint8_t minmax_signed_test[68];
extern "C" const uint8_t report_count_oom_test[31];

TEST(HidParserHelperTest, ParseEmptyData) {
  hid::DeviceDescriptor* dev = nullptr;
  uint8_t data[] = {0};
  auto res = hid::ParseReportDescriptor(data, sizeof(data), &dev);
  ASSERT_EQ(res, hid::ParseResult::kParseInvalidTag);

  res = hid::ParseReportDescriptor(nullptr, 0, &dev);
  ASSERT_EQ(res, hid::ParseResult::kParseMoreNeeded);

  res = hid::ParseReportDescriptor(data, 0, &dev);
  ASSERT_EQ(res, hid::ParseResult::kParseMoreNeeded);
}

// Tests that the max values of the MinMax are parsed as unsigned
// data when the min values are >= 0. Also tests that the max values
// are parsed as signed data when the min values are < 0.
TEST(HidParserHelperTest, ParseMinmaxSigned) {
  hid::DeviceDescriptor* dev = nullptr;
  auto res = hid::ParseReportDescriptor(minmax_signed_test, sizeof(minmax_signed_test), &dev);
  ASSERT_EQ(res, hid::ParseResult::kParseOk);

  const auto fields = dev->report[0].input_fields;

  EXPECT_EQ(fields[0].attr.logc_mm.min, 0);
  EXPECT_EQ(fields[0].attr.logc_mm.max, 0xFF);
  EXPECT_EQ(fields[0].attr.phys_mm.min, 0);
  EXPECT_EQ(fields[0].attr.phys_mm.max, 0xFFFF);

  EXPECT_EQ(fields[1].attr.logc_mm.min, -5);
  EXPECT_EQ(fields[1].attr.logc_mm.max, -1);
  EXPECT_EQ(fields[1].attr.phys_mm.min, -5);
  EXPECT_EQ(fields[1].attr.phys_mm.max, -1);
  hid::FreeDeviceDescriptor(dev);
}

// Test that the push and pop operations complete successfully.
// Pushing saves all of the GLOBAL items.
// Popping restores the previously saved GLOBAL items
TEST(HidParserHelperTest, ParsePushPop) {
  hid::DeviceDescriptor* dev = nullptr;
  auto res = hid::ParseReportDescriptor(push_pop_test, sizeof(push_pop_test), &dev);
  ASSERT_EQ(res, hid::ParseResult::kParseOk);

  // A single report with id zero, this means no report id.
  ASSERT_EQ(dev->rep_count, 1u);
  EXPECT_EQ(dev->report[0].report_id, 0);

  // The only report has 12 fields.
  EXPECT_EQ(dev->report[0].input_count, 12);
  const auto fields = dev->report[0].input_fields;

  // All fields are input type with report id = 0.
  for (uint8_t ix = 0; ix != dev->report[0].input_count; ++ix) {
    EXPECT_EQ(fields[ix].report_id, 0);
    EXPECT_EQ(fields[ix].type, hid::kInput);
  }

  // First 3 fields are the buttons, with usages 1, 2, 3, in the button page.
  auto expected_flags = hid::kData | hid::kAbsolute | hid::kScalar;

  for (uint8_t ix = 0; ix != 3; ++ix) {
    EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kButton);
    EXPECT_EQ(fields[ix].attr.usage.usage, uint32_t(ix + 1));
    EXPECT_EQ(fields[ix].attr.bit_sz, 1);
    EXPECT_EQ(fields[ix].attr.logc_mm.min, 0);
    EXPECT_EQ(fields[ix].attr.logc_mm.max, 1);
    EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
  }

  // Next field is 5 bits constant. Aka padding.
  EXPECT_EQ(fields[3].attr.bit_sz, 5);
  EXPECT_EQ(hid::kConstant & fields[3].flags, hid::kConstant);

  // Next comes 'X' field, 8 bits data, relative.
  expected_flags = hid::kData | hid::kRelative | hid::kScalar;

  EXPECT_EQ(fields[4].attr.usage.page, hid::usage::Page::kGenericDesktop);
  EXPECT_EQ(fields[4].attr.usage.usage, hid::usage::GenericDesktop::kX);
  EXPECT_EQ(fields[4].attr.bit_sz, 8);
  EXPECT_EQ(fields[4].attr.logc_mm.min, -127);
  EXPECT_EQ(fields[4].attr.logc_mm.max, 127);
  EXPECT_EQ(fields[4].attr.phys_mm.min, -127);
  EXPECT_EQ(fields[4].attr.phys_mm.max, 127);
  EXPECT_EQ(expected_flags & fields[4].flags, expected_flags);

  // Next comes 'Y' field, same as 'X'.
  EXPECT_EQ(fields[5].attr.usage.page, hid::usage::Page::kGenericDesktop);
  EXPECT_EQ(fields[5].attr.usage.usage, hid::usage::GenericDesktop::kY);
  EXPECT_EQ(fields[5].attr.bit_sz, 8);
  EXPECT_EQ(fields[5].attr.logc_mm.min, -127);
  EXPECT_EQ(fields[5].attr.logc_mm.max, 127);
  EXPECT_EQ(fields[5].attr.phys_mm.min, -127);
  EXPECT_EQ(fields[5].attr.phys_mm.max, 127);
  EXPECT_EQ(expected_flags & fields[4].flags, expected_flags);

  // Next comes 'X' and 'Y' field again
  expected_flags = hid::kData | hid::kRelative | hid::kScalar;
  EXPECT_EQ(fields[6].attr.usage.page, hid::usage::Page::kGenericDesktop);
  EXPECT_EQ(fields[6].attr.usage.usage, hid::usage::GenericDesktop::kX);
  EXPECT_EQ(fields[6].attr.bit_sz, 8);
  EXPECT_EQ(fields[6].attr.logc_mm.min, -127);
  EXPECT_EQ(fields[6].attr.logc_mm.max, 127);
  EXPECT_EQ(fields[6].attr.phys_mm.min, -127);
  EXPECT_EQ(fields[6].attr.phys_mm.max, 127);
  EXPECT_EQ(expected_flags & fields[6].flags, expected_flags);
  EXPECT_EQ(fields[7].attr.usage.page, hid::usage::Page::kGenericDesktop);
  EXPECT_EQ(fields[7].attr.usage.usage, hid::usage::GenericDesktop::kY);
  EXPECT_EQ(fields[7].attr.bit_sz, 8);
  EXPECT_EQ(fields[7].attr.logc_mm.min, -127);
  EXPECT_EQ(fields[7].attr.logc_mm.max, 127);
  EXPECT_EQ(fields[7].attr.phys_mm.min, -127);
  EXPECT_EQ(fields[7].attr.phys_mm.max, 127);
  EXPECT_EQ(expected_flags & fields[7].flags, expected_flags);

  // Next is the popped padding field
  EXPECT_EQ(fields[8].attr.bit_sz, 5);
  EXPECT_EQ(hid::kConstant & fields[8].flags, hid::kConstant);

  // Last 3 fields are the popped buttons
  expected_flags = hid::kData | hid::kAbsolute | hid::kScalar;

  for (uint8_t ix = 9; ix != 12; ++ix) {
    EXPECT_EQ(fields[ix].attr.usage.page, hid::usage::Page::kButton);
    EXPECT_EQ(fields[ix].attr.usage.usage, ix - 8);
    EXPECT_EQ(fields[ix].attr.bit_sz, 1);
    EXPECT_EQ(fields[ix].attr.logc_mm.min, 0);
    EXPECT_EQ(fields[ix].attr.logc_mm.max, 1);
    EXPECT_EQ(expected_flags & fields[ix].flags, expected_flags);
  }
  hid::FreeDeviceDescriptor(dev);
}

TEST(HidParserHelperTest, UsageHelper) {
  auto usage = hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kContactID);
  EXPECT_EQ(usage.page, static_cast<uint16_t>(hid::usage::Page::kDigitizer));
  EXPECT_EQ(usage.usage, static_cast<uint32_t>(hid::usage::Digitizer::kContactID));
}

TEST(HidParserHelperTest, MinMaxOperators) {
  EXPECT_TRUE((hid::MinMax{-1, 1} == hid::MinMax{-1, 1}));
  EXPECT_FALSE((hid::MinMax{0, 1} == hid::MinMax{-1, 1}));
  EXPECT_FALSE((hid::MinMax{-1, 1} == hid::MinMax{0, 1}));
  EXPECT_FALSE((hid::MinMax{-1, 2} == hid::MinMax{-1, 1}));
  EXPECT_FALSE((hid::MinMax{-1, 1} == hid::MinMax{-1, 2}));
  EXPECT_FALSE((hid::MinMax{0, 2} == hid::MinMax{-1, 1}));
}

TEST(HidParserHelperTest, UsageOperators) {
  EXPECT_TRUE(hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kContactID) ==
              hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kContactID));
  EXPECT_FALSE(hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kTipSwitch) ==
               hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kContactID));
  EXPECT_FALSE(hid::USAGE(hid::usage::Page::kGenericDesktop, hid::usage::GenericDesktop::kX) ==
               hid::USAGE(hid::usage::Page::kDigitizer, hid::usage::Digitizer::kContactID));
}

TEST(HidParserHelperTest, ExtractTests) {
  uint8_t report[] = {0x0F, 0x0F, 0x0F, 0x0F, 0x0F};
  size_t report_len = sizeof(report);
  hid::Attributes attr;
  bool ret;

  uint8_t int8;
  attr.offset = 0;
  attr.bit_sz = 8;
  ret = ExtractUint(report, report_len, attr, &int8);
  EXPECT_TRUE(ret);
  EXPECT_EQ(int8, 0x0F);

  attr.offset = 2;
  attr.bit_sz = 6;
  ret = ExtractUint(report, report_len, attr, &int8);
  EXPECT_TRUE(ret);
  EXPECT_EQ(int8, 0x03);

  attr.offset = 3;
  attr.bit_sz = 2;
  ret = ExtractUint(report, report_len, attr, &int8);
  EXPECT_TRUE(ret);
  EXPECT_EQ(int8, 0x01);

  // Test over a byte boundary
  attr.offset = 4;
  attr.bit_sz = 8;
  ret = ExtractUint(report, report_len, attr, &int8);
  EXPECT_TRUE(ret);
  EXPECT_EQ(int8, 0xF0);

  uint16_t int16;
  attr.offset = 0;
  attr.bit_sz = 16;
  ret = ExtractUint(report, report_len, attr, &int16);
  EXPECT_TRUE(ret);
  EXPECT_EQ(int16, 0x0F0F);

  attr.offset = 4;
  attr.bit_sz = 16;
  ret = ExtractUint(report, report_len, attr, &int16);
  EXPECT_TRUE(ret);
  EXPECT_EQ(int16, 0xF0F0);

  uint32_t int32;
  attr.offset = 0;
  attr.bit_sz = 32;
  ret = ExtractUint(report, report_len, attr, &int32);
  EXPECT_TRUE(ret);
  EXPECT_EQ(int32, 0x0F0F0F0F);

  attr.offset = 4;
  attr.bit_sz = 32;
  ret = ExtractUint(report, report_len, attr, &int32);
  EXPECT_TRUE(ret);
  EXPECT_EQ(int32, 0xF0F0F0F0);

  // Test that it fails if the attr is too large
  attr.offset = 0;
  attr.bit_sz = 9;
  ret = ExtractUint(report, report_len, attr, &int8);
  EXPECT_FALSE(ret);

  // Test that it fails if it goes past the end of the report
  attr.offset = 36;
  attr.bit_sz = 16;
  ret = ExtractUint(report, report_len, attr, &int16);
  EXPECT_FALSE(ret);
}

TEST(HidParserHelperTest, ExtractAsUnitTests) {
  uint8_t report[] = {0x0F, 10, 0x0F, 0x0F, 0x0F};
  size_t report_len = sizeof(report);
  hid::Attributes attr = {};
  bool ret;
  double val_out;

  // Test a signed conversion with the same logc / physical max
  attr.offset = 0;
  attr.bit_sz = 8;

  attr.logc_mm.max = 100;
  attr.logc_mm.min = -100;
  attr.phys_mm.max = 100;
  attr.phys_mm.min = -100;

  ret = ExtractAsUnit(report, report_len, attr, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 0x0F);

  // Test that a signed conversion actually sign extends
  attr.offset = 0;
  attr.bit_sz = 4;

  attr.logc_mm.max = 10;
  attr.logc_mm.min = -10;
  attr.phys_mm.max = 10;
  attr.phys_mm.min = -10;

  ret = ExtractAsUnit(report, report_len, attr, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), -1);

  // Test an unsigned conversion
  attr.offset = 0;
  attr.bit_sz = 4;

  attr.logc_mm.max = 100;
  attr.logc_mm.min = 0;
  attr.phys_mm.max = 100;
  attr.phys_mm.min = 0;

  ret = ExtractAsUnit(report, report_len, attr, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<uint32_t>(val_out), 0xF);

  // Test a signed conversion with a negative number that x3 the number
  attr.offset = 0;
  attr.bit_sz = 4;

  attr.logc_mm.max = 10;
  attr.logc_mm.min = -10;
  attr.phys_mm.max = 30;
  attr.phys_mm.min = -30;

  ret = ExtractAsUnit(report, report_len, attr, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), -3);

  // Test an unsigned conversion with a negative number that x2.5 the number
  attr.offset = 8;
  attr.bit_sz = 8;

  attr.logc_mm.max = 10;
  attr.logc_mm.min = 0;
  attr.phys_mm.max = 25;
  attr.phys_mm.min = 0;

  ret = ExtractAsUnit(report, report_len, attr, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 25);

  // Test that when phys max and min are 0, there is no scaling
  attr.offset = 8;
  attr.bit_sz = 8;

  attr.logc_mm.max = 100;
  attr.logc_mm.min = 0;
  attr.phys_mm.max = 0;
  attr.phys_mm.min = 0;

  ret = ExtractAsUnit(report, report_len, attr, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 10);

  // Test ExtractWithUnit
  attr.offset = 8;
  attr.bit_sz = 8;

  attr.logc_mm.max = 10;
  attr.logc_mm.min = 0;
  attr.phys_mm.max = 25;
  attr.phys_mm.min = 0;

  // 25 * 10^0 cm = 250 * 10^-1 cm
  hid::Unit unit_in;
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_in, 1);
  unit_in.exp = 0;

  hid::Unit unit_out;
  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_out, 1);
  unit_out.exp = -1;

  attr.unit = unit_in;

  ret = ExtractWithUnit(report, report_len, attr, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 250);
}

TEST(HidParserHelperTest, UnitTests) {
  hid::Unit unit_in;
  hid::Unit unit_out;
  bool ret;
  double val_out;

  // Test the unit type setting/getting.
  {
    unit_in.type = 0;
    hid::unit::SetSystem(unit_in, hid::unit::System::si_linear);
    hid::unit::SetLengthExp(unit_in, 2);
    hid::unit::SetMassExp(unit_in, 3);
    hid::unit::SetTimeExp(unit_in, 7);
    hid::unit::SetTemperatureExp(unit_in, -1);
    hid::unit::SetCurrentExp(unit_in, -2);
    hid::unit::SetLuminousExp(unit_in, -8);

    auto sys = hid::unit::GetSystem(unit_in);
    EXPECT_EQ(sys, hid::unit::System::si_linear);

    int exp = hid::unit::GetLengthExp(unit_in);
    EXPECT_EQ(exp, 2);
    exp = hid::unit::GetMassExp(unit_in);
    EXPECT_EQ(exp, 3);
    exp = hid::unit::GetTimeExp(unit_in);
    EXPECT_EQ(exp, 7);
    exp = hid::unit::GetTemperatureExp(unit_in);
    EXPECT_EQ(exp, -1);
    exp = hid::unit::GetCurrentExp(unit_in);
    EXPECT_EQ(exp, -2);
    exp = hid::unit::GetLuminousExp(unit_in);
    EXPECT_EQ(exp, -8);
  }

  // Test same units convert to lower exponent.
  // 1 * 10^0 cm = 100 * 10^-1 cm
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_in, 1);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_out, 1);
  unit_out.exp = -2;

  ret = hid::unit::ConvertUnits(unit_in, 1, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 100);

  // Test same units convert to higher exponent.
  // 100 * 10^0 cm = 1 * 10^2 cm
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_in, 1);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_out, 1);
  unit_out.exp = 2;

  ret = hid::unit::ConvertUnits(unit_in, 100, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 1);

  // Distance Conversion Tests.

  // 100 * 10^1 inches == 25 * 10^2 cm
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::eng_linear);
  hid::unit::SetLengthExp(unit_in, 1);
  unit_in.exp = 1;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_out, 1);
  unit_out.exp = 2;

  ret = hid::unit::ConvertUnits(unit_in, 100, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 25);

  // 1 * 10^2 cm == 39 * 10^0 in
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_in, 1);
  unit_in.exp = 2;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::eng_linear);
  hid::unit::SetLengthExp(unit_out, 1);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 1, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 39);

  // 100 * 10^0 cm^3 == 6 * 10^0 in^3
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_in, 3);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::eng_linear);
  hid::unit::SetLengthExp(unit_out, 3);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 100, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 6);

  // 1 * 10^0 in^3 == 16 * 10^0 cm^3
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::eng_linear);
  hid::unit::SetLengthExp(unit_in, 3);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_out, 3);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 1, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 16);

  // 180 degrees = 3 radians
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::eng_rotation);
  hid::unit::SetLengthExp(unit_in, 1);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_rotation);
  hid::unit::SetLengthExp(unit_out, 1);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 180, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 3);

  // 3 radians = 171 degrees
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::si_rotation);
  hid::unit::SetLengthExp(unit_in, 1);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::eng_rotation);
  hid::unit::SetLengthExp(unit_out, 1);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 3, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 171);

  // Mass Converson Tests.
  // 1 slug = 14593 grams
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::eng_linear);
  hid::unit::SetMassExp(unit_in, 1);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetMassExp(unit_out, 1);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 1, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 14593);

  // 200000 grams = 13 slugs
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::si_linear);
  hid::unit::SetMassExp(unit_in, 1);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::eng_linear);
  hid::unit::SetMassExp(unit_out, 1);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 200000, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 13);

  // Temperature Conversion Tests.
  // 32 F = 273 K
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::eng_linear);
  hid::unit::SetTemperatureExp(unit_in, 1);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetTemperatureExp(unit_out, 1);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 32, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 273);

  // 273 K = 31 F
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::si_linear);
  hid::unit::SetTemperatureExp(unit_in, 1);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::eng_linear);
  hid::unit::SetTemperatureExp(unit_out, 1);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 273, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 31);

  // Misc Tests.
  // SlugUnits to Newtons (Force conversion).
  // 100 * 10^0 slug * in / seconds^2 == 37 * 10^5 g * cm / seconds^2
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::eng_linear);
  hid::unit::SetMassExp(unit_in, 1);
  hid::unit::SetLengthExp(unit_in, 1);
  hid::unit::SetTimeExp(unit_in, -2);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetMassExp(unit_out, 1);
  hid::unit::SetLengthExp(unit_out, 1);
  hid::unit::SetTimeExp(unit_out, -2);
  unit_out.exp = 5;

  ret = hid::unit::ConvertUnits(unit_in, 100, unit_out, &val_out);
  EXPECT_TRUE(ret);
  EXPECT_EQ(static_cast<int32_t>(val_out), 37);

  // Failure Tests.
  // Can't convert between different units.
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::eng_linear);
  hid::unit::SetMassExp(unit_in, 1);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetMassExp(unit_out, 2);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 1, unit_out, &val_out);
  EXPECT_FALSE(ret);

  // Can't convert between rotation and linear distance.
  unit_in.type = 0;
  hid::unit::SetSystem(unit_in, hid::unit::System::eng_rotation);
  hid::unit::SetLengthExp(unit_in, 1);
  unit_in.exp = 0;

  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_out, 1);
  unit_out.exp = 0;

  ret = hid::unit::ConvertUnits(unit_in, 1, unit_out, &val_out);
  EXPECT_FALSE(ret);
}

TEST(HidParserHelperTest, InsertTests) {
  uint8_t report[8] = {};
  size_t report_len = sizeof(report);
  hid::Attributes attr = {};
  uint32_t value_out;

  attr.bit_sz = 1;
  attr.offset = 0;
  ASSERT_TRUE(InsertUint(report, report_len, attr, 0xFFFFFFFF));
  ASSERT_TRUE(ExtractUint(report, report_len, attr, &value_out));
  EXPECT_EQ(0x1, value_out);
  for (int i = 0; i < 8; i++) {
    report[i] = 0;
  }

  attr.bit_sz = 4;
  attr.offset = 0;
  ASSERT_TRUE(InsertUint(report, report_len, attr, 0xFFFFFFFF));
  ASSERT_TRUE(ExtractUint(report, report_len, attr, &value_out));
  EXPECT_EQ(0xF, value_out);
  for (int i = 0; i < 8; i++) {
    report[i] = 0;
  }

  attr.bit_sz = 8;
  attr.offset = 4;
  ASSERT_TRUE(InsertUint(report, report_len, attr, 0xFFFFFFFF));
  ASSERT_TRUE(ExtractUint(report, report_len, attr, &value_out));
  EXPECT_EQ(0xF0, report[0]);
  EXPECT_EQ(0x0F, report[1]);
  EXPECT_EQ(0xFF, value_out);

  attr.bit_sz = 32;
  attr.offset = 0;
  ASSERT_TRUE(InsertUint(report, report_len, attr, 0xFFFFFFFF));
  ASSERT_TRUE(ExtractUint(report, report_len, attr, &value_out));
  EXPECT_EQ(0xFFFFFFFF, value_out);

  attr.bit_sz = 32;
  attr.offset = 0;
  ASSERT_TRUE(InsertUint(report, report_len, attr, 0x12345678));
  ASSERT_TRUE(ExtractUint(report, report_len, attr, &value_out));
  EXPECT_EQ(0x12345678, value_out);

  attr.bit_sz = 16;
  attr.offset = 8;
  ASSERT_TRUE(InsertUint(report, report_len, attr, 0x12345678));
  ASSERT_TRUE(ExtractUint(report, report_len, attr, &value_out));
  EXPECT_EQ(0x5678, value_out);

  attr.bit_sz = 16;
  attr.offset = 3;
  ASSERT_TRUE(InsertUint(report, report_len, attr, 0x12345678));
  ASSERT_TRUE(ExtractUint(report, report_len, attr, &value_out));
  EXPECT_EQ(0x5678, value_out);

  // Test that Insert and Extract give back the same number.
  double double_out;
  attr.logc_mm.min = 0;
  attr.logc_mm.max = 200;
  attr.phys_mm.min = 0;
  attr.phys_mm.max = 200;
  attr.offset = 5;
  attr.bit_sz = 8;
  ASSERT_TRUE(InsertAsUnit(report, report_len, attr, 100));
  ASSERT_TRUE(ExtractAsUnit(report, report_len, attr, &double_out));
  EXPECT_EQ(static_cast<int>(double_out), 100);

  // Test that Insert and Extract give back the same number with scaling.
  attr.logc_mm.min = 0;
  attr.logc_mm.max = 100;
  attr.phys_mm.min = 0;
  attr.phys_mm.max = 200;
  attr.offset = 5;
  attr.bit_sz = 8;
  ASSERT_TRUE(InsertAsUnit(report, report_len, attr, 100));
  ASSERT_TRUE(ExtractAsUnit(report, report_len, attr, &double_out));
  EXPECT_EQ(static_cast<int>(double_out), 100);

  // Test that Insert and Extract accept negative numbers.
  attr.logc_mm.min = -50;
  attr.logc_mm.max = 50;
  attr.phys_mm.min = -50;
  attr.phys_mm.max = 50;
  attr.offset = 5;
  attr.bit_sz = 8;
  ASSERT_TRUE(InsertAsUnit(report, report_len, attr, -5));
  ASSERT_TRUE(ExtractAsUnit(report, report_len, attr, &double_out));
  EXPECT_EQ(static_cast<int>(double_out), -5);

  // Test InsertWithUnit
  attr.logc_mm.min = 0;
  attr.logc_mm.max = 256;
  attr.phys_mm.min = 0;
  attr.phys_mm.max = 256;
  attr.offset = 5;
  attr.bit_sz = 8;

  hid::Unit unit_out;
  unit_out.type = 0;
  hid::unit::SetSystem(unit_out, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_out, 1);
  unit_out.exp = 2;

  hid::Unit unit_report;
  unit_report.type = 0;
  hid::unit::SetSystem(unit_report, hid::unit::System::si_linear);
  hid::unit::SetLengthExp(unit_report, 1);
  unit_report.exp = 1;

  attr.unit = unit_report;

  ASSERT_TRUE(InsertWithUnit(report, report_len, attr, unit_out, 20));
  ASSERT_TRUE(ExtractWithUnit(report, report_len, attr, unit_out, &double_out));
  EXPECT_EQ(static_cast<int>(double_out), 20);
}

TEST(HidParserHelperTest, ReportCountOomTest) {
  hid::DeviceDescriptor* dev = nullptr;
  auto res = hid::ParseReportDescriptor(report_count_oom_test, sizeof(report_count_oom_test), &dev);
  ASSERT_EQ(res, hid::ParseResult::kParseNoMemory);
}
