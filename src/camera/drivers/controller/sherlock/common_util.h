// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_COMMON_UTIL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_COMMON_UTIL_H_

#include "src/camera/lib/stream_utils/stream_constraints.h"

namespace camera {

namespace {
constexpr uint32_t kGdcBytesPerRowDivisor = 16;
constexpr uint32_t kGe2dBytesPerRowDivisor = 32;
constexpr uint32_t kIspBytesPerRowDivisor = 128;
}  // namespace

fuchsia::camera2::StreamProperties GetStreamProperties(fuchsia::camera2::CameraStreamType type);

fuchsia::sysmem::BufferCollectionConstraints InvalidConstraints();

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_COMMON_UTIL_H_
