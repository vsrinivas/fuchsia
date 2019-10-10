// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_COMMON_UTIL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_COMMON_UTIL_H_

namespace camera {

fuchsia::camera2::StreamProperties GetStreamProperties(fuchsia::camera2::CameraStreamType type) {
  fuchsia::camera2::StreamProperties ret{};
  ret.set_stream_type(type);
  return ret;
}

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_COMMON_UTIL_H_
