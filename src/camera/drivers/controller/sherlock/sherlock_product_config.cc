// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/sherlock/sherlock_product_config.h"

#include <sstream>

#include "src/camera/drivers/controller/sherlock/isp_debug_config.h"
#include "src/camera/drivers/controller/sherlock/monitoring_config.h"
#include "src/camera/drivers/controller/sherlock/video_conferencing_config.h"
#include "src/camera/lib/formatting/formatting.h"

namespace camera {

fuchsia::camera2::DeviceInfo ProductConfig::DeviceInfo() {
  fuchsia::camera2::DeviceInfo info{};
  info.set_vendor_name("Google Inc.");
  info.set_vendor_id(0x18D1);
  info.set_product_name("Fuchsia Sherlock Camera");
  info.set_product_id(0xF00D);
  info.set_type(fuchsia::camera2::DeviceType::BUILTIN);
  return info;
}

std::unique_ptr<ProductConfig> ProductConfig::Create() {
  return std::make_unique<SherlockProductConfig>();
}

std::vector<fuchsia::camera2::hal::Config> SherlockProductConfig::ExternalConfigs() const {
  std::vector<fuchsia::camera2::hal::Config> configs;

  // Monitoring configuration.
  configs.push_back(MonitoringConfig());

  // Video conferencing configuration.
  configs.push_back(VideoConferencingConfig(false));

  // Video conferencing configuration with extended FOV enabled.
  configs.push_back(VideoConferencingConfig(true));

  return configs;
}

InternalConfigs SherlockProductConfig::InternalConfigs() const {
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

const char* SherlockProductConfig::GetGdcConfigFile(GdcConfig config_type) const {
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

// NOLINTNEXTLINE(misc-no-recursion): struct is recursive :(
static std::string DumpInternalConfigNode(const InternalConfigNode& node, size_t indent_width = 0) {
  static const std::map<NodeType, const char*> kNodeTypeNames{
      {NodeType::kInputStream, "Input"},
      {NodeType::kGdc, "GDC"},
      {NodeType::kGe2d, "GE2D"},
      {NodeType::kOutputStream, "Output"},
      {NodeType::kPassthrough, "Passthrough"}};
  std::string indent(indent_width, ' ');
  std::stringstream ss;
  ss << indent << "type = " << kNodeTypeNames.at(node.type) << "\n";
  ss << indent << "output_framerate = " << formatting::ToString(node.output_frame_rate);
  ss << indent << "supported_streams[" << node.supported_streams.size() << "]:\n";
  for (uint32_t i = 0; i < node.supported_streams.size(); ++i) {
    auto& stream = node.supported_streams[i];
    ss << indent << "  [" << i << "]: (";
    if (stream.supports_crop_region) {
      ss << "C";
    }
    if (stream.supports_dynamic_resolution) {
      ss << "R";
    }
    ss << ") " << formatting::ToString(stream.type);
  }
  ss << indent << "image_formats[" << node.image_formats.size() << "]:\n";
  for (uint32_t i = 0; i < node.image_formats.size(); ++i) {
    ss << indent << "  [" << i << "]: " << formatting::ToString(node.image_formats[i]);
  }
  ss << indent << "input_constraints: ";
  if (node.input_constraints) {
    ss << formatting::ToString(*node.input_constraints);
  } else {
    ss << "<none>\n";
  }
  ss << indent << "output_constraints:\n";
  if (node.output_constraints) {
    ss << formatting::ToString(*node.output_constraints);
  } else {
    ss << "<none>\n";
  }
  if (!node.child_nodes.empty()) {
    ss << indent << "child_nodes[" << node.child_nodes.size() << "]:\n";
    for (uint32_t i = 0; i < node.child_nodes.size(); ++i) {
      ss << indent << "  [" << i << "]:\n";
      ss << DumpInternalConfigNode(node.child_nodes[i], indent_width + 4);
    }
  }
  return ss.str();
}

std::string SherlockProductConfig::ToString() const {
  std::stringstream ss;
  auto externals = ExternalConfigs();
  ss << "ExternalConfigs[" << externals.size() << "]:\n";
  for (uint32_t i = 0; i < externals.size(); ++i) {
    ss << "  [" << i << "]: " << formatting::ToString(externals[i]);
  }
  auto internals = InternalConfigs();
  ss << "InternalConfigs[" << internals.configs_info.size() << "]:\n";
  for (uint32_t i = 0; i < internals.configs_info.size(); ++i) {
    auto& config = internals.configs_info[i];
    ss << "  [" << i << "] streams_info[" << config.streams_info.size() << "]:\n";
    for (uint32_t j = 0; j < config.streams_info.size(); ++j) {
      auto& stream = config.streams_info[j];
      ss << "    [" << j << "]:\n";
      ss << DumpInternalConfigNode(stream, 6);
    }
  }
  return ss.str();
}

}  // namespace camera
