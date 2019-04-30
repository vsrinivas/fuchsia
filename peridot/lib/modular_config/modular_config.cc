// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/modular_config/modular_config.h"

#include "lib/json/json_parser.h"
#include "peridot/lib/modular_config/modular_config_constants.h"
#include "peridot/lib/modular_config/modular_config_xdr.h"
#include "src/lib/files/file.h"

namespace modular {

ModularConfigReader::ModularConfigReader() {}
ModularConfigReader::~ModularConfigReader() {}

fuchsia::modular::session::BasemgrConfig
ModularConfigReader::GetBasemgrConfig() {
  std::string config_path;
  if (files::IsFile(modular_config::kOverridenStartupConfigPath)) {
    config_path = modular_config::kOverridenStartupConfigPath;
  } else {
    config_path = modular_config::kStartupConfigPath;
  }

  // Get basemgr config section from file
  auto basemgr_config_str =
      GetConfigAsString(modular_config::kBasemgrConfigName, config_path);

  // Parse with xdr
  fuchsia::modular::session::BasemgrConfig basemgr_config;
  if (!basemgr_config_str.empty() &&
      !XdrRead(basemgr_config_str, &basemgr_config, XdrBasemgrConfig)) {
    FXL_LOG(ERROR) << "Unable to parse startup.json";
  }

  return basemgr_config;
}

fuchsia::modular::session::SessionmgrConfig
ModularConfigReader::GetSessionmgrConfig() {
  // Get sessionmgr config section from file
  auto sessionmgr_config_str =
      GetConfigAsString(modular_config::kSessionmgrConfigName,
                        modular_config::kOverridenStartupConfigPath);

  // Parse with xdr
  fuchsia::modular::session::SessionmgrConfig sessionmgr_config;
  if (!XdrRead(sessionmgr_config_str, &sessionmgr_config,
               XdrSessionmgrConfig)) {
    FXL_LOG(ERROR) << "Unable to parse startup.json";
  }

  return sessionmgr_config;
}

fuchsia::modular::session::SessionmgrConfig
ModularConfigReader::GetDefaultSessionmgrConfig() {
  fuchsia::modular::session::SessionmgrConfig sessionmgr_config;
  XdrRead("\"\"", &sessionmgr_config, XdrSessionmgrConfig);
  return sessionmgr_config;
}

std::string ModularConfigReader::GetConfigAsString(
    const std::string& config_name, std::string config_path) {
  // Check that config file exists

  if (!files::IsFile(config_path)) {
    FXL_LOG(ERROR) << config_path << " does not exist.";
    return "\"\"";
  }

  std::string json;
  if (!files::ReadFileToString(config_path, &json)) {
    FXL_LOG(ERROR) << "Unable to read " << config_path;
    return "\"\"";
  }

  json::JSONParser json_parser;
  auto startup_config = json_parser.ParseFromString(json, config_path);
  if (json_parser.HasError()) {
    FXL_LOG(ERROR) << "Error while parsing " << config_path
                   << " to string. Error: " << json_parser.error_str();
    return "\"\"";
  }

  // Get |config_name| section from file
  auto config_json = startup_config.FindMember(config_name);
  if (config_json == startup_config.MemberEnd()) {
    // |config_name| was not found
    FXL_LOG(ERROR) << config_name << " configurations were not found in "
                   << config_path;
    return "\"\"";
  }

  return modular::JsonValueToString(config_json->value);
}

}  // namespace modular