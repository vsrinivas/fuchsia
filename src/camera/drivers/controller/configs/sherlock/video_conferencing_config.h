// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_VIDEO_CONFERENCING_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_VIDEO_CONFERENCING_CONFIG_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <vector>

#include "src/camera/drivers/controller/configs/sherlock/common_util.h"
#include "src/camera/drivers/controller/configs/sherlock/internal_config.h"
#include "src/camera/lib/stream_utils/stream_constraints.h"

// Config 2: Video conferencing configuration.
//          Stream 0: ML | FR | VIDEO
//          Stream 1: VIDEO
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
constexpr uint32_t kMlFRMinBufferForCamping = 3;
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

}  // namespace

// Returns the internal video conferencing configuration (FR).
InternalConfigNode VideoConfigFullRes();

// Return the external video conferencing configuration.
fuchsia::camera2::hal::Config VideoConferencingConfig();

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_VIDEO_CONFERENCING_CONFIG_H_
