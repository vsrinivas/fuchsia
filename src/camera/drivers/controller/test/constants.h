// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_TEST_CONSTANTS_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_TEST_CONSTANTS_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <zircon/types.h>

namespace camera {

enum SherlockConfigs {
  MONITORING = 0u,
  VIDEO,
  VIDEO_EXTENDED_FOV,
  MAX,
};

constexpr auto kStreamTypeFR = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION;
constexpr auto kStreamTypeDS = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION;
constexpr auto kStreamTypeML = fuchsia::camera2::CameraStreamType::MACHINE_LEARNING;
constexpr auto kStreamTypeVideo = fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE;
constexpr auto kStreamTypeVideoExtendedFOV = fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE |
                                             fuchsia::camera2::CameraStreamType::EXTENDED_FOV;
constexpr auto kStreamTypeMonitoring = fuchsia::camera2::CameraStreamType::MONITORING;

// kNumBuffers represents the number of buffers to feed into the camera pipeline fakes.
// Based on the current code, it is constrained to the size of the smallest buffer collection
// involving the ISP. On Sherlock, this is 4.
constexpr auto kNumBuffers = 4;

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_TEST_CONSTANTS_H_
