// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/configs/sherlock/sherlock_configs.h"

namespace camera {

std::vector<fuchsia::camera2::hal::Config> SherlockExternalConfigs() {
  std::vector<fuchsia::camera2::hal::Config> configs;

  // Monitoring configuration.
  configs.push_back(MonitoringConfig());

  // Video conferencing configuration.
  configs.push_back(VideoConferencingConfig(false));

  // Video conferencing configuration with extended FOV enabled.
  configs.push_back(VideoConferencingConfig(true));

  return configs;
}

InternalConfigs SherlockInternalConfigs() {
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

}  // namespace camera
