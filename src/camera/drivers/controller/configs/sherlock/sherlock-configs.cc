// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../../controller-protocol.h"
#include "isp-debug-config.h"

namespace camera {

std::vector<fuchsia::camera2::hal::Config> ControllerImpl::SherlockConfigs() {
  std::vector<fuchsia::camera2::hal::Config> configs;
  configs.push_back(std::move(DebugConfig()));
  return configs;
}

}  // namespace camera
