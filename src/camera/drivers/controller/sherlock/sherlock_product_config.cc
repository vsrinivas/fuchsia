// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/sherlock/sherlock_product_config.h"

#include "src/camera/drivers/controller/sherlock/isp_debug_config.h"
#include "src/camera/drivers/controller/sherlock/monitoring_config.h"
#include "src/camera/drivers/controller/sherlock/video_conferencing_config.h"

namespace camera {

std::unique_ptr<ProductConfig> ProductConfig::Create() {
  return std::make_unique<SherlockProductConfig>();
}

std::vector<fuchsia::camera2::hal::Config> SherlockProductConfig::ExternalConfigs() {
  std::vector<fuchsia::camera2::hal::Config> configs;

  // Monitoring configuration.
  configs.push_back(MonitoringConfig());

  // Video conferencing configuration.
  configs.push_back(VideoConferencingConfig(false));

  // Video conferencing configuration with extended FOV enabled.
  configs.push_back(VideoConferencingConfig(true));

  return configs;
}

InternalConfigs SherlockProductConfig::InternalConfigs() {
  return {
      .configs_info =
          {
              // Monitoring configuration.
              {
                  .streams_info =
                      {
                          {
                              MonitorConfigFullRes(),
                          },
                          {
                              MonitorConfigDownScaledRes(),
                          },
                      },
                  .frame_rate_range = kMonitoringFrameRateRange,
              },
              // Video conferencing configuration with extended FOV disabled.
              {
                  .streams_info =
                      {
                          VideoConfigFullRes(false),
                      },
                  .frame_rate_range = kVideoFrameRateRange,
              },
              // Video conferencing configuration with extended FOV enabled.
              {
                  .streams_info =
                      {
                          VideoConfigFullRes(true),
                      },
                  .frame_rate_range = kVideoFrameRateRange,
              },
          },
  };
}

const char* SherlockProductConfig::GetGdcConfigFile(GdcConfig config_type) {
  switch (config_type) {
    case GdcConfig::MONITORING_360p:
      return "config_1152x1440_to_512x384_Crop_Rotate.bin";
    case GdcConfig::MONITORING_480p:
      return "config_1152x1440_to_720x540_Crop_Rotate.bin";
    case GdcConfig::MONITORING_720p:
      return "config_1152x1440_to_1152x864_Crop_Rotate.bin";
    case GdcConfig::MONITORING_ML:
      return "config_001_2176x2720-to-640x512-RS-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE:
      return "config_002_2176x2720-to-2240x1792-DKCR-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE_EXTENDED_FOV:
      return "config_003_2176x2720-to-2240x1792-DKCR-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE_ML:
      return "config_001_2240x1792-to-640x512-S-YUV420SemiPlanar.bin";
    case GdcConfig::INVALID:
    default:
      return nullptr;
  }
}

}  // namespace camera
