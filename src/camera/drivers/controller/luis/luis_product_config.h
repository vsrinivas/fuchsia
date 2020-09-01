// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_LUIS_LUIS_PRODUCT_CONFIG_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_LUIS_LUIS_PRODUCT_CONFIG_H_

#include <vector>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/camera/drivers/controller/configs/product_config.h"

namespace camera {

class LuisProductConfig : public ProductConfig {
 public:
  // Along with providing the external configuration, we also
  // populate the internal configuration here.
  // NOTE: we need to ensure that we are keeping the order of
  // external and internal configuration same so we can lookup
  // the internal data easily when needed.
  // The demo uses the config and stream indexes to setup a
  // particular stream. These indexes are based on what order
  // we populate the different configurations below.
  // Following is the order of configs and streams for Luis.
  // Config 0: Monitoring configuration.
  //          Stream 0: IspDebug | FR

  std::vector<fuchsia::camera2::hal::Config> ExternalConfigs() override;

  // Returns the internal configuration corresponding to the
  // external configuration.
  struct InternalConfigs InternalConfigs() override;

  const char* GetGdcConfigFile(GdcConfig config_type) override;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_LUIS_LUIS_PRODUCT_CONFIG_H_
