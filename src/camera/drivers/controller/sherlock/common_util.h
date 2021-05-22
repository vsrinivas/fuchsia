// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_COMMON_UTIL_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_COMMON_UTIL_H_

#include "src/camera/lib/stream_utils/stream_constraints.h"

namespace camera {

namespace {
// This is the max number of buffers the client can ask for when setting its constraints.
// TODO(afoxley) This is enough to cover current clients, but should be exposed in some way
// for clients to know what the limit is, since it can't increase once allocation has completed.
inline constexpr uint32_t kNumClientBuffers = 5;
inline constexpr uint32_t kGdcBytesPerRowDivisor = 16;
inline constexpr uint32_t kGe2dBytesPerRowDivisor = 32;
inline constexpr uint32_t kIspBytesPerRowDivisor = 128;

// ISP needs to hold on to 3 frames at any given point
// DMA module has a queue for 3 frames - current, done & delay frame.
inline constexpr uint32_t kIspBufferForCamping = 3;
// GDC needs to hold on to 1 frame for processing.
inline constexpr uint32_t kGdcBufferForCamping = 1;
// GE2D needs to hold on to 1 frame for processing.
inline constexpr uint32_t kGe2dBufferForCamping = 1;
// Extra buffers to keep the pipelines flowing.
inline constexpr uint32_t kExtraBuffers = 1;
}  // namespace

fuchsia::camera2::StreamProperties GetStreamProperties(fuchsia::camera2::CameraStreamType type);

fuchsia::sysmem::BufferCollectionConstraints InvalidConstraints();

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_COMMON_UTIL_H_
