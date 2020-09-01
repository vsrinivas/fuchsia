// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_LUIS_ISP_DEBUG_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_LUIS_ISP_DEBUG_CONFIG_H_

#include "src/camera/drivers/controller/configs/internal_config.h"

// Config 0: Debug configuration.
//          Stream 0: FR

namespace camera {

namespace {

// IspDebugStream Parameters
constexpr uint32_t kStreamMinBufferForCamping = 5;
constexpr uint32_t kFRStreamWidth = 3280;
constexpr uint32_t kFRStreamHeight = 2432;
constexpr uint32_t kFRStreamFrameRate = 30;
constexpr fuchsia::sysmem::PixelFormatType kStreamPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;

}  // namespace

// Returns the internal debug configuration(FR).
InternalConfigNode DebugConfigFullRes();

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

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_LUIS_ISP_DEBUG_CONFIG_H_
