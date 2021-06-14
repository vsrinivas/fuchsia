// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/power/drivers/fusb302/fusb302.h"

#include <lib/mock-i2c/mock-i2c.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/inspect/testing/cpp/zxtest/inspect.h"

namespace fusb302 {

namespace {

bool FloatNear(double a, double b) { return std::abs(a - b) < 0.001; }

}  // namespace

using inspect::InspectTestHelper;

class Fusb302Test : public Fusb302 {
 public:
  Fusb302Test(const i2c_protocol_t* i2c) : Fusb302(nullptr, i2c) {}

  zx_status_t Init() { return Fusb302::Init(); }
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }
};

class Fusb302TestFixture : public InspectTestHelper, public zxtest::Test {
 public:
  Fusb302TestFixture() : dut_(mock_i2c_.GetProto()) {}

  void SetUp() override {
    // Device ID
    mock_i2c_.ExpectWrite({0x01}).ExpectReadStop({0x91});  // Device ID
    mock_i2c_.ExpectWrite({0x03}).ExpectReadStop({0x90});  // Switches1
    // Setup Measure
    mock_i2c_.ExpectWrite({0x0B}).ExpectReadStop({0x00});  // Power
    mock_i2c_.ExpectWriteStop({0x0B, 0x4});
    mock_i2c_.ExpectWrite({0x02}).ExpectReadStop({0xFF});  // Switches0
    mock_i2c_.ExpectWriteStop({0x02, 0xF3});
    // Measure VBUS
    mock_i2c_.ExpectWrite({0x04}).ExpectReadStop({0x00});  // Measure
    mock_i2c_.ExpectWriteStop({0x04, 0x40});
    mock_i2c_.ExpectWrite({0x04}).ExpectReadStop({0x43});
    // Measure CC1
    mock_i2c_.ExpectWrite({0x04}).ExpectReadStop({0xFF});  // Measure
    mock_i2c_.ExpectWriteStop({0x04, 0xBF});
    mock_i2c_.ExpectWrite({0x02}).ExpectReadStop({0x00});  // Switches0
    mock_i2c_.ExpectWriteStop({0x02, 0x04});
    mock_i2c_.ExpectWrite({0x04}).ExpectReadStop({0x0A});  // Measure
    mock_i2c_.ExpectWrite({0x40}).ExpectReadStop({0x03});  // Status0
    // Measure CC2
    mock_i2c_.ExpectWrite({0x02}).ExpectReadStop({0x00});  // Switches0
    mock_i2c_.ExpectWriteStop({0x02, 0x08});
    mock_i2c_.ExpectWrite({0x04}).ExpectReadStop({0x04});  // Measure
    mock_i2c_.ExpectWrite({0x40}).ExpectReadStop({0x01});  // Status0
    // Power off Measure Block
    mock_i2c_.ExpectWrite({0x0B}).ExpectReadStop({0xFF});  // Power
    mock_i2c_.ExpectWriteStop({0x0B, 0xFB});

    EXPECT_OK(dut_.Init());
  }

  void TearDown() override { mock_i2c_.VerifyAndClear(); }

 protected:
  mock_i2c::MockI2c mock_i2c_;
  Fusb302Test dut_;
};

TEST_F(Fusb302TestFixture, InspectTest) {
  ASSERT_NO_FATAL_FAILURES(ReadInspect(dut_.inspect_vmo()));
  auto* device_id = hierarchy().GetByPath({"DeviceId"});
  ASSERT_TRUE(device_id);
  // VersionId: 9
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(device_id->node(), "VersionId", inspect::UintPropertyValue(9)));
  // ProductId: 0
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(device_id->node(), "ProductId", inspect::UintPropertyValue(0)));
  // RevisionId: 1
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(device_id->node(), "RevisionId", inspect::UintPropertyValue(1)));

  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(hierarchy().node(), "PowerRole", inspect::StringPropertyValue("Source")));
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(hierarchy().node(), "DataRole", inspect::StringPropertyValue("Source")));
  auto vbus_volt =
      hierarchy().node().get_property<inspect::DoublePropertyValue>("VBUS Voltage (Volts)");
  EXPECT_TRUE(vbus_volt);
  EXPECT_TRUE(FloatNear(vbus_volt->value(), 1.26));

  auto* cc1 = hierarchy().GetByPath({"CC1"});
  ASSERT_TRUE(cc1);
  // CC1 Voltage (Volts): 0.42
  auto cc1_volt = cc1->node().get_property<inspect::DoublePropertyValue>("CC1 Voltage (Volts)");
  EXPECT_TRUE(cc1_volt);
  EXPECT_TRUE(FloatNear(cc1_volt->value(), 0.42));
  // Battery Charging Level: > 1.23 V
  ASSERT_NO_FATAL_FAILURES(CheckProperty(cc1->node(), "Battery Charging Level",
                                         inspect::StringPropertyValue("> 1.23 V")));

  auto* cc2 = hierarchy().GetByPath({"CC2"});
  ASSERT_TRUE(cc2);
  // CC2 Voltage (Volts): 0.168
  auto cc2_volt = cc2->node().get_property<inspect::DoublePropertyValue>("CC2 Voltage (Volts)");
  EXPECT_TRUE(cc2_volt);
  EXPECT_TRUE(FloatNear(cc2_volt->value(), 0.168));
  // Battery Charging Level: 200 mV - 660 mV
  ASSERT_NO_FATAL_FAILURES(CheckProperty(cc2->node(), "Battery Charging Level",
                                         inspect::StringPropertyValue("200 mV - 660 mV")));
}

}  // namespace fusb302
