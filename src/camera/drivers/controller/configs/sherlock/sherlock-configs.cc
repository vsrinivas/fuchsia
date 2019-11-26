// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../../controller-protocol.h"
#include "isp-debug-config.h"
#include "monitoring-config.h"
#include "video-conferencing-config.h"

namespace camera {

// Along with providing the external configuration, we also
// populate the internal configuration here.
// NOTE: we need to ensure that we are keeping the order of
// external and internal configuration same so we can lookup
// the internal data easily when needed.
std::vector<fuchsia::camera2::hal::Config> ControllerImpl::SherlockConfigs() {
  std::vector<fuchsia::camera2::hal::Config> configs;

  // Debug configuration.
  configs.push_back(DebugConfig());
  InternalConfigInfo debug_config_info;
  debug_config_info.streams_info.push_back(DebugConfigFullRes());

  // Monitoring configuration.
  configs.push_back(MonitoringConfig());
  InternalConfigInfo monitor_config_info;
  monitor_config_info.streams_info.push_back(MonitorConfigFullRes());
  monitor_config_info.streams_info.push_back(MonitorConfigDownScaledRes());

  // Video conferencing configuration.
  configs.push_back(VideoConferencingConfig());
  InternalConfigInfo video_conferencing_config_info;
  video_conferencing_config_info.streams_info.push_back(VideoConfigFullRes());

  // Pushing the internal configurations
  internal_configs_.configs_info.push_back(std::move(debug_config_info));
  internal_configs_.configs_info.push_back(std::move(monitor_config_info));
  internal_configs_.configs_info.push_back(std::move(video_conferencing_config_info));

  return configs;
}

}  // namespace camera
