// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/luis/luis_product_config.h"

#include "src/camera/drivers/controller/luis/isp_debug_config.h"

namespace camera {

std::unique_ptr<ProductConfig> ProductConfig::Create() {
  return std::make_unique<LuisProductConfig>();
}

std::vector<fuchsia::camera2::hal::Config> LuisProductConfig::ExternalConfigs() {
  std::vector<fuchsia::camera2::hal::Config> configs;

  // Debug configuration.
  configs.push_back(DebugConfig());

  return configs;
}

InternalConfigs LuisProductConfig::InternalConfigs() {
  return {
      .configs_info =
          {
              // Debug configuration.
              {
                  .streams_info =
                      {
                          {
                              DebugConfigFullRes(),
                          },
                      },
                  .frame_rate_range = kDebugFrameRateRange,
              },
          },
  };
}

const char* LuisProductConfig::GetGdcConfigFile(GdcConfig config_type) {
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
