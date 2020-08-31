// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_SYSMGR_CONFIG_H_
#define SRC_SYS_SYSMGR_CONFIG_H_

#include <fuchsia/sys/cpp/fidl.h>

#include <string>
#include <unordered_map>
#include <utility>

#include "rapidjson/document.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/json_parser/json_parser.h"

namespace sysmgr {

// Parses configuration files.  See README.md for format.
// TODO(jeffbrown): Support chaining multiple configuration files together
// via imports.
class Config {
 public:
  using ServiceMap = std::unordered_map<std::string, fuchsia::sys::LaunchInfoPtr>;
  using StartupServices = std::vector<std::string>;
  using UpdateDependencies = std::vector<std::string>;
  using OptionalServices = std::vector<std::string>;
  using AppVector = std::vector<fuchsia::sys::LaunchInfoPtr>;
  using CriticalComponents = std::vector<std::string>;

  Config() = default;
  Config(Config&&) = default;
  Config& operator=(Config&&) = default;

  // Initializes the Config from a config directory, merging its files together.
  // Returns false if there were any errors.
  bool ParseFromDirectory(const std::string& dir);

  bool HasError() const;
  std::string error_str() const;

  ServiceMap TakeServices() { return std::move(services_); }

  StartupServices TakeStartupServices() { return std::move(startup_services_); }

  UpdateDependencies TakeUpdateDependencies() { return std::move(update_dependencies_); }

  OptionalServices TakeOptionalServices() { return std::move(optional_services_); }

  CriticalComponents TakeCriticalComponents() { return std::move(critical_components_); }

  AppVector TakeApps() { return std::move(apps_); }

 private:
  void ParseDocument(rapidjson::Document document);
  bool ParseServiceMap(const rapidjson::Document& document, const std::string& key,
                       Config::ServiceMap* services);
  void ReadJsonStringArray(const rapidjson::Document& document, const char* member,
                           std::vector<std::string>* out);
  fuchsia::sys::LaunchInfoPtr GetLaunchInfo(const rapidjson::Document::ValueType& value,
                                            const std::string& name);

  ServiceMap services_;
  StartupServices startup_services_;
  UpdateDependencies update_dependencies_;
  OptionalServices optional_services_;
  AppVector apps_;
  json::JSONParser json_parser_;
  std::string diagnostics_url_;
  CriticalComponents critical_components_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Config);
};

}  // namespace sysmgr

#endif  // SRC_SYS_SYSMGR_CONFIG_H_
