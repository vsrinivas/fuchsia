// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/configuration_data.h"

#include <lib/fsl/syslogger/init.h>

#include <third_party/protobuf/src/google/protobuf/stubs/map_util.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
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

const std::map<std::string, config::Environment> environmentStrings = {
    {"PROD", config::Environment::PROD}, {"DEVEL", config::Environment::DEVEL}};

// Parse the cobalt environment value from the config data.
config::Environment LookupCobaltEnvironment(const std::string& config_dir) {
  auto environment_path = files::JoinPath(config_dir, kCobaltEnvironmentFile);
  std::string cobalt_environment;
  if (files::ReadFileToString(environment_path, &cobalt_environment)) {
    const config::Environment* found_environment =
        google::protobuf::FindOrNull(environmentStrings, cobalt_environment);
    if (found_environment != nullptr) {
      FX_LOGS(INFO) << "Loaded Cobalt environment from config file " << environment_path << ": "
                    << *found_environment;
      return *found_environment;
    }
    FX_LOGS(ERROR) << "Failed to parse the contents of config file " << environment_path << ": "
                   << cobalt_environment
                   << ". Falling back to default environment: " << kDefaultEnvironment;
  } else {
    FX_LOGS(ERROR) << "Failed to read config file " << environment_path
                   << ". Falling back to default environment: " << kDefaultEnvironment;
  }
  return kDefaultEnvironment;
}

FuchsiaConfigurationData::FuchsiaConfigurationData(const std::string& config_dir)
    : ConfigurationData(LookupCobaltEnvironment(config_dir)) {}

const char* FuchsiaConfigurationData::AnalyzerPublicKeyPath() {
  switch (GetEnvironment()) {
    case config::PROD:
      return kAnalyzerProdTinkPublicKeyPath;
    case config::DEVEL:
      return kAnalyzerDevelTinkPublicKeyPath;
    default: {
      FX_LOGS(ERROR) << "Failed to handle environment enum: " << GetEnvironmentString()
                     << ". Falling back to using analyzer key for DEVEL environment.";
      return kAnalyzerDevelTinkPublicKeyPath;
    }
  }
}

const char* FuchsiaConfigurationData::ShufflerPublicKeyPath() {
  switch (GetEnvironment()) {
    case config::PROD:
      return kShufflerProdTinkPublicKeyPath;
    case config::DEVEL:
      return kShufflerDevelTinkPublicKeyPath;
    default: {
      FX_LOGS(ERROR) << "Failed to handle environment enum: " << GetEnvironmentString()
                     << ". Falling back to using shuffler key for DEVEL environment.";
      return kShufflerDevelTinkPublicKeyPath;
    }
  }
}

}  // namespace cobalt
