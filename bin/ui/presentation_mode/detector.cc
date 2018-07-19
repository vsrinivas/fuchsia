// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/presentation_mode/detector.h"

#include <assert.h>
#include <limits>

#include "garnet/public/lib/fxl/logging.h"

namespace presentation_mode {

Detector::Detector(size_t history_size)
    : base_accelerometer_(
          std::make_unique<internal::MovingAverage>(history_size)),
      lid_accelerometer_(
          std::make_unique<internal::MovingAverage>(history_size)) {
  FXL_CHECK(history_size > 0);
}

std::pair<bool, fuchsia::ui::policy::PresentationMode> Detector::Update(
    const fuchsia::ui::input::SensorDescriptor& sensor,
    fuchsia::ui::input::InputReport event) {
  FXL_CHECK(sensor.type == fuchsia::ui::input::SensorType::ACCELEROMETER);
  FXL_CHECK(event.sensor);
  FXL_CHECK(event.sensor->is_vector());

  const fidl::Array<int16_t, 3>& vector = event.sensor->vector();
  AccelerometerData data{vector[0], vector[1], vector[2]};

  switch (sensor.loc) {
    case fuchsia::ui::input::SensorLocation::BASE:
      base_accelerometer_->Update(data);
      break;
    case fuchsia::ui::input::SensorLocation::LID:
      lid_accelerometer_->Update(data);
      break;
    default:
      break;
  }

  AccelerometerData base_avg = base_accelerometer_->Average();
  AccelerometerData lid_avg = lid_accelerometer_->Average();

  std::pair<bool, fuchsia::ui::policy::PresentationMode> result;
  result.first = false;

  if (base_avg[2] > kZPosLimit && lid_avg[2] < kZNegLimit) {
    result.first = true;
    result.second = fuchsia::ui::policy::PresentationMode::CLOSED;
  } else if (base_avg[2] > kZPosLimit && lid_avg[1] > kYPosLimit) {
    result.first = true;
    result.second = fuchsia::ui::policy::PresentationMode::LAPTOP;
  } else if (base_avg[2] < kZNegLimit && lid_avg[2] > kZPosLimit) {
    result.first = true;
    result.second = fuchsia::ui::policy::PresentationMode::TABLET;
  } else if (base_avg[1] > kYPosLimit && lid_avg[1] < kYNegLimit) {
    result.first = true;
    result.second = fuchsia::ui::policy::PresentationMode::TENT;
  }

  if (result.first)
    FXL_VLOG(2) << "Presentation mode detected: " << result.second;

  return result;
}

internal::MovingAverage::MovingAverage(size_t size)
    : max_count_(size), count_(0), curr_(0), data_(size), sum_({0, 0, 0}) {
  FXL_CHECK(max_count_ <= std::numeric_limits<int16_t>::max())
      << "Integer truncation may occur. Reduce history size.";
}

AccelerometerData internal::MovingAverage::Average() {
  if (count_ == 0)
    return {0, 0, 0};

  AccelerometerData avg;
  // N.B. Truncation (and undefined behavior) guarded against by ctor's CHECK.
  int16_t denom_count = static_cast<int16_t>(count_);
  avg[0] = sum_[0] / denom_count;
  avg[1] = sum_[1] / denom_count;
  avg[2] = sum_[2] / denom_count;
  return avg;
}

void internal::MovingAverage::Update(AccelerometerData data) {
  if (count_ < max_count_) {
    ++count_;
  } else {
    sum_[0] -= data_[curr_][0];
    sum_[1] -= data_[curr_][1];
    sum_[2] -= data_[curr_][2];
  }
  sum_[0] += data[0];
  sum_[1] += data[1];
  sum_[2] += data[2];
  data_[curr_] = data;
  curr_ = (curr_ + 1) % max_count_;
}

}  // namespace presentation_mode
