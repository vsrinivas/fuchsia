// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/configuration_data.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/trim.h"
#include "src/lib/json_parser/json_parser.h"
#include "third_party/cobalt/src/lib/statusor/status_macros.h"
#include "third_party/cobalt/src/lib/util/file_util.h"
#include "third_party/cobalt/src/public/cobalt_service_interface.h"

namespace cobalt {

using cobalt::lib::statusor::StatusOr;
using cobalt::util::Status;

const char FuchsiaConfigurationData::kDefaultEnvironmentDir[] = "/pkg/data";
const char FuchsiaConfigurationData::kDefaultConfigDir[] = "/config/data";

namespace {

constexpr char kCobaltEnvironmentFile[] = "cobalt_environment";
const config::Environment kDefaultEnvironment = config::Environment::PROD;

constexpr char kConfigFile[] = "config.json";

constexpr char kReleaseStageKey[] = "release_stage";
const cobalt::ReleaseStage kDefaultReleaseStage = cobalt::ReleaseStage::GA;

constexpr char kDefaultDataCollectionPolicyKey[] = "default_data_collection_policy";
// When we start Cobalt, we have no idea what the current state of user consent is. Starting with
// DO_NOT_UPLOAD will allow us to collect metrics while the system is booting, before we get an
// updated policy from the UserConsentWatcher.
//
// If we started with DO_NOT_COLLECT, we could possibly miss early boot metrics entirely, and if
// we started with COLLECT_AND_UPLOAD, we could possibly violate the user's chosen
// DataCollectionPolicy by uploading metrics when they have opted out.
const cobalt::CobaltServiceInterface::DataCollectionPolicy kDefaultDataCollectionPolicy =
    cobalt::CobaltServiceInterface::DataCollectionPolicy::DO_NOT_UPLOAD;

constexpr char kWatchForUserConsentKey[] = "watch_for_user_consent";
const bool kDefaultWatchForUserConsent = true;

// This will be found under the config directory.
constexpr char kApiKeyFile[] = "api_key.hex";
constexpr char kDefaultApiKey[] = "cobalt-default-api-key";

constexpr char kAnalyzerDevelTinkPublicKeyPath[] = "/pkg/data/keys/analyzer_devel_public";
constexpr char kShufflerDevelTinkPublicKeyPath[] = "/pkg/data/keys/shuffler_devel_public";
constexpr char kAnalyzerProdTinkPublicKeyPath[] = "/pkg/data/keys/analyzer_prod_public";
constexpr char kShufflerProdTinkPublicKeyPath[] = "/pkg/data/keys/shuffler_prod_public";

}  // namespace

JSONHelper::JSONHelper(const std::string& path)
    : config_file_contents_(json_parser_.ParseFromFile(path)) {}

template <typename T>
StatusOr<T> MakeBadTypeError(const std::string& key, const std::string& expected,
                             rapidjson::Type actual) {
  static const char* kTypeNames[] = {"Null",  "False",  "True",  "Object",
                                     "Array", "String", "Number"};

  return Status(util::StatusCode::INVALID_ARGUMENT,
                fxl::Concatenate({"Key ", key, " is not of type ", expected, "."}),
                fxl::Concatenate({"Key ", key, " is expected to be a ", expected,
                                  ", but was instead a ", std::string(kTypeNames[actual])}));
}

StatusOr<std::string> JSONHelper::GetString(const std::string& key) const {
  CB_RETURN_IF_ERROR(EnsureKey(key));

  if (!config_file_contents_[key].IsString()) {
    return MakeBadTypeError<std::string>(key, "string", config_file_contents_[key].GetType());
  }

  return StatusOr(config_file_contents_[key].GetString());
}

StatusOr<bool> JSONHelper::GetBool(const std::string& key) const {
  CB_RETURN_IF_ERROR(EnsureKey(key));

  if (!config_file_contents_[key].IsBool()) {
    return MakeBadTypeError<bool>(key, "bool", config_file_contents_[key].GetType());
  }

  return config_file_contents_[key].GetBool();
}

Status JSONHelper::EnsureKey(const std::string& key) const {
  if (json_parser_.HasError()) {
    return Status(util::StatusCode::INTERNAL, "Failed to parse json file.",
                  json_parser_.error_str());
  }

  if (!config_file_contents_.HasMember(key)) {
    return Status(util::StatusCode::NOT_FOUND,
                  fxl::Concatenate({"Key ", key, " not present in the config."}));
  }

  return Status::OK;
}

namespace {
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

#define ASSIGN_OR_RETURN_DEFAULT(lhs, def, rexpr) \
  ASSIGN_OR_RETURN_DEFAULT_IMPL(_status_or_value##__COUNTER__, lhs, def, rexpr)

#define ASSIGN_OR_RETURN_DEFAULT_IMPL(statusor, lhs, def, rexpr)                         \
  auto statusor = (rexpr);                                                               \
  if (!statusor.ok()) {                                                                  \
    auto status = statusor.status();                                                     \
    if (status.error_details().empty()) {                                                \
      FX_LOGS(ERROR) << "Failed to read from config. " << status.error_message()         \
                     << ". Using default.";                                              \
    } else {                                                                             \
      FX_LOGS(ERROR) << "Failed to read from config. " << status.error_message() << " (" \
                     << status.error_details() << "). Using default.";                   \
    }                                                                                    \
    return def;                                                                          \
  }                                                                                      \
  lhs = std::move(statusor.ValueOrDie())

cobalt::ReleaseStage LookupReleaseStage(const JSONHelper& json_helper) {
  ASSIGN_OR_RETURN_DEFAULT(auto release_stage, kDefaultReleaseStage,
                           json_helper.GetString(kReleaseStageKey));

  FX_LOGS(INFO) << "Loaded Cobalt release stage from config file: " << release_stage;
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
}

cobalt::CobaltServiceInterface::DataCollectionPolicy LookupDataCollectionPolicy(
    const JSONHelper& json_helper) {
  ASSIGN_OR_RETURN_DEFAULT(auto data_collection_policy, kDefaultDataCollectionPolicy,
                           json_helper.GetString(kDefaultDataCollectionPolicyKey));

  FX_LOGS(INFO) << "Loaded Cobalt data collection policy from config file: "
                << data_collection_policy;
  if (data_collection_policy == "DO_NOT_COLLECT") {
    return cobalt::CobaltServiceInterface::DataCollectionPolicy::DO_NOT_COLLECT;
  } else if (data_collection_policy == "DO_NOT_UPLOAD") {
    return cobalt::CobaltServiceInterface::DataCollectionPolicy::DO_NOT_UPLOAD;
  } else if (data_collection_policy == "COLLECT_AND_UPLOAD") {
    return cobalt::CobaltServiceInterface::DataCollectionPolicy::COLLECT_AND_UPLOAD;
  }

  FX_LOGS(ERROR) << "Failed to parse the data collection policy: `" << data_collection_policy
                 << "`. Falling back to default.";
  return kDefaultDataCollectionPolicy;
}

bool LookupWatchForUserConsent(const JSONHelper& json_helper) {
  ASSIGN_OR_RETURN_DEFAULT(auto watch_for_user_consent, kDefaultWatchForUserConsent,
                           json_helper.GetBool(kWatchForUserConsentKey));

  return watch_for_user_consent;
}

}  // namespace

FuchsiaConfigurationData::FuchsiaConfigurationData(const std::string& config_dir,
                                                   const std::string& environment_dir)
    : backend_environment_(LookupCobaltEnvironment(environment_dir)),
      backend_configuration_(config::ConfigurationData(backend_environment_)),
      api_key_(LookupApiKeyOrDefault(config_dir)),
      json_helper_(files::JoinPath(config_dir, kConfigFile)),
      release_stage_(LookupReleaseStage(json_helper_)),
      data_collection_policy_(LookupDataCollectionPolicy(json_helper_)),
      watch_for_user_consent_(LookupWatchForUserConsent(json_helper_)) {}

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

cobalt::CobaltServiceInterface::DataCollectionPolicy
FuchsiaConfigurationData::GetDataCollectionPolicy() const {
  return data_collection_policy_;
}

bool FuchsiaConfigurationData::GetWatchForUserConsent() const { return watch_for_user_consent_; }

std::string FuchsiaConfigurationData::GetApiKey() const { return api_key_; }

}  // namespace cobalt
