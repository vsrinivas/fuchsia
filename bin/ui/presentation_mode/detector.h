// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_PRESENTATION_MODE_DETECTOR_H_
#define GARNET_BIN_UI_PRESENTATION_MODE_DETECTOR_H_

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include <fuchsia/ui/input/cpp/fidl.h>
#include <presentation/cpp/fidl.h>

namespace presentation_mode {

namespace internal {
class MovingAverage;
}  // namespace internal

using AccelerometerData = std::array<int16_t, 3>;

// Use accelerometer data to detect changes in presentation mode. The detection
// is accurate only relative to the earth's gravity vector, so it's still easy
// to confuse the detector. We use a moving average to smooth out the data.
//
// This class is not copyable but it is movable.
class Detector final {
 public:
  Detector(size_t history_size);
  ~Detector() = default;

  // Movable
  Detector(Detector&& rhs) = default;
  Detector& operator=(Detector&& rhs) = default;

  // Not copyable
  Detector(const Detector& rhs) = delete;
  Detector& operator=(Detector& rhs) = delete;

  // Return <true,mode> if a mode was recognized and stable.
  // Otherwise return <false,_>, where the second value is undefined.
  std::pair<bool, presentation::PresentationMode> Update(
      const fuchsia::ui::input::SensorDescriptor& sensor,
      fuchsia::ui::input::InputReport event);

 private:
  // Interpretation of X, Y, Z, based on reading words on the base (keyboard) or
  // the lid (screen).
  // Pos X is your eye reading words, or right arrow.
  // Pos Y is your hand moving from space bar to Fn keys, or up arrow.
  // Pos Z is your hand rising from the reading surface (keyboard and screen).
  static constexpr int16_t kXPosLimit = 15000;
  static constexpr int16_t kXNegLimit = -15000;
  static constexpr int16_t kYPosLimit = 15000;
  static constexpr int16_t kYNegLimit = -15000;
  static constexpr int16_t kZPosLimit = 15000;
  static constexpr int16_t kZNegLimit = -15000;

  std::unique_ptr<internal::MovingAverage> base_accelerometer_;
  std::unique_ptr<internal::MovingAverage> lid_accelerometer_;
};

namespace internal {

// Keeps track of the moving average for AccelerometerData.
class MovingAverage final {
 public:
  MovingAverage(size_t size);
  ~MovingAverage() = default;

  AccelerometerData Average();
  void Update(AccelerometerData data);

 private:
  const size_t max_count_;

  // Ring buffer of past measurements
  size_t count_;
  size_t curr_;
  std::vector<AccelerometerData> data_;

  // Running sum
  std::array<int32_t, 3> sum_;
};

}  // namespace internal
}  // namespace presentation_mode

#endif  // GARNET_BIN_UI_PRESENTATION_MODE_DETECTOR_H_
