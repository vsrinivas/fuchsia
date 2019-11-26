// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_COMMON_UTIL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_COMMON_UTIL_H_

#include "src/camera/stream_utils/stream_constraints.h"

namespace camera {

namespace {
constexpr uint32_t kGdcBytesPerRowDivisor = 16;
constexpr uint32_t kIspBytesPerRowDivisor = 128;
}  // namespace

fuchsia::camera2::StreamProperties GetStreamProperties(fuchsia::camera2::CameraStreamType type) {
  fuchsia::camera2::StreamProperties ret{};
  ret.set_stream_type(type);
  return ret;
}

fuchsia::sysmem::BufferCollectionConstraints InvalidConstraints() {
  StreamConstraints stream_constraints;
  stream_constraints.set_bytes_per_row_divisor(0);
  stream_constraints.set_contiguous(false);
  stream_constraints.AddImageFormat(0, 0, fuchsia::sysmem::PixelFormatType::NV12);
  stream_constraints.set_buffer_count_for_camping(0);
  return stream_constraints.MakeBufferCollectionConstraints();
}

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_COMMON_UTIL_H_
