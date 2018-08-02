// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/array.h>
#include <limits>

#include "garnet/bin/ui/presentation_mode/detector.h"
#include "gtest/gtest.h"

namespace presentation_mode {
namespace test {

fuchsia::ui::input::SensorDescriptor CreateAccelerometer(
    fuchsia::ui::input::SensorLocation loc) {
  fuchsia::ui::input::SensorDescriptor desc;
  desc.type = fuchsia::ui::input::SensorType::ACCELEROMETER;
  desc.loc = loc;
  return desc;
}

constexpr int16_t kMaxVal = std::numeric_limits<int16_t>::max();
constexpr int16_t kMinVal = std::numeric_limits<int16_t>::min();
constexpr AccelerometerData kZero = {0, 0, 0};
const fuchsia::ui::input::SensorDescriptor kBaseSensor =
    CreateAccelerometer(fuchsia::ui::input::SensorLocation::BASE);
const fuchsia::ui::input::SensorDescriptor kLidSensor =
    CreateAccelerometer(fuchsia::ui::input::SensorLocation::LID);

// Exercise expected values over partial and full history (ie 0-4 events), as
// well as overflow and underflow values.
TEST(PositiveData, MovingAverage) {
  constexpr AccelerometerData kMax = {kMaxVal, kMaxVal, kMaxVal};

  presentation_mode::internal::MovingAverage mv(/*history*/ 3);
  EXPECT_EQ(mv.Average(), kZero);

  mv.Update(kMax);
  EXPECT_EQ(mv.Average(), kMax);

  mv.Update(kMax);
  EXPECT_EQ(mv.Average(), kMax);

  mv.Update(kMax);
  EXPECT_EQ(mv.Average(), kMax);

  mv.Update(kMax);
  EXPECT_EQ(mv.Average(), kMax);
}

TEST(NegativeData, MovingAverage) {
  constexpr AccelerometerData kMin = {kMinVal, kMinVal, kMinVal};

  presentation_mode::internal::MovingAverage mv(/*history*/ 3);
  EXPECT_EQ(mv.Average(), kZero);

  mv.Update(kMin);
  EXPECT_EQ(mv.Average(), kMin);

  mv.Update(kMin);
  EXPECT_EQ(mv.Average(), kMin);

  mv.Update(kMin);
  EXPECT_EQ(mv.Average(), kMin);

  mv.Update(kMin);
  EXPECT_EQ(mv.Average(), kMin);
}

fuchsia::ui::input::InputReport CreateVector(int16_t x, int16_t y, int16_t z) {
  fuchsia::ui::input::InputReport report;
  report.sensor = fuchsia::ui::input::SensorReport::New();
  fidl::Array<int16_t, 3> data;
  data[0] = x;
  data[1] = y;
  data[2] = z;
  report.sensor->set_vector(data);
  return report;
}

TEST(Detector, Closed) {
  Detector detector(/*history*/ 2);

  fuchsia::ui::input::InputReport base_report = CreateVector(0, 0, kMaxVal);
  std::pair<bool, fuchsia::ui::policy::PresentationMode> result =
      detector.Update(kBaseSensor, std::move(base_report));
  EXPECT_FALSE(result.first);

  fuchsia::ui::input::InputReport lid_report = CreateVector(0, 0, kMinVal);
  result = detector.Update(kLidSensor, std::move(lid_report));
  EXPECT_TRUE(result.first);
  EXPECT_EQ(result.second, fuchsia::ui::policy::PresentationMode::CLOSED);

  fuchsia::ui::input::InputReport base_shift = CreateVector(0, 0, kMinVal);
  result = detector.Update(kBaseSensor, std::move(base_shift));
  EXPECT_FALSE(result.first);
}

TEST(Detector, Laptop) {
  Detector detector(/*history*/ 2);

  fuchsia::ui::input::InputReport base_report = CreateVector(0, 0, kMaxVal);
  std::pair<bool, fuchsia::ui::policy::PresentationMode> result =
      detector.Update(kBaseSensor, std::move(base_report));
  EXPECT_FALSE(result.first);

  fuchsia::ui::input::InputReport lid_report = CreateVector(0, kMaxVal, 0);
  result = detector.Update(kLidSensor, std::move(lid_report));
  EXPECT_TRUE(result.first);
  EXPECT_EQ(result.second, fuchsia::ui::policy::PresentationMode::LAPTOP);

  fuchsia::ui::input::InputReport base_shift = CreateVector(0, 0, kMinVal);
  result = detector.Update(kBaseSensor, std::move(base_shift));
  EXPECT_FALSE(result.first);
}

TEST(Detector, Tablet) {
  Detector detector(/*history*/ 2);

  fuchsia::ui::input::InputReport base_report = CreateVector(0, 0, kMinVal);
  std::pair<bool, fuchsia::ui::policy::PresentationMode> result =
      detector.Update(kBaseSensor, std::move(base_report));
  EXPECT_FALSE(result.first);

  fuchsia::ui::input::InputReport lid_report = CreateVector(0, 0, kMaxVal);
  result = detector.Update(kLidSensor, std::move(lid_report));
  EXPECT_TRUE(result.first);
  EXPECT_EQ(result.second, fuchsia::ui::policy::PresentationMode::TABLET);

  fuchsia::ui::input::InputReport base_shift = CreateVector(0, 0, kMaxVal);
  result = detector.Update(kBaseSensor, std::move(base_shift));
  EXPECT_FALSE(result.first);
}

TEST(Detector, Tent) {
  Detector detector(/*history*/ 2);

  fuchsia::ui::input::InputReport base_report = CreateVector(0, kMaxVal, 0);
  std::pair<bool, fuchsia::ui::policy::PresentationMode> result =
      detector.Update(kBaseSensor, std::move(base_report));
  EXPECT_FALSE(result.first);

  fuchsia::ui::input::InputReport lid_report = CreateVector(0, kMinVal, 0);
  result = detector.Update(kLidSensor, std::move(lid_report));
  EXPECT_TRUE(result.first);
  EXPECT_EQ(result.second, fuchsia::ui::policy::PresentationMode::TENT);

  fuchsia::ui::input::InputReport base_shift = CreateVector(0, kMinVal, 0);
  result = detector.Update(kBaseSensor, std::move(base_shift));
  EXPECT_FALSE(result.first);
}

TEST(Detector, NonAccelerometer) {
  Detector detector(/*history*/ 2);

  fuchsia::ui::input::SensorDescriptor sensor;
  sensor.type = fuchsia::ui::input::SensorType::LIGHTMETER;
  sensor.loc = fuchsia::ui::input::SensorLocation::LID;

  fuchsia::ui::input::InputReport report = {};
  auto result = detector.Update(sensor, std::move(report));
  EXPECT_FALSE(result.first);
}

}  // namespace test
}  // namespace presentation_mode
