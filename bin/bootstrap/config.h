// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_BOOTSTRAP_CONFIG_H_
#define APPLICATION_SRC_BOOTSTRAP_CONFIG_H_

#include <string>
#include <unordered_map>
#include <utility>

#include "application/services/application_launcher.fidl.h"
#include "lib/ftl/macros.h"

namespace bootstrap {

// Parses configuration files.  See README.md for format.
// TODO(jeffbrown): Support chaining multiple configuration files together
// via imports.
class Config {
 public:
  using ServiceMap =
      std::unordered_map<std::string, app::ApplicationLaunchInfoPtr>;
  using AppVector = std::vector<app::ApplicationLaunchInfoPtr>;

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

  FTL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace bootstrap

#endif  // APPLICATION_SRC_BOOTSTRAP_CONFIG_H_
