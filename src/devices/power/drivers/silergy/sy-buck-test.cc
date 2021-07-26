// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sy-buck.h"

#include <lib/ddk/platform-defs.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace silergy {

class SyBuckTest : public SyBuck {
 public:
  SyBuckTest(zx_device_t* parent, const ddk::I2cProtocolClient&& i2c)
      : SyBuck(parent, std::move(i2c)) {}

  zx_status_t Init() { return SyBuck::Init(); }

  uint32_t GetMinVoltageUv() { return kMinVoltageUv; }
  uint32_t GetVoltageStepUv() { return kVoltageStepUv; }
  uint32_t GetNumSteps() { return kNumSteps; }
};

class SyBuckTestFixture : public zxtest::Test {
 public:
  SyBuckTestFixture() : dut_(fake_parent_.get(), ddk::I2cProtocolClient(mock_i2c_.GetProto())) {}

  void SetUp() override {
    mock_i2c_.ExpectWrite({0x03}).ExpectReadStop({0x80}).ExpectWrite({0x04}).ExpectReadStop({0x08});
  }

 protected:
  void Verify() { mock_i2c_.VerifyAndClear(); }

  std::shared_ptr<MockDevice> fake_parent_ = MockDevice::FakeRootParent();
  mock_i2c::MockI2c mock_i2c_;
  SyBuckTest dut_;
};

TEST_F(SyBuckTestFixture, Init) {
  mock_i2c_.ExpectWrite({0x00}).ExpectReadStop({0xFF});
  EXPECT_OK(dut_.Init());
  ASSERT_NO_FATAL_FAILURES(Verify());
}

TEST_F(SyBuckTestFixture, ReadConfig) {
  mock_i2c_.ExpectWrite({0x00}).ExpectReadStop({0xFF});
  EXPECT_OK(dut_.Init());

  vreg_params_t params;
  dut_.VregGetRegulatorParams(&params);

  EXPECT_EQ(params.min_uv, dut_.GetMinVoltageUv());
  EXPECT_EQ(params.num_steps, dut_.GetNumSteps());
  EXPECT_EQ(params.step_size_uv, dut_.GetVoltageStepUv());

  ASSERT_NO_FATAL_FAILURES(Verify());
}

TEST_F(SyBuckTestFixture, ReadConfigNull) {
  mock_i2c_.ExpectWrite({0x00}).ExpectReadStop({0xFF});
  EXPECT_OK(dut_.Init());

  // Make sure nulls don't crash us.
  dut_.VregGetRegulatorParams(nullptr);

  ASSERT_NO_FATAL_FAILURES(Verify());
}

TEST_F(SyBuckTestFixture, SetStep) {
  mock_i2c_.ExpectWrite({0x00}).ExpectReadStop({0xFF});

  EXPECT_OK(dut_.Init());

  mock_i2c_.ExpectWrite({0x00}).ExpectReadStop({0xFF}).ExpectWriteStop({0x00, 0xC4});

  EXPECT_OK(dut_.VregSetVoltageStep(4));

  ASSERT_NO_FATAL_FAILURES(Verify());
}

TEST_F(SyBuckTestFixture, GetStep) {
  mock_i2c_.ExpectWrite({0x00}).ExpectReadStop({0xFF});
  EXPECT_OK(dut_.Init());
  ASSERT_NO_FATAL_FAILURES(Verify());
}

TEST_F(SyBuckTestFixture, SetStepOutOfBounds) {
  mock_i2c_.ExpectWrite({0x00}).ExpectReadStop({0xFF});

  // Set to something outside the acceptable range.
  EXPECT_NOT_OK(dut_.VregSetVoltageStep(dut_.GetNumSteps()));

  EXPECT_OK(dut_.Init());
  ASSERT_NO_FATAL_FAILURES(Verify());
}

}  // namespace silergy
