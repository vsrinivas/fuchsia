// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock-sensor-device.h"

#include <zxtest/zxtest.h>

namespace camera {
namespace {

constexpr int32_t kNewGain = 1000;
constexpr int32_t kNewIntegrationTime = 30;

constexpr sensor_mode_t kMode = {
    .fpms = 30000,
    .resolution =
        {
            .width = 2200,
            .height = 2720,
        },
    .exposures = 1,
    .wdr_mode = WDR_MODE_LINEAR,
    .bits = 10,
    .lanes = 2,
    .mbps = 1000,
    .idx = 3,
    .bayer = BAYER_RGGB,
};

class MockSensorDeviceTest : public zxtest::Test {
 public:
  void SetUp() override;

protected:
  std::unique_ptr<MockSensorDevice> sensor_;
};

void MockSensorDeviceTest::SetUp() {
  sensor_ = std::make_unique<MockSensorDevice>();
}

TEST_F(MockSensorDeviceTest, InitSuccess) {
  ASSERT_FALSE(sensor_->IsInitialized());
  ASSERT_OK(sensor_->CameraSensorInit());
  ASSERT_TRUE(sensor_->IsInitialized());
  sensor_->CameraSensorDeInit();
  ASSERT_FALSE(sensor_->IsInitialized());
}

TEST_F(MockSensorDeviceTest, InitError) {
  ASSERT_FALSE(sensor_->IsInitialized());
  ASSERT_OK(sensor_->CameraSensorInit());
  ASSERT_TRUE(sensor_->IsInitialized());
  ASSERT_NOT_OK(sensor_->CameraSensorInit());
}

TEST_F(MockSensorDeviceTest, SetModeError) {
  ASSERT_NOT_OK(sensor_->CameraSensorSetMode(4));
}

TEST_F(MockSensorDeviceTest, SetMode) {
  constexpr uint8_t kModeIdx = 1;
  ASSERT_NOT_OK(sensor_->CameraSensorSetMode(kModeIdx));
  sensor_->AddMode(kMode);
  ASSERT_OK(sensor_->CameraSensorSetMode(kModeIdx));
}

TEST_F(MockSensorDeviceTest, StreamingSuccess) {
  ASSERT_FALSE(sensor_->IsStreaming());
  ASSERT_OK(sensor_->CameraSensorStartStreaming());
  ASSERT_TRUE(sensor_->IsStreaming());
  ASSERT_OK(sensor_->CameraSensorStopStreaming());
  ASSERT_FALSE(sensor_->IsStreaming());
}

TEST_F(MockSensorDeviceTest, StartStreamingError) {
  ASSERT_OK(sensor_->CameraSensorStartStreaming());
  ASSERT_NOT_OK(sensor_->CameraSensorStartStreaming());
}

TEST_F(MockSensorDeviceTest, StopStreamingError) {
  ASSERT_NOT_OK(sensor_->CameraSensorStopStreaming());
}

TEST_F(MockSensorDeviceTest, AnalogGain) {
  ASSERT_NE(kNewGain, sensor_->GetAnalogGain());
  ASSERT_EQ(kNewGain, sensor_->CameraSensorSetAnalogGain(kNewGain));
  ASSERT_EQ(kNewGain, sensor_->GetAnalogGain());
}

TEST_F(MockSensorDeviceTest, DigitalGain) {
  ASSERT_NE(kNewGain, sensor_->GetDigitalGain());
  ASSERT_EQ(kNewGain, sensor_->CameraSensorSetDigitalGain(kNewGain));
  ASSERT_EQ(kNewGain, sensor_->GetDigitalGain());
}

TEST_F(MockSensorDeviceTest, IntegrationTime) {
  ASSERT_NE(kNewIntegrationTime, sensor_->GetIntegrationTime());
  ASSERT_OK(sensor_->CameraSensorSetIntegrationTime(kNewIntegrationTime));
  ASSERT_EQ(kNewIntegrationTime, sensor_->GetIntegrationTime());
}

}  // namespace
}  // namespace camera
