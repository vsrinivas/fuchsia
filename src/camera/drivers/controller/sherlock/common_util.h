// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_COMMON_UTIL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_COMMON_UTIL_H_

#include "src/camera/drivers/controller/configs/product_config.h"
#include "src/camera/lib/stream_utils/stream_constraints.h"

namespace camera {

namespace {

// Frame rate throttle controls.
// The sensor max frame rate should match kThrottledFramesPerSecond in imx227/constants.h.
inline constexpr uint32_t kSensorMaxFramesPerSecond = 24;  // Default is 30.

// The Monitoring and Video throttles should be no larger than the sensor max fps.
// In typical usage, they will match the sensor max frame rate.
inline constexpr uint32_t kMonitoringThrottledOutputFrameRate = kSensorMaxFramesPerSecond;
inline constexpr uint32_t kVideoThrottledOutputFrameRate = kSensorMaxFramesPerSecond;

// This is the max number of buffers the client can ask for when setting its constraints.
// TODO(jsasinowski): This is enough to cover current clients, but should be exposed in some way
// for clients to know what the limit is, since it can't increase once allocation has completed.
inline constexpr uint32_t kGdcBytesPerRowDivisor = 16;
inline constexpr uint32_t kGe2dBytesPerRowDivisor = 32;
inline constexpr uint32_t kIspBytesPerRowDivisor = 128;
}  // namespace

fuchsia::camera2::StreamProperties GetStreamProperties(fuchsia::camera2::CameraStreamType type);

fuchsia::sysmem::BufferCollectionConstraints InvalidConstraints();

struct ConstraintsOverrides {
  std::optional<uint32_t> min_buffer_count_for_camping;
  std::optional<uint32_t> min_buffer_count;
};
// This function returns a copy of the provided constraints struct but with certain parameters
// modified to the provided values. This can be used to set output constraints of an internal node
// based on the an external stream config. This is useful because external configs apply real
// constraints to the collection (e.g. min_buffer_count), but they also represent aggregate views of
// the controller constraints to the client (specifically, min_buffer_count_for_camping). Because it
// is not possible for a collection client to voluntarily relinquish ownership of its camping
// allocation (fxbug.dev/99578) it is necessary to set this field to zero when the constraints are
// being used as a proxy to reserve space for a future client.
fuchsia::sysmem::BufferCollectionConstraints CopyConstraintsWithOverrides(
    const fuchsia::sysmem::BufferCollectionConstraints& original,
    ConstraintsOverrides overrides = ConstraintsOverrides{});

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_COMMON_UTIL_H_
