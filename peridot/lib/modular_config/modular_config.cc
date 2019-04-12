// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/modular_config/modular_config.h"

#include "lib/json/json_parser.h"
#include "peridot/lib/modular_config/modular_config_xdr.h"
#include "src/lib/files/file.h"

namespace modular {
namespace {

constexpr char kBasemgrConfigName[] = "basemgr";
constexpr char kSessionmgrConfigName[] = "sessionmgr";
constexpr char kStartupConfigPath[] = "/config/data/startup.config";

}  // namespace

ModularConfigReader::ModularConfigReader() {}
ModularConfigReader::~ModularConfigReader() {}

fuchsia::modular::internal::BasemgrConfig
ModularConfigReader::GetBasemgrConfig() {
  // Get basemgr config section from file
  auto basemgr_config_str = GetConfigAsString(kBasemgrConfigName);

  // Parse with xdr
  fuchsia::modular::internal::BasemgrConfig basemgr_config;
  if (!basemgr_config_str.empty() &&
      !XdrRead(basemgr_config_str, &basemgr_config, XdrBasemgrConfig)) {
    FXL_LOG(ERROR) << "Unable to parse startup.json";
  }

  return basemgr_config;
}

fuchsia::modular::internal::SessionmgrConfig
ModularConfigReader::GetSessionmgrConfig() {
  // Get sessionmgr config section from file
  auto sessionmgr_config_str = GetConfigAsString(kSessionmgrConfigName);

  // Parse with xdr
  fuchsia::modular::internal::SessionmgrConfig sessionmgr_config;
  if (!XdrRead(sessionmgr_config_str, &sessionmgr_config,
               XdrSessionmgrConfig)) {
    FXL_LOG(ERROR) << "Unable to parse startup.json";
  }

  return sessionmgr_config;
}

fuchsia::modular::internal::SessionmgrConfig
ModularConfigReader::GetDefaultSessionmgrConfig() {
  fuchsia::modular::internal::SessionmgrConfig sessionmgr_config;
  XdrRead("\"\"", &sessionmgr_config, XdrSessionmgrConfig);
  return sessionmgr_config;
}

std::string ModularConfigReader::GetConfigAsString(
    const std::string& config_name) {
  // Check that config file exists
  if (!files::IsFile(kStartupConfigPath)) {
    FXL_LOG(ERROR) << kStartupConfigPath << " does not exist.";
    return "\"\"";
  }

  std::string json;
  if (!files::ReadFileToString(kStartupConfigPath, &json)) {
    FXL_LOG(ERROR) << "Unable to read " << kStartupConfigPath;
    return "\"\"";
  }

  json::JSONParser json_parser;
  auto startup_config = json_parser.ParseFromString(json, kStartupConfigPath);
  if (json_parser.HasError()) {
    FXL_LOG(ERROR) << "Error while parsing " << kStartupConfigPath
                   << " to string. Error: " << json_parser.error_str();
    return "\"\"";
  }

  // Get |config_name| section from file
  auto config_json = startup_config.FindMember(config_name);
  if (config_json == startup_config.MemberEnd()) {
    // |config_name| was not found
    FXL_LOG(ERROR) << config_name << " configurations were not found in "
                   << kStartupConfigPath;
    return "\"\"";
  }

  return modular::JsonValueToString(config_json->value);
}

}  // namespace modular