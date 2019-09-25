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

// This should contain all unit types except None and Other.
constexpr hid::unit::UnitType units[] = {
    hid::unit::UnitType::Distance,       hid::unit::UnitType::Weight,
    hid::unit::UnitType::Rotation,       hid::unit::UnitType::AngularVelocity,
    hid::unit::UnitType::LinearVelocity, hid::unit::UnitType::Acceleration,
    hid::unit::UnitType::MagneticFlux,   hid::unit::UnitType::Light,
    hid::unit::UnitType::Pressure};

// Tests that inserting and extracting as a unit type leads to the same value.
TEST(HidUnitsTest, InsertExtractBalanced) {
  hid::Attributes attr = {};
  attr.logc_mm.min = 0;
  attr.logc_mm.max = 100;
  attr.phys_mm.min = 0;
  attr.phys_mm.max = 200;

  attr.bit_sz = 8;
  attr.offset = 0;

  uint8_t report;

  for (size_t i = 0; i < countof(units); i++) {
    double initial_value = 50;
    double out_value;
    attr.unit = GetUnitFromUnitType(units[i]);
    ASSERT_TRUE(InsertAsUnitType(&report, sizeof(report), attr, initial_value));
    ASSERT_TRUE(ExtractAsUnitType(&report, sizeof(report), attr, &out_value));

    ASSERT_EQ(out_value, initial_value);
  }
}

// Tests that inserting and extracting with different exponents leads to the same value.
TEST(HidUnitsTest, InsertExtractBalancedExp) {
  hid::Attributes attr = {};
  attr.logc_mm.min = 0;
  attr.logc_mm.max = 200;
  attr.phys_mm.min = 0;
  attr.phys_mm.max = 200;

  attr.bit_sz = 8;
  attr.offset = 0;

  uint8_t report;

  for (size_t i = 0; i < countof(units); i++) {
    int32_t initial_value = 10;
    int32_t out_int;

    attr.unit = GetUnitFromUnitType(units[i]);
    // Decrement the exponent, so there is an exponent conversion in Insert/Extract functions.
    attr.unit.exp++;
    ASSERT_TRUE(
        InsertAsUnitType(&report, sizeof(report), attr, static_cast<double>(initial_value)));

    double out_value;
    ASSERT_TRUE(ExtractAsUnitType(&report, sizeof(report), attr, &out_value));
    out_int = static_cast<int32_t>(out_value);

    ASSERT_EQ(initial_value, out_int);
  }
}
