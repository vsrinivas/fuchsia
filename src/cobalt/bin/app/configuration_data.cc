// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/configuration_data.h"

#include <third_party/protobuf/src/google/protobuf/stubs/map_util.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fsl/syslogger/init.h"
#include "src/lib/fxl/strings/trim.h"

namespace cobalt {

const char FuchsiaConfigurationData::kDefaultConfigDir[] = "/pkg/data";
constexpr char kCobaltEnvironmentFile[] = "cobalt_environment";
// TODO(camrdale): change the default to PROD once its pipeline is working.
const config::Environment kDefaultEnvironment = config::Environment::DEVEL;

constexpr char kAnalyzerDevelTinkPublicKeyPath[] = "/pkg/data/keys/analyzer_devel_public";
constexpr char kShufflerDevelTinkPublicKeyPath[] = "/pkg/data/keys/shuffler_devel_public";
constexpr char kAnalyzerProdTinkPublicKeyPath[] = "/pkg/data/keys/analyzer_prod_public";
constexpr char kShufflerProdTinkPublicKeyPath[] = "/pkg/data/keys/shuffler_prod_public";

// Parse the cobalt environment value from the config data.
std::vector<config::Environment> LookupCobaltEnvironment(const std::string& config_dir) {
  auto environment_path = files::JoinPath(config_dir, kCobaltEnvironmentFile);
  std::string cobalt_environment;
  if (files::ReadFileToString(environment_path, &cobalt_environment)) {
    FX_LOGS(INFO) << "Loaded Cobalt environment from config file " << environment_path << ": "
                  << cobalt_environment;
    if (cobalt_environment == "LOCAL")
      return std::vector({config::Environment::LOCAL});
    if (cobalt_environment == "PROD")
      return std::vector({config::Environment::PROD});
    if (cobalt_environment == "DEVEL")
      return std::vector({config::Environment::DEVEL});
    // TODO(camrdale): remove this once the log source transition is complete.
    if (cobalt_environment == "DEVEL_AND_PROD")
      return std::vector({config::Environment::DEVEL, config::Environment::PROD});
    FX_LOGS(ERROR) << "Failed to parse the contents of config file " << environment_path << ": "
                   << cobalt_environment
                   << ". Falling back to default environment: " << kDefaultEnvironment;
  } else {
    FX_LOGS(ERROR) << "Failed to read config file " << environment_path
                   << ". Falling back to default environment: " << kDefaultEnvironment;
  }
  return std::vector({kDefaultEnvironment});
}

FuchsiaConfigurationData::FuchsiaConfigurationData(const std::string& config_dir) {
  for (const auto& environment : LookupCobaltEnvironment(config_dir)) {
    backend_configurations_.emplace(environment, environment);
  }
}

std::vector<config::Environment> FuchsiaConfigurationData::GetBackendEnvironments() const {
  std::vector<config::Environment> envs;
  for (const auto& entry : backend_configurations_) {
    envs.push_back(entry.first);
  }
  return envs;
}

const char* FuchsiaConfigurationData::AnalyzerPublicKeyPath() const {
  // Use devel keys even if we are also writing to prod. The prod pipeline can handle Observations
  // encrypted with the devel keys.
  if (backend_configurations_.find(config::DEVEL) != backend_configurations_.end())
    return kAnalyzerDevelTinkPublicKeyPath;
  if (backend_configurations_.find(config::PROD) != backend_configurations_.end())
    return kAnalyzerProdTinkPublicKeyPath;
  FX_LOGS(ERROR) << "Failed to handle any environments. Falling back to using analyzer key for "
                    "DEVEL environment.";
  return kAnalyzerDevelTinkPublicKeyPath;
}

const char* FuchsiaConfigurationData::ShufflerPublicKeyPath(
    const config::Environment& backend_environment) const {
  switch (backend_environment) {
    case config::PROD:
      return kShufflerProdTinkPublicKeyPath;
    case config::DEVEL:
      return kShufflerDevelTinkPublicKeyPath;
    default: {
      FX_LOGS(ERROR) << "Failed to handle environment enum: " << backend_environment
                     << ". Falling back to using shuffler key for DEVEL environment.";
      return kShufflerDevelTinkPublicKeyPath;
    }
  }
}

int32_t FuchsiaConfigurationData::GetLogSourceId(
    const config::Environment& backend_environment) const {
  return backend_configurations_.find(backend_environment)->second.GetLogSourceId();
}

}  // namespace cobalt
