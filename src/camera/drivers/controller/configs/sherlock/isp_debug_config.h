// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <vector>

#include "src/camera/drivers/controller/configs/sherlock/common_util.h"
#include "src/camera/drivers/controller/configs/sherlock/internal_config.h"
#include "src/camera/lib/stream_utils/stream_constraints.h"

// Config 0: Debug configuration.
//          Stream 0: FR

namespace camera {

// Returns the internal debug configuration(FR).
InternalConfigNode DebugConfigFullRes();

// Return the external debug configuration.
fuchsia::camera2::hal::Config DebugConfig();

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONFIGS_SHERLOCK_ISP_DEBUG_CONFIG_H_
