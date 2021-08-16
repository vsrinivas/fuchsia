// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/power/drivers/fusb302/fusb302.h"

#include <lib/mock-i2c/mock-i2c.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/inspect/testing/cpp/zxtest/inspect.h"

namespace fusb302 {

using inspect::InspectTestHelper;

class Fusb302Test : public Fusb302 {
 public:
  Fusb302Test(const i2c_protocol_t* i2c) : Fusb302(nullptr, i2c, {}) {}  // TODO

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

    EXPECT_OK(dut_.Init());
  }

  void TearDown() override { mock_i2c_.VerifyAndClear(); }

 protected:
  mock_i2c::MockI2c mock_i2c_;
  Fusb302Test dut_;
};

// TODO (rdzhuang): Disabled tests. Tests will be added back in a future CL with modifications.
// TEST_F(Fusb302TestFixture, InspectTest) {
//   ASSERT_NO_FATAL_FAILURES(ReadInspect(dut_.inspect_vmo()));
//   auto* device_id = hierarchy().GetByPath({"DeviceId"});
//   ASSERT_TRUE(device_id);
//   // VersionId: 9
//   ASSERT_NO_FATAL_FAILURES(
//       CheckProperty(device_id->node(), "VersionId", inspect::UintPropertyValue(9)));
//   // ProductId: 0
//   ASSERT_NO_FATAL_FAILURES(
//       CheckProperty(device_id->node(), "ProductId", inspect::UintPropertyValue(0)));
//   // RevisionId: 1
//   ASSERT_NO_FATAL_FAILURES(
//       CheckProperty(device_id->node(), "RevisionId", inspect::UintPropertyValue(1)));
// }

}  // namespace fusb302
