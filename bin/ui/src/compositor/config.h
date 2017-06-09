// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_CONFIG_H_
#define APPS_MOZART_SRC_COMPOSITOR_CONFIG_H_

#include <string>
#include <unordered_map>
#include <utility>

#include "application/services/application_launcher.fidl.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "lib/ftl/macros.h"

namespace compositor {

class Config {
 public:
  Config();
  ~Config();

  bool ReadFrom(const std::string& config_file);

  bool Parse(const std::string& data, const std::string& config_file);

  float device_pixel_ratio() { return device_pixel_ratio_; }

 private:
  float device_pixel_ratio_ = 1.f;

  FTL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_CONFIG_H_
