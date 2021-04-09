// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_MONITORING_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_MONITORING_CONFIG_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <vector>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/camera/drivers/controller/sherlock/common_util.h"
#include "src/camera/lib/stream_utils/stream_constraints.h"

// Config 1: Monitoring configuration.
//          Stream 0: ML | FR
//          Stream 1: ML | DS
//          Stream 2: MONITORING

namespace camera {

namespace {

// OutputStreamMLFR Parameters
constexpr uint32_t kOutputStreamMlFRMinBufferForCamping = 5;
constexpr uint32_t kOutputStreamMlFRWidth = 2176;
constexpr uint32_t kOutputStreamMlFRHeight = 2720;
constexpr uint32_t kOutputStreamMlFRFrameRate = 10;
constexpr fuchsia::sysmem::PixelFormatType kOutputStreamMlFRPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;

// OutputStreamMLDS Parameters
constexpr uint32_t kOutputStreamMlDSMinBufferForCamping = 5;
constexpr uint32_t kOutputStreamMlDSWidth = 640;
constexpr uint32_t kOutputStreamMlDSHeight = 512;
constexpr uint32_t kOutputStreamMlDSFrameRate = 10;
constexpr fuchsia::sysmem::PixelFormatType kOutputStreamMlDSPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;

// OutputStreamMonitoring Parameters
constexpr uint32_t kOutputStreamDSWidth = 1152;
constexpr uint32_t kOutputStreamDSHeight = 1440;
constexpr uint32_t kOutputStreamMonitoringMinBufferForCamping = 5;
constexpr uint32_t kOutputStreamMonitoringWidth = 1152;
constexpr uint32_t kOutputStreamMonitoringHeight = 864;
constexpr uint32_t kOutputStreamMonitoringWidth1 = 720;
constexpr uint32_t kOutputStreamMonitoringHeight1 = 540;
constexpr uint32_t kOutputStreamMonitoringWidth2 = 512;
constexpr uint32_t kOutputStreamMonitoringHeight2 = 384;
constexpr uint32_t kMaxOutputStreamMonitoringFrameRate = 30;
constexpr uint32_t kMinOutputStreamMonitoringFrameRate = 15;
constexpr fuchsia::sysmem::PixelFormatType kOutputStreamMonitoringPixelFormat =
    fuchsia::sysmem::PixelFormatType::NV12;

// Watermark information.
constexpr uint32_t kWatermark720pWidth = 144;
constexpr uint32_t kWatermark720pHeight = 84;

constexpr uint32_t kWatermark480pWidth = 88;
constexpr uint32_t kWatermark480pHeight = 48;

constexpr uint32_t kWatermark360pWidth = 73;
constexpr uint32_t kWatermark360pHeight = 42;

constexpr fuchsia::sysmem::PixelFormatType kWatermarkPixelFormat =
    fuchsia::sysmem::PixelFormatType::R8G8B8A8;

}  // namespace

// Returns the internal monitor configuration (DS).
InternalConfigNode MonitorConfigDownScaledRes();

// Returns the internal monitor configuration (FR).
InternalConfigNode MonitorConfigFullRes();

// Return the external monitor configuration.
fuchsia::camera2::hal::Config MonitoringConfig();

constexpr FrameRateRange kMonitoringFrameRateRange = {
    .min =
        {
            .frames_per_sec_numerator = kMinOutputStreamMonitoringFrameRate,
            .frames_per_sec_denominator = 1,
        },
    .max =
        {
            .frames_per_sec_numerator = kMaxOutputStreamMonitoringFrameRate,
            .frames_per_sec_denominator = 1,
        },
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_SHERLOCK_MONITORING_CONFIG_H_
