// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_SHERLOCK_CONFIGS_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_SHERLOCK_CONFIGS_H_

#include <vector>

#include "src/camera/drivers/controller/configs/sherlock/internal_config.h"
#include "src/camera/drivers/controller/configs/sherlock/isp_debug_config.h"
#include "src/camera/drivers/controller/configs/sherlock/monitoring_config.h"
#include "src/camera/drivers/controller/configs/sherlock/video_conferencing_config.h"

namespace camera {

// Along with providing the external configuration, we also
// populate the internal configuration here.
// NOTE: we need to ensure that we are keeping the order of
// external and internal configuration same so we can lookup
// the internal data easily when needed.
// The demo uses the config and stream indexes to setup a
// particular stream. These indexes are based on what order
// we populate the different configurations below.
// Following is the order of configs and streams for Sherlock.
// Config 0: Debug configuration.
//          Stream 0: FR
// Config 1: Monitoring configuration.
//          Stream 0: ML | FR
//          Stream 1: ML | DS
//          Stream 2: MONITORING
// Config 2: Video conferencing configuration.
//          Stream 0: ML | FR | VIDEO
//          Stream 1: VIDEO
std::vector<fuchsia::camera2::hal::Config> SherlockExternalConfigs();

// Returns the internal configuration corresponding to the
// external configuration.
InternalConfigs SherlockInternalConfigs();

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_SHERLOCK_CONFIGS_H_
