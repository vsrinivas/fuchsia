// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSMGR_CONFIG_H_
#define GARNET_BIN_SYSMGR_CONFIG_H_

#include <string>
#include <unordered_map>
#include <utility>

#include <fuchsia/sys/cpp/fidl.h>
#include "garnet/lib/json/json_parser.h"
#include "lib/fxl/macros.h"
#include "third_party/rapidjson/rapidjson/document.h"

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

  Config() = default;
  Config(Config&&) = default;
  Config& operator=(Config&&) = default;

  // Initializes the Config from a JSON file. Returns false if there were
  // any errors.
  bool ParseFromFile(const std::string& config_file);

  // Initializes the Config from a JSON string. |pseudo_file| is used as the
  // 'file' in the error string.
  bool ParseFromString(const std::string& data, const std::string& pseudo_file);

  bool HasError() const;
  std::string error_str() const;

  ServiceMap TakeServices() { return std::move(services_); }

  StartupServiceVector TakeStartupServices() {
    return std::move(startup_services_);
  }

  ServiceMap TakeAppLoaders() { return std::move(app_loaders_); }

  AppVector TakeApps() { return std::move(apps_); }

 private:
  void Parse(const rapidjson::Document& document);
  bool ParseServiceMap(const rapidjson::Document& document,
                       const std::string& key, Config::ServiceMap* services);
  fuchsia::sys::LaunchInfoPtr GetLaunchInfo(
      const rapidjson::Document::ValueType& value, const std::string& name);

  ServiceMap services_;
  StartupServiceVector startup_services_;
  ServiceMap app_loaders_;
  AppVector apps_;
  json::JSONParser json_parser_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace sysmgr

#endif  // GARNET_BIN_SYSMGR_CONFIG_H_
