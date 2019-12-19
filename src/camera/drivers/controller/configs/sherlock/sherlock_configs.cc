// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/configs/sherlock/sherlock_configs.h"

namespace camera {

std::vector<fuchsia::camera2::hal::Config> SherlockExternalConfigs() {
  std::vector<fuchsia::camera2::hal::Config> configs;

  // Debug configuration.
  configs.push_back(DebugConfig());

  // Monitoring configuration.
  configs.push_back(MonitoringConfig());

  // Video conferencing configuration.
  configs.push_back(VideoConferencingConfig());

  return configs;
}

InternalConfigs SherlockInternalConfigs() {
  return {
      .configs_info =
          {
              // Debug configuration.
              {
                  .streams_info =
                      {
                          DebugConfigFullRes(),
                      },
              },
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
              },
              // Video conferencing configuration.
              {
                  .streams_info =
                      {
                          VideoConfigFullRes(),
                      },
              },
          },
  };
}

}  // namespace camera
