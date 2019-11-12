
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pipeline_manager.h"
#include "src/lib/syslog/cpp/logger.h"

namespace camera {

const char* ToConfigFileName(const camera::GdcConfig& config_type) {
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

fit::result<gdc_config_info, zx_status_t> PipelineManager::LoadGdcConfiguration(
    const camera::GdcConfig& config_type) {
  if (config_type == GdcConfig::INVALID) {
    FX_LOGS(ERROR) << "Invalid GDC configuration type";
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  gdc_config_info info;
  size_t size;
  auto status = load_firmware(device_, ToConfigFileName(config_type), &info.config_vmo, &size);
  if (status != ZX_OK || size == 0) {
    FX_PLOGS(ERROR, status) << "Failed to load the GDC firmware";
    return fit::error(status);
  }
  info.size = size;
  return fit::ok(std::move(info));
}

}  // namespace camera
