// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSMGR_CONFIG_H_
#define GARNET_BIN_SYSMGR_CONFIG_H_

#include <string>
#include <unordered_map>
#include <utility>

#include <component/cpp/fidl.h>
#include "lib/fxl/macros.h"

namespace sysmgr {

// Parses configuration files.  See README.md for format.
// TODO(jeffbrown): Support chaining multiple configuration files together
// via imports.
class Config {
 public:
  using ServiceMap =
      std::unordered_map<std::string, component::LaunchInfoPtr>;
  using AppVector = std::vector<component::LaunchInfoPtr>;

  Config();
  ~Config();

  bool ReadFrom(const std::string& config_file);

  bool Parse(const std::string& data, const std::string& config_file);

  ServiceMap TakeServices() { return std::move(services_); }
  ServiceMap TakeAppLoaders() { return std::move(app_loaders_); }
  AppVector TakeApps() { return std::move(apps_); }

 private:
  ServiceMap services_;
  ServiceMap app_loaders_;
  AppVector apps_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace sysmgr

#endif  // GARNET_BIN_SYSMGR_CONFIG_H_
