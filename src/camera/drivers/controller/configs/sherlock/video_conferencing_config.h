// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_VIDEO_CONFERENCING_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_VIDEO_CONFERENCING_CONFIG_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <vector>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/camera/drivers/controller/configs/sherlock/common_util.h"
#include "src/camera/lib/stream_utils/stream_constraints.h"

// Config 2: Video conferencing configuration.
//          Stream 0: ML | FR | VIDEO
//          Stream 1: VIDEO
// Config 3: Video conferencing configuration.
//          Stream 0: ML | FR | VIDEO | EXTENDED_FOV
//          Stream 1: VIDEO | EXTENDED_FOV

namespace camera {

namespace {

constexpr fuchsia::sysmem::PixelFormatType kFramePixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;

// Isp FR parameters
constexpr uint32_t kIspFRWidth = 2176;
constexpr uint32_t kIspFRHeight = 2720;

// GDC parameters
constexpr uint32_t kGdcFRWidth = 2240;
constexpr uint32_t kGdcFRHeight = 1792;

// ML Video FR Parameters
// ISP needs to hold on to 3 frames at any given point
// DMA module has a queue for 3 frames - current, done & delay frame.
constexpr uint32_t kMlFRMinBufferForCamping = 3;
// GDC needs to hold on to 1 frame for processing.
constexpr uint32_t kGdcBufferForCamping = 1;

constexpr uint32_t kMlFRWidth = 640;
constexpr uint32_t kMlFRHeight = 512;
constexpr uint32_t kMlFRFrameRate = 5;

// Video Conferencing FR Parameters
constexpr uint32_t kVideoMinBufferForCamping = 3;
constexpr uint32_t kVideoWidth = 1280;
constexpr uint32_t kVideoHeight = 720;
constexpr uint32_t kVideoWidth1 = 896;
constexpr uint32_t kVideoHeight1 = 504;
constexpr uint32_t kVideoWidth2 = 640;
constexpr uint32_t kVideoHeight2 = 360;
constexpr uint32_t kVideoFrameRate = 30;

constexpr auto kMlStreamType = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                               fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
                               fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE;
constexpr auto kVideoStreamType = fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE;

}  // namespace

// Returns the internal video conferencing configuration (FR).
InternalConfigNode VideoConfigFullRes(bool extended_fov);

// Return the external video conferencing configuration.
fuchsia::camera2::hal::Config VideoConferencingConfig(bool extended_fov);

constexpr FrameRateRange kVideoFrameRateRange = {
    .min =
        {
            .frames_per_sec_numerator = kVideoFrameRate,
            .frames_per_sec_denominator = 1,
        },
    .max =
        {
            .frames_per_sec_numerator = kVideoFrameRate,
            .frames_per_sec_denominator = 1,
        },
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_VIDEO_CONFERENCING_CONFIG_H_
