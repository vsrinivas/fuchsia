// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/sensors/imx227/imx227.h"

#include <lib/driver-unit-test/logger.h>
#include <lib/driver-unit-test/utils.h>

#include <iostream>

#include <zxtest/zxtest.h>

#include "src/camera/drivers/sensors/imx227/imx227_otp_config.h"

namespace camera {
namespace {

const uint32_t kValidSensorMode = 0;

class Imx227DeviceTest : public zxtest::Test {
 protected:
  void SetUp() override {
    ASSERT_OK(Imx227Device::Create(nullptr, driver_unit_test::GetParent(), &imx227_device_));
    ASSERT_OK(imx227_device_->CameraSensorInit());
  }

  void TearDown() override {
    imx227_device_->CameraSensorDeInit();
    imx227_device_.release();
  }

  std::unique_ptr<Imx227Device> imx227_device_;
};

TEST_F(Imx227DeviceTest, SetMode) {
  EXPECT_OK(imx227_device_->CameraSensorSetMode(kValidSensorMode));
  EXPECT_NOT_OK(imx227_device_->CameraSensorSetMode(0xFF));
}

TEST_F(Imx227DeviceTest, StartAndStopStreaming) {
  EXPECT_NE(ZX_OK, imx227_device_->CameraSensorStopStreaming());
  EXPECT_EQ(ZX_OK, imx227_device_->CameraSensorStartStreaming());
  EXPECT_NE(ZX_OK, imx227_device_->CameraSensorStartStreaming());
  // TODO(braval): Figure out a way to validate starting & stopping of streaming
  EXPECT_EQ(ZX_OK, imx227_device_->CameraSensorStopStreaming());
}

TEST_F(Imx227DeviceTest, DeInitStateThrowsErrors) {
  imx227_device_->CameraSensorDeInit();
  EXPECT_NE(ZX_OK, imx227_device_->CameraSensorSetMode(kValidSensorMode));
  EXPECT_NE(ZX_OK, imx227_device_->CameraSensorStartStreaming());
  EXPECT_NE(ZX_OK, imx227_device_->CameraSensorStopStreaming());
}

TEST_F(Imx227DeviceTest, OtpReadAndValidate) {
  // Try reading the entire OTP from the start
  fit::result result = imx227_device_->OtpRead();
  ASSERT_TRUE(result.is_ok());
  zx::vmo vmo = std::move(result.value());

  // Validate vmo contents
  EXPECT_TRUE(imx227_device_->OtpValidate(&vmo));
}

}  // namespace
}  // namespace camera
