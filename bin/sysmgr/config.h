// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSMGR_CONFIG_H_
#define GARNET_BIN_SYSMGR_CONFIG_H_

#include <string>
#include <unordered_map>
#include <utility>

#include <fuchsia/sys/cpp/fidl.h>
#include "lib/fxl/macros.h"

namespace sysmgr {

// Parses configuration files.  See README.md for format.
// TODO(jeffbrown): Support chaining multiple configuration files together
// via imports.
class Config {
 public:
  using ServiceMap =
      std::unordered_map<std::string, fuchsia::sys::LaunchInfoPtr>;
  using StartupServiceVector = std::vector<std::string>;
  using AppVector = std::vector<fuchsia::sys::LaunchInfoPtr>;

  Config();

  Config(Config&& other);
  Config& operator=(Config&& other);

  ~Config();

  bool ReadFrom(const std::string& config_file);

  void Parse(const std::string& data, const std::string& config_file);

  bool HasErrors() const { return !errors_.empty(); }

  ServiceMap TakeServices() { return std::move(services_); }
  StartupServiceVector TakeStartupServices() {
    return std::move(startup_services_);
  }
  ServiceMap TakeAppLoaders() { return std::move(app_loaders_); }
  AppVector TakeApps() { return std::move(apps_); }
  // GetErrors obtains a reference to the list of errors from parsing.
  const std::vector<std::string>& GetErrors() const { return errors_; }
  // GetFailedConfig returns the content of the config file. This method returns
  // an empty string if the config was parsed correctly.
  const std::string& GetFailedConfig() const { return config_data_; }

 private:
  ServiceMap services_;
  StartupServiceVector startup_services_;
  ServiceMap app_loaders_;
  AppVector apps_;
  std::vector<std::string> errors_;
  std::string config_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace sysmgr

#endif  // GARNET_BIN_SYSMGR_CONFIG_H_
