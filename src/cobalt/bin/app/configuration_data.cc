// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/configuration_data.h"

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fsl/syslogger/init.h"
#include "src/lib/fxl/strings/trim.h"
#include "third_party/cobalt/src/lib/util/file_util.h"

namespace cobalt {

const char FuchsiaConfigurationData::kDefaultEnvironmentDir[] = "/pkg/data";

constexpr char kCobaltEnvironmentFile[] = "cobalt_environment";
const config::Environment kDefaultEnvironment = config::Environment::PROD;

const char FuchsiaConfigurationData::kDefaultConfigDir[] = "/config/data";
constexpr char kReleaseStageFile[] = "release_stage";
const cobalt::ReleaseStage kDefaultReleaseStage = cobalt::ReleaseStage::GA;

// This will be found under the config directory.
constexpr char kApiKeyFile[] = "api_key.hex";
constexpr char kDefaultApiKey[] = "cobalt-default-api-key";

constexpr char kAnalyzerDevelTinkPublicKeyPath[] = "/pkg/data/keys/analyzer_devel_public";
constexpr char kShufflerDevelTinkPublicKeyPath[] = "/pkg/data/keys/shuffler_devel_public";
constexpr char kAnalyzerProdTinkPublicKeyPath[] = "/pkg/data/keys/analyzer_prod_public";
constexpr char kShufflerProdTinkPublicKeyPath[] = "/pkg/data/keys/shuffler_prod_public";

// Parse the cobalt environment value from the config data.
config::Environment LookupCobaltEnvironment(const std::string& environment_dir) {
  auto environment_path = files::JoinPath(environment_dir, kCobaltEnvironmentFile);
  std::string cobalt_environment;
  if (files::ReadFileToString(environment_path, &cobalt_environment)) {
    FX_LOGS(INFO) << "Loaded Cobalt environment from config file " << environment_path << ": "
                  << cobalt_environment;
    if (cobalt_environment == "LOCAL")
      return config::Environment::LOCAL;
    if (cobalt_environment == "PROD")
      return config::Environment::PROD;
    if (cobalt_environment == "DEVEL")
      return config::Environment::DEVEL;
    FX_LOGS(ERROR) << "Failed to parse the contents of config file " << environment_path << ": "
                   << cobalt_environment
                   << ". Falling back to default environment: " << kDefaultEnvironment;
  } else {
    FX_LOGS(ERROR) << "Failed to read config file " << environment_path
                   << ". Falling back to default environment: " << kDefaultEnvironment;
  }
  return kDefaultEnvironment;
}

cobalt::ReleaseStage LookupReleaseStage(const std::string& config_dir) {
  auto release_stage_path = files::JoinPath(config_dir, kReleaseStageFile);
  std::string release_stage;
  if (files::ReadFileToString(release_stage_path, &release_stage)) {
    FX_LOGS(INFO) << "Loaded Cobalt release stage from config file " << release_stage_path << ": "
                  << release_stage;
    if (release_stage == "DEBUG") {
      return cobalt::ReleaseStage::DEBUG;
    } else if (release_stage == "FISHFOOD") {
      return cobalt::ReleaseStage::FISHFOOD;
    } else if (release_stage == "DOGFOOD") {
      return cobalt::ReleaseStage::DOGFOOD;
    } else if (release_stage == "GA") {
      return cobalt::ReleaseStage::GA;
    }

    FX_LOGS(ERROR) << "Failed to parse the release stage: `" << release_stage
                   << "`. Falling back to default of " << kDefaultReleaseStage << ".";
    return kDefaultReleaseStage;
  } else {
    FX_LOGS(ERROR) << "Unable to determine release stage. Defaulting to " << kDefaultReleaseStage
                   << ".";
    return kDefaultReleaseStage;
  }
}

std::string LookupApiKeyOrDefault(const std::string& config_dir) {
  auto api_key_path = files::JoinPath(config_dir, kApiKeyFile);
  std::string api_key = util::ReadHexFileOrDefault(api_key_path, kDefaultApiKey);
  if (api_key == kDefaultApiKey) {
    FX_LOGS(INFO) << "LookupApiKeyOrDefault: Using default Cobalt API key.";
  } else {
    FX_LOGS(INFO) << "LookupApiKeyOrDefault: Using secret Cobalt API key.";
  }

  return api_key;
}

FuchsiaConfigurationData::FuchsiaConfigurationData(const std::string& config_dir,
                                                   const std::string& environment_dir)
    : backend_environment_(LookupCobaltEnvironment(environment_dir)),
      backend_configuration_(config::ConfigurationData(backend_environment_)),
      release_stage_(LookupReleaseStage(config_dir)),
      api_key_(LookupApiKeyOrDefault(config_dir)) {}

config::Environment FuchsiaConfigurationData::GetBackendEnvironment() const {
  return backend_environment_;
}

const char* FuchsiaConfigurationData::AnalyzerPublicKeyPath() const {
  if (backend_environment_ == config::DEVEL)
    return kAnalyzerDevelTinkPublicKeyPath;
  if (backend_environment_ == config::PROD)
    return kAnalyzerProdTinkPublicKeyPath;
  FX_LOGS(ERROR) << "Failed to handle any environments. Falling back to using analyzer key for "
                    "DEVEL environment.";
  return kAnalyzerDevelTinkPublicKeyPath;
}

const char* FuchsiaConfigurationData::ShufflerPublicKeyPath() const {
  switch (backend_environment_) {
    case config::PROD:
      return kShufflerProdTinkPublicKeyPath;
    case config::DEVEL:
      return kShufflerDevelTinkPublicKeyPath;
    default: {
      FX_LOGS(ERROR) << "Failed to handle environment enum: " << backend_environment_
                     << ". Falling back to using shuffler key for DEVEL environment.";
      return kShufflerDevelTinkPublicKeyPath;
    }
  }
}

int32_t FuchsiaConfigurationData::GetLogSourceId() const {
  return backend_configuration_.GetLogSourceId();
}

cobalt::ReleaseStage FuchsiaConfigurationData::GetReleaseStage() const { return release_stage_; }

std::string FuchsiaConfigurationData::GetApiKey() const { return api_key_; }

}  // namespace cobalt
