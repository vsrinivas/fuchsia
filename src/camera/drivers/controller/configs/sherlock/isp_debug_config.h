// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <vector>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/camera/drivers/controller/configs/sherlock/common_util.h"
#include "src/camera/lib/stream_utils/stream_constraints.h"

// Config 0: Debug configuration.
//          Stream 0: FR

namespace camera {

namespace {

// IspDebugStream Parameters
constexpr uint32_t kStreamMinBufferForCamping = 5;
constexpr uint32_t kFRStreamWidth = 2176;
constexpr uint32_t kFRStreamHeight = 2720;
constexpr uint32_t kDSStreamWidth = 1152;
constexpr uint32_t kDSStreamHeight = 1440;
constexpr uint32_t kFRStreamFrameRate = 30;
constexpr uint32_t kDSStreamFrameRate = 30;
constexpr fuchsia::sysmem::PixelFormatType kStreamPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;

}  // namespace

// Returns the internal debug configuration(FR).
InternalConfigNode DebugConfigFullRes();

// Returns the internal debug configuration(DS).
InternalConfigNode DebugConfigDownScaledRes();

// Return the external debug configuration.
fuchsia::camera2::hal::Config DebugConfig();

constexpr FrameRateRange kDebugFrameRateRange = {
    .min =
        {
            .frames_per_sec_numerator = kFRStreamFrameRate,
            .frames_per_sec_denominator = 1,
        },
    .max =
        {
            .frames_per_sec_numerator = kFRStreamFrameRate,
            .frames_per_sec_denominator = 1,
        },
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_
